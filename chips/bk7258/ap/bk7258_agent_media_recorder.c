/****************************************************************************
 * chips/bk7258/ap/bk7258_agent_media_recorder.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small media_recorder ABI bridge for the official Agent.  The media
 * framework is intentionally disabled for this profile; the bridge keeps the
 * Agent's portable backend on the public NuttX audio upper-half ABI.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_BK7258_MIC) && \
    defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && \
    defined(CONFIG_BK7258_AGENT_MEDIA_RECORDER) && !defined(CONFIG_MEDIA)

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <media_recorder.h>
#include <nuttx/audio/audio.h>
#include <nuttx/mutex.h>

#define BK7258_AGENT_AUDIO_MAX_BUFFERS 4u
#define BK7258_AGENT_AUDIO_MQ_NAME_LEN 32u
#define BK7258_AGENT_AUDIO_BITS        16u
#define BK7258_AGENT_AUDIO_RATE        16000u

struct bk7258_agent_audio_s
{
  int                    fd;
  mqd_t                  mq;
  mutex_t                lock;
  bool                   lock_initialized;
  bool                   reserved;
  bool                   mq_registered;
  bool                   mq_created;
  bool                   prepared;
  bool                   started;
  bool                   buffers_queued;
  bool                   stopping;
  bool                   wake_sent;
  uint8_t                channels;
  uint8_t                physical_channels;
  size_t                 frame_bytes;
  size_t                 buffer_count;
  struct ap_buffer_s    *buffers[BK7258_AGENT_AUDIO_MAX_BUFFERS];
  struct ap_buffer_s    *current;
  size_t                 current_frame;
  size_t                 current_frames;
  char                   mq_name[BK7258_AGENT_AUDIO_MQ_NAME_LEN];
};

static uint32_t g_bk7258_agent_audio_sequence;

static int bk7258_agent_audio_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static int bk7258_agent_audio_ioctl(
    struct bk7258_agent_audio_s *rec, int cmd, unsigned long arg)
{
  int ret = ioctl(rec->fd, cmd, arg);
  return ret < 0 ? bk7258_agent_audio_errno() : ret;
}

static void bk7258_agent_audio_first_error(int *first, int ret)
{
  if (*first == 0 && ret < 0)
    {
      *first = ret;
    }
}

static bool bk7258_agent_audio_resources_released(
    const struct bk7258_agent_audio_s *rec)
{
  return !rec->reserved && !rec->mq_registered && !rec->mq_created &&
         rec->mq == (mqd_t)-1 && rec->buffer_count == 0 && rec->fd < 0;
}

static void bk7258_agent_audio_dispose(struct bk7258_agent_audio_s *rec)
{
  if (rec->lock_initialized)
    {
      nxmutex_destroy(&rec->lock);
      rec->lock_initialized = false;
    }

  free(rec);
}

static int bk7258_agent_audio_parse_options(const char *options,
                                            uint8_t *channels)
{
  char copy[128];
  char *save = NULL;
  char *token;
  bool format_seen = false;
  bool rate_seen = false;
  bool layout_seen = false;
  unsigned long rate;

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

          errno = 0;
          rate = strtoul(token + 12, &end, 10);
          if (errno != 0 || end == token + 12 || *end != '\0' ||
              rate != BK7258_AGENT_AUDIO_RATE)
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

static int bk7258_agent_audio_enqueue(struct bk7258_agent_audio_s *rec,
                                      struct ap_buffer_s *apb)
{
  struct audio_buf_desc_s desc;

  if (apb == NULL)
    {
      return -EINVAL;
    }

  apb->flags &= ~AUDIO_APB_FINAL;
  apb->nbytes = 0;
  apb->curbyte = 0;
  memset(&desc, 0, sizeof(desc));
  desc.numbytes = apb->nmaxbytes;
  desc.u.buffer = apb;
  return bk7258_agent_audio_ioctl(rec, AUDIOIOC_ENQUEUEBUFFER,
                                  (unsigned long)(uintptr_t)&desc);
}

static int bk7258_agent_audio_stop_locked(struct bk7258_agent_audio_s *rec,
                                          bool *wake)
{
  bool needs_stop;
  int ret;

  needs_stop = rec->started || rec->buffers_queued;
  rec->started = false;
  rec->stopping = true;
  *wake = rec->mq_registered && !rec->wake_sent;
  rec->wake_sent = true;
  ret = needs_stop ?
        bk7258_agent_audio_ioctl(rec, AUDIOIOC_STOP, 0) : 0;
  if (ret >= 0)
    {
      rec->buffers_queued = false;
    }

  return ret;
}

static int bk7258_agent_audio_cleanup(struct bk7258_agent_audio_s *rec)
{
  struct audio_buf_desc_s desc;
  bool buffers_left = false;
  int first = 0;
  int ret;
  size_t index;

  if (rec->started || rec->buffers_queued)
    {
      bool wake;

      ret = bk7258_agent_audio_stop_locked(rec, &wake);
      bk7258_agent_audio_first_error(&first, ret);
      if (wake)
        {
          struct audio_msg_s msg;

          memset(&msg, 0, sizeof(msg));
          msg.msg_id = AUDIO_MSG_STOP;
          (void)mq_send(rec->mq, (const char *)&msg, sizeof(msg), 0);
        }
    }

  /* RELEASE drains lower-half pending buffers before the queue reference is
   * removed.  This is the same ownership order as the public validation. */

  if (rec->reserved)
    {
      ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_RELEASE, 0);
      bk7258_agent_audio_first_error(&first, ret);
      if (ret < 0)
        {
          ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_SHUTDOWN, 0);
          bk7258_agent_audio_first_error(&first, ret);
        }

      if (ret >= 0)
        {
          rec->reserved = false;
          rec->buffers_queued = false;
        }
    }

  if (rec->mq_registered && !rec->reserved)
    {
      ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_UNREGISTERMQ,
                                     (unsigned long)rec->mq);
      bk7258_agent_audio_first_error(&first, ret);
      if (ret >= 0)
        {
          rec->mq_registered = false;
        }
    }

  if (rec->fd >= 0 && !rec->reserved && !rec->mq_registered)
    {
      for (index = 0; index < rec->buffer_count; index++)
        {
          if (rec->buffers[index] != NULL)
            {
              memset(&desc, 0, sizeof(desc));
              desc.u.buffer = rec->buffers[index];
              ret = bk7258_agent_audio_ioctl(
                  rec, AUDIOIOC_FREEBUFFER,
                  (unsigned long)(uintptr_t)&desc);
              bk7258_agent_audio_first_error(&first, ret);
              /* NuttX AUDIOIOC_FREEBUFFER returns the descriptor size on
               * success when the lower half uses the generic apb_free path.
               * Treat every non-negative ioctl result as success; requiring
               * zero leaves all buffers owned and prevents the fd/mqueue
               * teardown needed by the next PTT session. */

              if (ret >= 0)
                {
                  rec->buffers[index] = NULL;
                }
            }

          if (rec->buffers[index] != NULL)
            {
              buffers_left = true;
            }
        }

      if (!buffers_left)
        {
          rec->buffer_count = 0;
        }
    }

  if (rec->mq != (mqd_t)-1 && !rec->mq_registered &&
      rec->buffer_count == 0)
    {
      ret = mq_close(rec->mq);
      bk7258_agent_audio_first_error(&first, ret < 0 ?
                                     bk7258_agent_audio_errno() : 0);
      if (ret == 0)
        {
          rec->mq = (mqd_t)-1;
        }
    }

  if (rec->mq_created && rec->mq == (mqd_t)-1 &&
      !rec->mq_registered)
    {
      ret = mq_unlink(rec->mq_name);
      if (ret < 0 && errno != ENOENT)
        {
          bk7258_agent_audio_first_error(
            &first, bk7258_agent_audio_errno());
        }
      else
        {
          rec->mq_created = false;
        }
    }

  if (rec->fd >= 0 && !rec->reserved && !rec->mq_registered &&
      !rec->mq_created && rec->mq == (mqd_t)-1 &&
      rec->buffer_count == 0)
    {
      ret = close(rec->fd);
      bk7258_agent_audio_first_error(&first, ret < 0 ?
                                     bk7258_agent_audio_errno() : 0);
      if (ret == 0)
        {
          rec->fd = -1;
        }
    }

  return first;
}

static void bk7258_agent_audio_copy_frames(
    const struct bk7258_agent_audio_s *rec,
    const struct ap_buffer_s *apb, size_t first, size_t frames, void *data)
{
  const int16_t *src = (const int16_t *)apb->samp;
  int16_t *dst = (int16_t *)data;

  if (rec->channels == 1)
    {
      memcpy(dst, src + first, frames * sizeof(int16_t));
    }
  else
    {
      memcpy(dst, src + first * 2,
             frames * 2 * sizeof(int16_t));
    }
}

/* Referenced by bk7258_mic.c so this object is extracted from libarch.a even
 * though the six ABI symbols only satisfy weak definitions in the Agent. */
__attribute__((used)) void bk7258_agent_media_recorder_link(void)
{
}

void *media_recorder_open(const char *params)
{
  struct bk7258_agent_audio_s *rec;
  char devpath[64];
  uint32_t sequence;
  int ret;

  if (params == NULL || strcmp(params, MEDIA_SOURCE_MIC) != 0)
    {
      errno = EINVAL;
      return NULL;
    }

  rec = calloc(1, sizeof(*rec));
  if (rec == NULL)
    {
      errno = ENOMEM;
      return NULL;
    }

  rec->fd = -1;
  rec->mq = (mqd_t)-1;
  ret = nxmutex_init(&rec->lock);
  if (ret < 0)
    {
      errno = -ret;
      free(rec);
      return NULL;
    }

  rec->lock_initialized = true;
  snprintf(devpath, sizeof(devpath), "/dev/audio/%s",
           CONFIG_BK7258_MIC_DEVNAME);
  rec->fd = open(devpath, O_RDWR | O_CLOEXEC);
  if (rec->fd < 0)
    {
      ret = errno > 0 ? errno : EIO;
      (void)bk7258_agent_audio_cleanup(rec);
      if (bk7258_agent_audio_resources_released(rec))
        {
          bk7258_agent_audio_dispose(rec);
        }

      errno = ret;
      return NULL;
    }

  ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_RESERVE, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[bk7258_media_rec] RESERVE failed: %d\n", ret);
      (void)bk7258_agent_audio_cleanup(rec);
      if (bk7258_agent_audio_resources_released(rec))
        {
          bk7258_agent_audio_dispose(rec);
        }

      errno = -ret;
      return NULL;
    }

  rec->reserved = true;
  sequence = __atomic_add_fetch(&g_bk7258_agent_audio_sequence, 1u,
                                __ATOMIC_RELAXED);
  snprintf(rec->mq_name, sizeof(rec->mq_name), "/bkaudio%lu",
           (unsigned long)sequence);
  return rec;
}

int media_recorder_prepare(void *handle, const char *url,
                           const char *options)
{
  struct bk7258_agent_audio_s *rec = handle;
  struct audio_caps_desc_s caps;
  struct audio_caps_s query;
  struct ap_buffer_info_s info;
  struct audio_buf_desc_s desc;
  struct mq_attr attr;
  uint8_t channels = 0;
  size_t index;
  int ret;

  if (rec == NULL || url != NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_agent_audio_parse_options(options, &channels);
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&rec->lock);
  if (rec->prepared || rec->stopping || rec->mq_registered ||
      rec->mq_created || rec->mq != (mqd_t)-1 ||
      rec->buffer_count != 0)
    {
      nxmutex_unlock(&rec->lock);
      return -EBUSY;
    }
  nxmutex_unlock(&rec->lock);

  memset(&query, 0, sizeof(query));
  query.ac_len = sizeof(query);
  query.ac_type = AUDIO_TYPE_QUERY;
  query.ac_subtype = AUDIO_TYPE_QUERY;
  ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_GETCAPS,
                                 (unsigned long)(uintptr_t)&query);
  if (ret < 0 || query.ac_channels < channels)
    {
      ret = ret < 0 ? ret : -ENOTSUP;
      goto fail;
    }

  memset(&caps, 0, sizeof(caps));
  caps.caps.ac_len = sizeof(struct audio_caps_s);
  caps.caps.ac_type = AUDIO_TYPE_INPUT;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  caps.caps.ac_channels = channels;
  caps.caps.ac_controls.hw[0] = BK7258_AGENT_AUDIO_RATE;
  caps.caps.ac_controls.b[3] = BK7258_AGENT_AUDIO_RATE >> 16;
  caps.caps.ac_controls.b[2] = BK7258_AGENT_AUDIO_BITS;
  ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_CONFIGURE,
                                 (unsigned long)(uintptr_t)&caps);
  if (ret < 0)
    {
      goto fail;
    }

  memset(&info, 0, sizeof(info));
  ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_GETBUFFERINFO,
                                 (unsigned long)(uintptr_t)&info);
  if (ret < 0 || info.buffer_size == 0 || info.nbuffers < 2)
    {
      ret = ret < 0 ? ret : -ENOBUFS;
      goto fail;
    }

  rec->buffer_count = info.nbuffers > BK7258_AGENT_AUDIO_MAX_BUFFERS ?
                      BK7258_AGENT_AUDIO_MAX_BUFFERS : info.nbuffers;
  rec->frame_bytes = channels * sizeof(int16_t);
  if (rec->buffer_count < 2 || info.buffer_size < rec->frame_bytes ||
      info.buffer_size % rec->frame_bytes != 0)
    {
      ret = -EPROTO;
      goto fail;
    }

  memset(&attr, 0, sizeof(attr));
  attr.mq_maxmsg = rec->buffer_count + 2;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  (void)mq_unlink(rec->mq_name);
  rec->mq = mq_open(rec->mq_name, O_RDWR | O_CREAT, 0644, &attr);
  if (rec->mq == (mqd_t)-1)
    {
      ret = bk7258_agent_audio_errno();
      goto fail;
    }

  rec->mq_created = true;

  ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_REGISTERMQ,
                                 (unsigned long)rec->mq);
  if (ret < 0)
    {
      goto fail;
    }

  rec->mq_registered = true;
  for (index = 0; index < rec->buffer_count; index++)
    {
      memset(&desc, 0, sizeof(desc));
      desc.numbytes = info.buffer_size;
      desc.u.pbuffer = &rec->buffers[index];
      ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_ALLOCBUFFER,
                                     (unsigned long)(uintptr_t)&desc);
      if (ret < 0 || rec->buffers[index] == NULL)
        {
          ret = ret < 0 ? ret : -ENOMEM;
          goto fail;
        }
    }

  rec->physical_channels = query.ac_channels;
  rec->channels = channels;
  rec->prepared = true;
  rec->stopping = false;
  return 0;

fail:
  /* audio_capture_open() closes the same handle after prepare failure.
   * Keep the object and its partial ownership state alive for that close;
   * freeing it here would turn the required close into a use-after-free.
   */

  return ret;
}

int media_recorder_start(void *handle)
{
  struct bk7258_agent_audio_s *rec = handle;
  size_t index;
  int ret;

  if (rec == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&rec->lock);
  if (!rec->prepared)
    {
      nxmutex_unlock(&rec->lock);
      return -EINVAL;
    }

  if (rec->started)
    {
      nxmutex_unlock(&rec->lock);
      return 0;
    }

  rec->stopping = false;
  rec->wake_sent = false;
  nxmutex_unlock(&rec->lock);

  for (index = 0; index < rec->buffer_count; index++)
    {
      ret = bk7258_agent_audio_enqueue(rec, rec->buffers[index]);
      if (ret < 0)
        {
          int stop_ret;

          syslog(LOG_ERR,
                 "[bk7258_media_rec] ENQUEUE[%zu] failed: %d "
                 "reserved=%d prepared=%d queued=%d\n",
                 index, ret, rec->reserved, rec->prepared,
                 rec->buffers_queued);

          /* media_recorder_stop() both stops queued hardware work and sends
           * the message-queue wake consumed by the recording thread.  Calling
           * stop_locked() here used to set wake_sent without sending that
           * message, leaving audio_capture_abort() stuck in pthread_join(). */

          stop_ret = media_recorder_stop(rec);
          if (stop_ret < 0)
            {
              syslog(LOG_ERR,
                     "[bk7258_media_rec] cleanup STOP failed after "
                     "ENQUEUE: %d\n", stop_ret);
            }

          return ret;
        }

      nxmutex_lock(&rec->lock);
      rec->buffers_queued = true;
      nxmutex_unlock(&rec->lock);
    }

  ret = bk7258_agent_audio_ioctl(rec, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      int stop_ret;

      syslog(LOG_ERR,
             "[bk7258_media_rec] START failed: %d "
             "reserved=%d prepared=%d queued=%d\n",
             ret, rec->reserved, rec->prepared, rec->buffers_queued);
      stop_ret = media_recorder_stop(rec);
      if (stop_ret < 0)
        {
          syslog(LOG_ERR,
                 "[bk7258_media_rec] cleanup STOP failed after START: %d\n",
                 stop_ret);
        }

      return ret;
    }

  nxmutex_lock(&rec->lock);
  rec->started = true;
  nxmutex_unlock(&rec->lock);
  return 0;
}

ssize_t media_recorder_read_data(void *handle, void *data, size_t len)
{
  struct bk7258_agent_audio_s *rec = handle;
  size_t written = 0;

  if (rec == NULL || data == NULL || len == 0)
    {
      return -EINVAL;
    }

  for (;;)
    {
      struct ap_buffer_s *apb = NULL;
      bool requeue = false;
      bool final = false;
      bool complete = false;
      bool stopped = false;
      size_t frames;
      size_t available;
      size_t request;
      int ret;

      nxmutex_lock(&rec->lock);
      if (!rec->prepared)
        {
          nxmutex_unlock(&rec->lock);
          return written > 0 ? (ssize_t)written : -EINVAL;
        }

      if (rec->current != NULL)
        {
          apb = rec->current;
          available = rec->current_frames - rec->current_frame;
          request = (len - written) / rec->frame_bytes;
          frames = available < request ? available : request;
          bk7258_agent_audio_copy_frames(rec, apb, rec->current_frame,
                                         frames, (uint8_t *)data + written);
          rec->current_frame += frames;
          written += frames * rec->frame_bytes;

          if (rec->current_frame == rec->current_frames)
            {
              final = (apb->flags & AUDIO_APB_FINAL) != 0;
              rec->current = NULL;
              rec->current_frame = 0;
              rec->current_frames = 0;
              requeue = !rec->stopping && !final;
              stopped = rec->stopping;
              complete = true;
            }

          nxmutex_unlock(&rec->lock);

          if (complete)
            {
              if (requeue)
                {
                  ret = bk7258_agent_audio_enqueue(rec, apb);
                  if (ret < 0)
                    {
                      return written > 0 ? (ssize_t)written : ret;
                    }
                }

              if (final || stopped)
                {
                  return written > 0 ? (ssize_t)written : -EPIPE;
                }
            }

          if (written >= len)
            {
              return (ssize_t)written;
            }

          if (frames == 0)
            {
              return written > 0 ? (ssize_t)written : -EPROTO;
            }

          continue;
        }

      if (rec->stopping)
        {
          nxmutex_unlock(&rec->lock);
          return written > 0 ? (ssize_t)written : -EPIPE;
        }
      nxmutex_unlock(&rec->lock);

      {
        struct audio_msg_s msg;
        ssize_t received;

        received = mq_receive(rec->mq, (char *)&msg, sizeof(msg), NULL);
        if (received < 0)
          {
            if (errno == EINTR)
              {
                continue;
              }

            return written > 0 ? (ssize_t)written :
                   bk7258_agent_audio_errno();
          }

        if (received != sizeof(msg))
          {
            continue;
          }

        if (msg.msg_id == AUDIO_MSG_STOP ||
            msg.msg_id == AUDIO_MSG_COMPLETE ||
            msg.msg_id == AUDIO_MSG_IOERR)
          {
            nxmutex_lock(&rec->lock);
            rec->stopping = true;
            nxmutex_unlock(&rec->lock);
            return written > 0 ? (ssize_t)written : -EPIPE;
          }

        if (msg.msg_id != AUDIO_MSG_DEQUEUE || msg.u.ptr == NULL)
          {
            continue;
          }

        nxmutex_lock(&rec->lock);
        rec->current = (struct ap_buffer_s *)msg.u.ptr;
        rec->current_frame = 0;
        rec->current_frames = rec->current->nbytes / rec->frame_bytes;
        nxmutex_unlock(&rec->lock);
      }
    }
}

int media_recorder_stop(void *handle)
{
  struct bk7258_agent_audio_s *rec = handle;
  bool wake;
  int ret;

  if (rec == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&rec->lock);
  if (!rec->prepared)
    {
      nxmutex_unlock(&rec->lock);
      return 0;
    }

  ret = bk7258_agent_audio_stop_locked(rec, &wake);
  nxmutex_unlock(&rec->lock);

  if (wake)
    {
      struct audio_msg_s msg;

      memset(&msg, 0, sizeof(msg));
      msg.msg_id = AUDIO_MSG_STOP;
      if (mq_send(rec->mq, (const char *)&msg, sizeof(msg), 0) < 0 &&
          ret == 0)
        {
          ret = bk7258_agent_audio_errno();
        }
    }

  return ret;
}

int media_recorder_close(void *handle)
{
  struct bk7258_agent_audio_s *rec = handle;
  int ret;
  int cleanup_ret;

  if (rec == NULL)
    {
      return -EINVAL;
    }

  ret = media_recorder_stop(rec);
  cleanup_ret = bk7258_agent_audio_cleanup(rec);
  if (!bk7258_agent_audio_resources_released(rec))
    {
      syslog(LOG_ERR,
             "[bk7258_media_rec] close incomplete: stop=%d cleanup=%d "
             "reserved=%d mqreg=%d mqcreated=%d buffers=%zu fd=%d\n",
             ret, cleanup_ret, rec->reserved, rec->mq_registered,
             rec->mq_created, rec->buffer_count, rec->fd);
      return ret < 0 ? ret : (cleanup_ret < 0 ? cleanup_ret : -EBUSY);
    }

  bk7258_agent_audio_dispose(rec);
  return ret < 0 ? ret : cleanup_ret;
}

#endif /* CONFIG_BK7258_AP_CORE && CONFIG_BK7258_MIC &&
        * CONFIG_EXAMPLES_AI_AGENT_VELA &&
        * CONFIG_BK7258_AGENT_MEDIA_RECORDER && !CONFIG_MEDIA */
