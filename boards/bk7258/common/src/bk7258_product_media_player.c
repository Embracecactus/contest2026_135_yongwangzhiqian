/****************************************************************************
 * boards/bk7258/common/src/bk7258_product_media_player.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board product PCM media_player bridge for the official Agent.  This board
 * profile intentionally omits the full media framework, so URL decoding and
 * seeking remain unsupported.  Voice playback stays on the public NuttX
 * audio upper-half ABI and the BK7258 speaker lower half.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_BK7258_AUD) && \
    defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && \
    defined(CONFIG_BK7258_AGENT_MEDIA_PLAYER) && !defined(CONFIG_MEDIA)

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <media_player.h>
#include <media_policy.h>
#include <nuttx/audio/audio.h>
#include <nuttx/mutex.h>

#define BK7258_AGENT_PLAYER_RATE          16000u
#define BK7258_AGENT_PLAYER_BITS          16u
#define BK7258_AGENT_PLAYER_PRIME_BUFFERS 2u
#define BK7258_AGENT_PLAYER_VOLUME_MIN    0
#define BK7258_AGENT_PLAYER_VOLUME_MAX    15
#define BK7258_AGENT_PLAYER_VOLUME_DEFAULT 11
#define BK7258_AGENT_PLAYER_MQ_NAME_LEN   32u
#define BK7258_AGENT_PLAYER_RETRY_US      1000u
#define BK7258_AGENT_PLAYER_DRAIN_POLLS   500u

struct bk7258_agent_player_s
{
  int                 fd;
  mqd_t               mq;
  mutex_t             lock;
  bool                lock_initialized;
  bool                reserved;
  bool                mq_registered;
  bool                mq_created;
  bool                prepared;
  bool                started;
  bool                hardware_started;
  bool                stopping;
  uint8_t             input_channels;
  size_t              input_frame_bytes;
  size_t              buffer_count;
  size_t              outstanding;
  struct ap_buffer_s  *buffers[CONFIG_BK7258_AUD_QUEUE_DEPTH];
  bool                available[CONFIG_BK7258_AUD_QUEUE_DEPTH];
  struct ap_buffer_s  *current;
  size_t              current_bytes;
  uint64_t            submitted_frames;
  media_event_callback callback;
  void               *callback_cookie;
  char                mq_name[BK7258_AGENT_PLAYER_MQ_NAME_LEN];
};

static mutex_t g_bk7258_agent_player_lock = NXMUTEX_INITIALIZER;
static struct bk7258_agent_player_s *g_bk7258_agent_player;
static uint32_t g_bk7258_agent_player_sequence;
static int g_bk7258_agent_player_volume =
  BK7258_AGENT_PLAYER_VOLUME_DEFAULT;

static int bk7258_agent_player_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static void bk7258_agent_player_first_error(int *first, int ret)
{
  if (*first == 0 && ret < 0)
    {
      *first = ret;
    }
}

static bool bk7258_agent_player_resources_released(
    const struct bk7258_agent_player_s *player)
{
  return !player->reserved && !player->mq_registered &&
         !player->mq_created && player->mq == (mqd_t)-1 &&
         player->buffer_count == 0;
}

static int bk7258_agent_player_ioctl(struct bk7258_agent_player_s *player,
                                     int cmd, unsigned long arg)
{
  int ret = ioctl(player->fd, cmd, arg);
  return ret < 0 ? bk7258_agent_player_errno() : ret;
}

static int bk7258_agent_player_parse_options(const char *options,
                                              uint8_t *channels)
{
  char copy[128];
  char *save = NULL;
  char *token;
  bool format_seen = false;
  bool rate_seen = false;
  bool layout_seen = false;

  if (options == NULL || channels == NULL || strlen(options) >= sizeof(copy))
    {
      return -EINVAL;
    }

  strcpy(copy, options);
  token = strtok_r(copy, ":", &save);
  while (token != NULL)
    {
      if (strcmp(token, "format=s16le") == 0)
        {
          format_seen = true;
        }
      else if (strncmp(token, "sample_rate=", 12) == 0)
        {
          char *end;
          unsigned long rate;

          errno = 0;
          rate = strtoul(token + 12, &end, 10);
          if (errno != 0 || end == token + 12 || *end != '\0' ||
              rate != BK7258_AGENT_PLAYER_RATE)
            {
              return -EINVAL;
            }

          rate_seen = true;
        }
      else if (strcmp(token, "ch_layout=mono") == 0)
        {
          *channels = 1;
          layout_seen = true;
        }
      else if (strcmp(token, "ch_layout=stereo") == 0)
        {
          *channels = 2;
          layout_seen = true;
        }
      else
        {
          return -EINVAL;
        }

      token = strtok_r(NULL, ":", &save);
    }

  return format_seen && rate_seen && layout_seen ? 0 : -EINVAL;
}

static void bk7258_agent_player_notify(struct bk7258_agent_player_s *player,
                                        int event, int result)
{
  media_event_callback callback;
  void *cookie;

  nxmutex_lock(&player->lock);
  callback = player->callback;
  cookie = player->callback_cookie;
  nxmutex_unlock(&player->lock);

  if (callback != NULL)
    {
      callback(cookie, event, result, NULL);
    }
}

static int bk7258_agent_player_set_volume_locked(
    struct bk7258_agent_player_s *player, int volume)
{
  struct audio_caps_desc_s caps;

  memset(&caps, 0, sizeof(caps));
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = AUDIO_TYPE_FEATURE;
  caps.caps.ac_format.hw = AUDIO_FU_VOLUME;
  caps.caps.ac_controls.hw[0] =
    (uint16_t)((volume * AUDIO_VOLUME_MAX +
                BK7258_AGENT_PLAYER_VOLUME_MAX / 2) /
               BK7258_AGENT_PLAYER_VOLUME_MAX);
  return bk7258_agent_player_ioctl(player, AUDIOIOC_CONFIGURE,
                                   (unsigned long)(uintptr_t)&caps);
}

static int bk7258_agent_player_enqueue_locked(
    struct bk7258_agent_player_s *player, struct ap_buffer_s *apb,
    size_t nbytes, bool final)
{
  struct audio_buf_desc_s desc;
  unsigned int attempt;
  int ret = -EAGAIN;

  if (apb == NULL || nbytes == 0 || nbytes > apb->nmaxbytes)
    {
      return -EINVAL;
    }

  if (final)
    {
      apb->flags |= AUDIO_APB_FINAL;
    }
  else
    {
      apb->flags &= ~AUDIO_APB_FINAL;
    }
  apb->nbytes = nbytes;
  apb->curbyte = 0;
  memset(&desc, 0, sizeof(desc));
  desc.numbytes = apb->nmaxbytes;
  desc.u.buffer = apb;

  for (attempt = 0; attempt < 3 && ret == -EAGAIN; attempt++)
    {
      ret = bk7258_agent_player_ioctl(
        player, AUDIOIOC_ENQUEUEBUFFER,
        (unsigned long)(uintptr_t)&desc);
      if (ret == -EAGAIN)
        {
          usleep(BK7258_AGENT_PLAYER_RETRY_US);
        }
    }

  if (ret == 0)
    {
      player->outstanding++;
    }

  return ret;
}

static struct ap_buffer_s *
bk7258_agent_player_take_free_locked(struct bk7258_agent_player_s *player)
{
  size_t index;

  for (index = 0; index < player->buffer_count; index++)
    {
      if (player->available[index])
        {
          player->available[index] = false;
          return player->buffers[index];
        }
    }

  return NULL;
}

static int bk7258_agent_player_receive(
    struct bk7258_agent_player_s *player)
{
  struct audio_msg_s msg;
  ssize_t received;
  size_t index;

  for (;;)
    {
      received = mq_receive(player->mq, (char *)&msg, sizeof(msg), NULL);
      if (received < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          if (errno == EAGAIN)
            {
              bool stopping;

              nxmutex_lock(&player->lock);
              stopping = player->stopping || !player->prepared;
              nxmutex_unlock(&player->lock);
              if (stopping)
                {
                  return -EPIPE;
                }

              usleep(BK7258_AGENT_PLAYER_RETRY_US);
              continue;
            }

          return bk7258_agent_player_errno();
        }

      if (received != sizeof(msg))
        {
          continue;
        }

      if (msg.msg_id == AUDIO_MSG_IOERR)
        {
          return -EIO;
        }

      if (msg.msg_id != AUDIO_MSG_DEQUEUE || msg.u.ptr == NULL)
        {
          continue;
        }

      nxmutex_lock(&player->lock);
      for (index = 0; index < player->buffer_count; index++)
        {
          if (player->buffers[index] == msg.u.ptr)
            {
              player->available[index] = true;
              if (player->outstanding > 0)
                {
                  player->outstanding--;
                }

              nxmutex_unlock(&player->lock);
              return 0;
            }
        }

      nxmutex_unlock(&player->lock);
    }
}

static int bk7258_agent_player_receive_locked(
    struct bk7258_agent_player_s *player, bool *complete)
{
  struct audio_msg_s msg;
  ssize_t received;
  size_t index;

  for (;;)
    {
      received = mq_receive(player->mq, (char *)&msg, sizeof(msg), NULL);
      if (received < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return errno == EAGAIN ? -EAGAIN :
                 bk7258_agent_player_errno();
        }

      if (received != sizeof(msg))
        {
          continue;
        }

      if (msg.msg_id == AUDIO_MSG_IOERR)
        {
          return -EIO;
        }

      if (msg.msg_id == AUDIO_MSG_COMPLETE)
        {
          *complete = true;
          return 0;
        }

      if (msg.msg_id != AUDIO_MSG_DEQUEUE || msg.u.ptr == NULL)
        {
          continue;
        }

      for (index = 0; index < player->buffer_count; index++)
        {
          if (player->buffers[index] == msg.u.ptr)
            {
              player->available[index] = true;
              if (player->outstanding > 0)
                {
                  player->outstanding--;
                }

              return 0;
            }
        }
    }
}

static int bk7258_agent_player_drain_locked(
    struct bk7258_agent_player_s *player)
{
  struct ap_buffer_s *apb;
  bool complete = false;
  unsigned int poll;
  int ret;

  if (!player->hardware_started)
    {
      return 0;
    }

  apb = player->current;
  for (poll = 0; apb == NULL &&
                 poll < BK7258_AGENT_PLAYER_DRAIN_POLLS; poll++)
    {
      apb = bk7258_agent_player_take_free_locked(player);
      if (apb != NULL)
        {
          break;
        }

      ret = bk7258_agent_player_receive_locked(player, &complete);
      if (ret == -EAGAIN)
        {
          usleep(BK7258_AGENT_PLAYER_RETRY_US);
        }
      else if (ret < 0)
        {
          return ret;
        }
    }

  if (apb == NULL)
    {
      return -ETIMEDOUT;
    }

  if (player->current_bytes == 0)
    {
      memset(apb->samp, 0, sizeof(int16_t));
      player->current_bytes = sizeof(int16_t);
    }

  ret = bk7258_agent_player_enqueue_locked(
    player, apb, player->current_bytes, true);
  if (ret < 0)
    {
      return ret;
    }

  player->current = NULL;
  player->current_bytes = 0;

  for (poll = 0; poll < BK7258_AGENT_PLAYER_DRAIN_POLLS; poll++)
    {
      ret = bk7258_agent_player_receive_locked(player, &complete);
      if (ret == -EAGAIN)
        {
          usleep(BK7258_AGENT_PLAYER_RETRY_US);
          continue;
        }

      if (ret < 0)
        {
          return ret;
        }

      if (complete)
        {
          player->hardware_started = false;
          return 0;
        }
    }

  return -ETIMEDOUT;
}

static int bk7258_agent_player_cleanup_locked(
    struct bk7258_agent_player_s *player)
{
  struct audio_buf_desc_s desc;
  bool buffers_left = false;
  int first = 0;
  int ret;
  size_t index;

  player->stopping = true;

  if (player->started && player->hardware_started)
    {
      ret = bk7258_agent_player_drain_locked(player);
      bk7258_agent_player_first_error(&first, ret);
    }

  player->started = false;

  if (player->hardware_started || player->outstanding > 0)
    {
      ret = bk7258_agent_player_ioctl(player, AUDIOIOC_STOP, 0);
      bk7258_agent_player_first_error(&first, ret);
      if (ret == 0)
        {
          player->hardware_started = false;
        }
    }

  if (player->reserved)
    {
      ret = bk7258_agent_player_ioctl(player, AUDIOIOC_RELEASE, 0);
      bk7258_agent_player_first_error(&first, ret);
      if (ret < 0)
        {
          ret = bk7258_agent_player_ioctl(player, AUDIOIOC_SHUTDOWN, 0);
          bk7258_agent_player_first_error(&first, ret);
        }

      if (ret == 0)
        {
          player->reserved = false;
          player->hardware_started = false;
          player->outstanding = 0;
        }
    }

  if (player->mq_registered && !player->reserved)
    {
      ret = bk7258_agent_player_ioctl(player, AUDIOIOC_UNREGISTERMQ,
                                      (unsigned long)player->mq);
      bk7258_agent_player_first_error(&first, ret);
      if (ret == 0)
        {
          player->mq_registered = false;
        }
    }

  if (!player->reserved && !player->mq_registered)
    {
      for (index = 0; index < player->buffer_count; index++)
        {
          if (player->buffers[index] != NULL)
            {
              memset(&desc, 0, sizeof(desc));
              desc.u.buffer = player->buffers[index];
              ret = bk7258_agent_player_ioctl(
                player, AUDIOIOC_FREEBUFFER,
                (unsigned long)(uintptr_t)&desc);
              bk7258_agent_player_first_error(&first, ret);
              if (ret == 0)
                {
                  player->buffers[index] = NULL;
                }
            }

          if (player->buffers[index] != NULL)
            {
              buffers_left = true;
            }
          else
            {
              player->available[index] = false;
            }
        }

      if (!buffers_left)
        {
          player->buffer_count = 0;
        }
    }

  if (player->mq != (mqd_t)-1 && !player->mq_registered)
    {
      ret = mq_close(player->mq);
      bk7258_agent_player_first_error(
        &first, ret < 0 ? bk7258_agent_player_errno() : 0);
      if (ret == 0)
        {
          player->mq = (mqd_t)-1;
        }
    }

  if (player->mq_created && player->mq == (mqd_t)-1 &&
      !player->mq_registered)
    {
      ret = mq_unlink(player->mq_name);
      if (ret < 0 && errno != ENOENT)
        {
          bk7258_agent_player_first_error(
            &first, bk7258_agent_player_errno());
        }
      else
        {
          player->mq_created = false;
        }
    }

  if (bk7258_agent_player_resources_released(player))
    {
      player->prepared = false;
      player->stopping = false;
      player->current = NULL;
      player->current_bytes = 0;
      player->submitted_frames = 0;
    }

  return first;
}

/* Referenced by bk7258_aud.c so this object is extracted from libarch.a even
 * though the ABI symbols only satisfy weak definitions in the Agent. */

__attribute__((used)) void bk7258_agent_media_player_link(void)
{
}

void *media_player_open(const char *stream)
{
  struct bk7258_agent_player_s *player;
  char devpath[64];
  uint32_t sequence;
  int saved_errno;
  int ret;

  if (stream == NULL || strcmp(stream, MEDIA_STREAM_MUSIC) != 0)
    {
      errno = EINVAL;
      return NULL;
    }

  nxmutex_lock(&g_bk7258_agent_player_lock);
  if (g_bk7258_agent_player != NULL)
    {
      nxmutex_unlock(&g_bk7258_agent_player_lock);
      errno = EBUSY;
      return NULL;
    }

  player = calloc(1, sizeof(*player));
  if (player == NULL)
    {
      nxmutex_unlock(&g_bk7258_agent_player_lock);
      errno = ENOMEM;
      return NULL;
    }

  player->fd = -1;
  player->mq = (mqd_t)-1;
  ret = nxmutex_init(&player->lock);
  if (ret < 0)
    {
      free(player);
      nxmutex_unlock(&g_bk7258_agent_player_lock);
      errno = -ret;
      return NULL;
    }

  player->lock_initialized = true;
  snprintf(devpath, sizeof(devpath), "/dev/audio/%s",
           CONFIG_BK7258_AUD_DEVNAME);
  player->fd = open(devpath, O_RDWR | O_CLOEXEC);
  if (player->fd < 0)
    {
      saved_errno = errno > 0 ? errno : EIO;
      nxmutex_destroy(&player->lock);
      free(player);
      nxmutex_unlock(&g_bk7258_agent_player_lock);
      errno = saved_errno;
      return NULL;
    }

  sequence = __atomic_add_fetch(&g_bk7258_agent_player_sequence, 1u,
                                __ATOMIC_RELAXED);
  snprintf(player->mq_name, sizeof(player->mq_name), "/bkplay%lu",
           (unsigned long)sequence);
  g_bk7258_agent_player = player;
  nxmutex_unlock(&g_bk7258_agent_player_lock);
  return player;
}

int media_player_set_event_callback(void *handle, void *event_cookie,
                                    media_event_callback on_event)
{
  struct bk7258_agent_player_s *player = handle;

  if (player == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&player->lock);
  player->callback = on_event;
  player->callback_cookie = event_cookie;
  nxmutex_unlock(&player->lock);
  return 0;
}

int media_player_prepare(void *handle, const char *url, const char *options)
{
  struct bk7258_agent_player_s *player = handle;
  struct audio_caps_desc_s caps;
  struct audio_caps_s query;
  struct ap_buffer_info_s info;
  struct audio_buf_desc_s desc;
  struct mq_attr attr;
  uint8_t channels = 0;
  size_t index;
  int ret;

  if (player == NULL)
    {
      return -EINVAL;
    }

  if (url != NULL)
    {
      return -ENOTSUP;
    }

  ret = bk7258_agent_player_parse_options(options, &channels);
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&player->lock);
  if (player->prepared || player->stopping ||
      !bk7258_agent_player_resources_released(player))
    {
      nxmutex_unlock(&player->lock);
      return -EBUSY;
    }

  ret = bk7258_agent_player_ioctl(player, AUDIOIOC_RESERVE, 0);
  if (ret < 0)
    {
      nxmutex_unlock(&player->lock);
      return ret;
    }

  player->reserved = true;
  memset(&query, 0, sizeof(query));
  query.ac_len = sizeof(query);
  query.ac_type = AUDIO_TYPE_QUERY;
  query.ac_subtype = AUDIO_TYPE_QUERY;
  ret = bk7258_agent_player_ioctl(player, AUDIOIOC_GETCAPS,
                                  (unsigned long)(uintptr_t)&query);
  if (ret < 0 || (query.ac_controls.b[0] & AUDIO_TYPE_OUTPUT) == 0 ||
      (query.ac_format.hw & (1 << (AUDIO_FMT_PCM - 1))) == 0)
    {
      ret = ret < 0 ? ret : -ENOTSUP;
      goto fail;
    }

  memset(&caps, 0, sizeof(caps));
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = AUDIO_TYPE_OUTPUT;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  caps.caps.ac_channels = 1;
  caps.caps.ac_controls.hw[0] = BK7258_AGENT_PLAYER_RATE;
  caps.caps.ac_controls.b[3] = BK7258_AGENT_PLAYER_RATE >> 16;
  caps.caps.ac_controls.b[2] = BK7258_AGENT_PLAYER_BITS;
  ret = bk7258_agent_player_ioctl(player, AUDIOIOC_CONFIGURE,
                                  (unsigned long)(uintptr_t)&caps);
  if (ret < 0)
    {
      goto fail;
    }

  ret = bk7258_agent_player_set_volume_locked(
    player, __atomic_load_n(&g_bk7258_agent_player_volume,
                            __ATOMIC_RELAXED));
  if (ret < 0)
    {
      goto fail;
    }

  memset(&info, 0, sizeof(info));
  ret = bk7258_agent_player_ioctl(player, AUDIOIOC_GETBUFFERINFO,
                                  (unsigned long)(uintptr_t)&info);
  if (ret < 0 || info.buffer_size == 0 ||
      info.nbuffers < BK7258_AGENT_PLAYER_PRIME_BUFFERS ||
      info.nbuffers > CONFIG_BK7258_AUD_QUEUE_DEPTH ||
      (info.buffer_size % sizeof(int16_t)) != 0)
    {
      ret = ret < 0 ? ret : -EPROTO;
      goto fail;
    }

  player->buffer_count = info.nbuffers;
  memset(&attr, 0, sizeof(attr));
  attr.mq_maxmsg = player->buffer_count + 4;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  (void)mq_unlink(player->mq_name);
  player->mq = mq_open(player->mq_name,
                       O_RDWR | O_CREAT | O_NONBLOCK, 0644, &attr);
  if (player->mq == (mqd_t)-1)
    {
      ret = bk7258_agent_player_errno();
      goto fail;
    }

  player->mq_created = true;

  ret = bk7258_agent_player_ioctl(player, AUDIOIOC_REGISTERMQ,
                                  (unsigned long)player->mq);
  if (ret < 0)
    {
      goto fail;
    }

  player->mq_registered = true;
  for (index = 0; index < player->buffer_count; index++)
    {
      memset(&desc, 0, sizeof(desc));
      desc.numbytes = info.buffer_size;
      desc.u.pbuffer = &player->buffers[index];
      ret = bk7258_agent_player_ioctl(
        player, AUDIOIOC_ALLOCBUFFER,
        (unsigned long)(uintptr_t)&desc);
      if (ret < 0 || player->buffers[index] == NULL)
        {
          ret = ret < 0 ? ret : -ENOMEM;
          goto fail;
        }

      player->available[index] = true;
    }

  player->input_channels = channels;
  player->input_frame_bytes = channels * sizeof(int16_t);
  player->prepared = true;
  player->started = false;
  player->hardware_started = false;
  player->stopping = false;
  player->submitted_frames = 0;
  nxmutex_unlock(&player->lock);
  bk7258_agent_player_notify(player, MEDIA_EVENT_PREPARED, 0);
  return 0;

fail:
  (void)bk7258_agent_player_cleanup_locked(player);
  nxmutex_unlock(&player->lock);
  return ret;
}

int media_player_start(void *handle)
{
  struct bk7258_agent_player_s *player = handle;
  struct ap_buffer_s *apb;
  unsigned int index;
  int ret;

  if (player == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&player->lock);
  if (!player->prepared || player->stopping)
    {
      nxmutex_unlock(&player->lock);
      return -EINVAL;
    }

  if (player->started)
    {
      nxmutex_unlock(&player->lock);
      return 0;
    }

  for (index = 0; index < BK7258_AGENT_PLAYER_PRIME_BUFFERS; index++)
    {
      apb = bk7258_agent_player_take_free_locked(player);
      if (apb == NULL)
        {
          ret = -ENOBUFS;
          goto fail;
        }

      memset(apb->samp, 0, apb->nmaxbytes);
      ret = bk7258_agent_player_enqueue_locked(player, apb,
                                               apb->nmaxbytes, false);
      if (ret < 0)
        {
          goto fail;
        }
    }

  ret = bk7258_agent_player_ioctl(player, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      goto fail;
    }

  player->hardware_started = true;
  player->started = true;
  nxmutex_unlock(&player->lock);
  bk7258_agent_player_notify(player, MEDIA_EVENT_STARTED, 0);
  return 0;

fail:
  player->stopping = true;
  nxmutex_unlock(&player->lock);
  return ret;
}

ssize_t media_player_write_data(void *handle, const void *data, size_t len)
{
  struct bk7258_agent_player_s *player = handle;
  const int16_t *input = data;
  size_t consumed = 0;

  if (player == NULL || data == NULL || len == 0)
    {
      return -EINVAL;
    }

  nxmutex_lock(&player->lock);
  if (!player->prepared || !player->started || player->stopping ||
      player->input_frame_bytes == 0 ||
      (len % player->input_frame_bytes) != 0)
    {
      nxmutex_unlock(&player->lock);
      return -EINVAL;
    }

  nxmutex_unlock(&player->lock);

  while (consumed < len)
    {
      struct ap_buffer_s *apb;
      size_t free_frames;
      size_t requested_frames;
      size_t copy_frames;
      size_t index;
      int ret;

      nxmutex_lock(&player->lock);
      if (!player->prepared || !player->started || player->stopping)
        {
          nxmutex_unlock(&player->lock);
          return consumed > 0 ? (ssize_t)consumed : -EPIPE;
        }

      if (player->current == NULL)
        {
          player->current = bk7258_agent_player_take_free_locked(player);
          player->current_bytes = 0;
        }

      apb = player->current;
      if (apb == NULL)
        {
          nxmutex_unlock(&player->lock);
          ret = bk7258_agent_player_receive(player);
          if (ret < 0)
            {
              return consumed > 0 ? (ssize_t)consumed : ret;
            }

          continue;
        }

      free_frames = (apb->nmaxbytes - player->current_bytes) /
                    sizeof(int16_t);
      requested_frames = (len - consumed) / player->input_frame_bytes;
      copy_frames = free_frames < requested_frames ?
                    free_frames : requested_frames;

      if (player->input_channels == 1)
        {
          memcpy(apb->samp + player->current_bytes,
                 (const uint8_t *)data + consumed,
                 copy_frames * sizeof(int16_t));
        }
      else
        {
          int16_t *output =
            (int16_t *)(apb->samp + player->current_bytes);
          const int16_t *stereo =
            input + consumed / sizeof(int16_t);

          for (index = 0; index < copy_frames; index++)
            {
              int32_t mixed = (int32_t)stereo[index * 2] +
                              (int32_t)stereo[index * 2 + 1];
              output[index] = (int16_t)(mixed / 2);
            }
        }

      player->current_bytes += copy_frames * sizeof(int16_t);
      consumed += copy_frames * player->input_frame_bytes;
      player->submitted_frames += copy_frames;

      if (player->current_bytes == apb->nmaxbytes)
        {
          ret = bk7258_agent_player_enqueue_locked(
            player, apb, player->current_bytes, false);
          if (ret < 0)
            {
              player->stopping = true;
              nxmutex_unlock(&player->lock);
              return ret;
            }

          player->current = NULL;
          player->current_bytes = 0;
        }

      nxmutex_unlock(&player->lock);
    }

  return (ssize_t)consumed;
}

int media_player_stop(void *handle)
{
  struct bk7258_agent_player_s *player = handle;
  bool notify;
  int ret;

  if (player == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&player->lock);
  notify = player->prepared || player->started;
  ret = bk7258_agent_player_cleanup_locked(player);
  nxmutex_unlock(&player->lock);

  if (notify)
    {
      bk7258_agent_player_notify(player, MEDIA_EVENT_STOPPED, ret);
    }

  return ret;
}

int media_player_close(void *handle, int pending_stop)
{
  struct bk7258_agent_player_s *player = handle;
  int first;
  int ret;

  (void)pending_stop;

  if (player == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_bk7258_agent_player_lock);
  if (g_bk7258_agent_player != player)
    {
      nxmutex_unlock(&g_bk7258_agent_player_lock);
      return -EINVAL;
    }

  nxmutex_lock(&player->lock);
  first = bk7258_agent_player_cleanup_locked(player);
  if (!bk7258_agent_player_resources_released(player))
    {
      nxmutex_unlock(&player->lock);
      nxmutex_unlock(&g_bk7258_agent_player_lock);
      return first < 0 ? first : -EBUSY;
    }

  if (player->fd >= 0)
    {
      ret = close(player->fd);
      bk7258_agent_player_first_error(
        &first, ret < 0 ? bk7258_agent_player_errno() : 0);
      if (ret < 0)
        {
          nxmutex_unlock(&player->lock);
          nxmutex_unlock(&g_bk7258_agent_player_lock);
          return first;
        }

      player->fd = -1;
    }

  g_bk7258_agent_player = NULL;
  player->callback = NULL;
  player->callback_cookie = NULL;
  nxmutex_unlock(&player->lock);
  nxmutex_destroy(&player->lock);
  free(player);
  nxmutex_unlock(&g_bk7258_agent_player_lock);
  return first;
}

int media_player_pause(void *handle)
{
  return handle == NULL ? -EINVAL : -ENOTSUP;
}

int media_player_seek(void *handle, unsigned int position)
{
  (void)position;
  return handle == NULL ? -EINVAL : -ENOTSUP;
}

int media_player_get_position(void *handle, unsigned int *position)
{
  struct bk7258_agent_player_s *player = handle;

  if (player == NULL || position == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&player->lock);
  *position = (unsigned int)(player->submitted_frames * 1000u /
                             BK7258_AGENT_PLAYER_RATE);
  nxmutex_unlock(&player->lock);
  return 0;
}

int media_player_get_duration(void *handle, unsigned int *duration)
{
  if (handle == NULL || duration == NULL)
    {
      return -EINVAL;
    }

  return -ENOTSUP;
}

int media_policy_set_stream_volume(const char *stream, int volume)
{
  struct bk7258_agent_player_s *player;
  int ret = 0;

  if (stream == NULL || strcmp(stream, MEDIA_STREAM_MUSIC) != 0 ||
      volume < BK7258_AGENT_PLAYER_VOLUME_MIN ||
      volume > BK7258_AGENT_PLAYER_VOLUME_MAX)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_bk7258_agent_player_lock);
  player = g_bk7258_agent_player;
  if (player != NULL)
    {
      nxmutex_lock(&player->lock);
      if (player->reserved)
        {
          ret = bk7258_agent_player_set_volume_locked(player, volume);
        }

      nxmutex_unlock(&player->lock);
    }

  if (ret == 0)
    {
      __atomic_store_n(&g_bk7258_agent_player_volume, volume,
                       __ATOMIC_RELAXED);
    }

  nxmutex_unlock(&g_bk7258_agent_player_lock);
  return ret;
}

#endif /* CONFIG_BK7258_AP_CORE && CONFIG_BK7258_AUD &&
        * CONFIG_EXAMPLES_AI_AGENT_VELA &&
        * CONFIG_BK7258_AGENT_MEDIA_PLAYER && !CONFIG_MEDIA */
