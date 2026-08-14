/****************************************************************************
 * board/bk7258/chip/ap/bk7258_mic_validation.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded real-board validation for the public BK7258 NuttX audio ABI.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_MIC_LIFECYCLE_VALIDATION

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/audio/audio.h>
#include <nuttx/kthread.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_mic.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BMICVAL_DEVPATH "/dev/audio/" CONFIG_BK7258_MIC_DEVNAME
#define BMICVAL_MQNAME                   "bmicval"
#define BMICVAL_CYCLES                   10u
#define BMICVAL_BUFFERS                  2u
#define BMICVAL_CAPTURE_BUFFERS          4u
#define BMICVAL_DELAY_US                 1000000u
#define BMICVAL_PAUSE_US                 50000u
#define BMICVAL_TIMEOUT_SEC              3
#define BMICVAL_STACKSIZE                4096
#define BMICVAL_MAGIC                    0x56434d42u /* "BMCV" */
#define BMICVAL_VERSION                  1u
#define BMICVAL_RUNNING                  1u
#define BMICVAL_PASSED                   2u
#define BMICVAL_FAILED                   3u

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum bmicval_stage_e
{
  BMICVAL_STAGE_INIT = 1,
  BMICVAL_STAGE_OPEN,
  BMICVAL_STAGE_RESERVE,
  BMICVAL_STAGE_CONFIGURE,
  BMICVAL_STAGE_BUFFER_INFO,
  BMICVAL_STAGE_MESSAGE_QUEUE,
  BMICVAL_STAGE_ALLOCATE,
  BMICVAL_STAGE_ENQUEUE,
  BMICVAL_STAGE_START,
  BMICVAL_STAGE_RECEIVE,
  BMICVAL_STAGE_PAUSE,
  BMICVAL_STAGE_RESUME,
  BMICVAL_STAGE_STOP,
  BMICVAL_STAGE_SIGNAL
};

struct bmicval_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  int32_t  result;
  uint32_t stage;
  uint32_t channels;
  uint32_t cycles;
  uint32_t buffers;
  uint32_t samples;
  int32_t  left_min;
  int32_t  left_max;
  int32_t  right_min;
  int32_t  right_max;
  uint32_t different;
  uint64_t left_energy;
  uint64_t right_energy;
};

struct bmicval_stats_s
{
  uint32_t samples;
  uint32_t different;
  int32_t  left_min;
  int32_t  left_max;
  int32_t  right_min;
  int32_t  right_max;
  uint64_t left_energy;
  uint64_t right_energy;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Intentionally non-static: J-Link can inspect the final result even when
 * the existing RPMsg syslog transport is unavailable.
 */

volatile struct bmicval_diag_s g_bk7258_mic_validation_diag;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bmicval_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static void bmicval_accumulate(struct bmicval_stats_s *stats,
                               const struct ap_buffer_s *apb,
                               uint8_t channels)
{
  const int16_t *samples = (const int16_t *)apb->samp;
  uint32_t frames = apb->nbytes /
                    (BK7258_MIC_BYTES_PER_SAMPLE * channels);
  uint32_t frame;

  for (frame = 0; frame < frames; frame++)
    {
      int32_t left = samples[frame * channels];
      int32_t left_abs = left < 0 ? -left : left;

      if (left < stats->left_min)
        {
          stats->left_min = left;
        }

      if (left > stats->left_max)
        {
          stats->left_max = left;
        }

      stats->left_energy += (uint32_t)left_abs;

      if (channels == 2)
        {
          int32_t right = samples[frame * channels + 1];
          int32_t right_abs = right < 0 ? -right : right;

          if (right < stats->right_min)
            {
              stats->right_min = right;
            }

          if (right > stats->right_max)
            {
              stats->right_max = right;
            }

          if (left != right)
            {
              stats->different++;
            }

          stats->right_energy += (uint32_t)right_abs;
        }

      stats->samples++;
    }
}

static void bmicval_publish(const struct bmicval_stats_s *stats)
{
  g_bk7258_mic_validation_diag.samples = stats->samples;
  g_bk7258_mic_validation_diag.left_min = stats->left_min;
  g_bk7258_mic_validation_diag.left_max = stats->left_max;
  g_bk7258_mic_validation_diag.right_min = stats->right_min;
  g_bk7258_mic_validation_diag.right_max = stats->right_max;
  g_bk7258_mic_validation_diag.different = stats->different;
  g_bk7258_mic_validation_diag.left_energy = stats->left_energy;
  g_bk7258_mic_validation_diag.right_energy = stats->right_energy;
}

static int bmicval_cycle(struct bmicval_stats_s *stats, uint32_t cycle)
{
  struct ap_buffer_s *buffers[BMICVAL_BUFFERS] = {NULL};
  struct audio_caps_desc_s caps;
  struct ap_buffer_info_s info;
  struct audio_buf_desc_s desc;
  struct audio_msg_s msg;
  struct mq_attr attr;
  struct timespec deadline;
  uint32_t completed = 0;
  uint32_t index;
  uint8_t channels = BK7258_BOARD_MIC_CHANNELS;
  mqd_t mq = (mqd_t)-1;
  bool mq_registered = false;
  bool reserved = false;
  bool started = false;
  int fd = -1;
  int ret = OK;
  int cleanup_ret;

  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_OPEN;
  fd = open(BMICVAL_DEVPATH, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      ret = bmicval_errno();
      goto out;
    }

  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_RESERVE;
  if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      ret = bmicval_errno();
      goto out;
    }

  reserved = true;

  memset(&caps, 0, sizeof(caps));
  caps.caps.ac_len = sizeof(struct audio_caps_s);
  caps.caps.ac_type = AUDIO_TYPE_INPUT;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  caps.caps.ac_channels = channels;
  caps.caps.ac_controls.hw[0] = CONFIG_BK7258_MIC_SAMPLE_RATE;
  caps.caps.ac_controls.b[3] = CONFIG_BK7258_MIC_SAMPLE_RATE >> 16;
  caps.caps.ac_controls.b[2] = BK7258_MIC_BITS_PER_SAMPLE;

  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_CONFIGURE;
  if (ioctl(fd, AUDIOIOC_CONFIGURE,
            (unsigned long)(uintptr_t)&caps) < 0)
    {
      ret = bmicval_errno();
      goto out;
    }

  memset(&info, 0, sizeof(info));
  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_BUFFER_INFO;
  if (ioctl(fd, AUDIOIOC_GETBUFFERINFO,
            (unsigned long)(uintptr_t)&info) < 0)
    {
      ret = bmicval_errno();
      goto out;
    }

  if (info.nbuffers < BMICVAL_BUFFERS ||
      info.buffer_size != CONFIG_BK7258_MIC_FRAME_SAMPLES *
                          BK7258_MIC_BYTES_PER_SAMPLE * channels)
    {
      ret = -EPROTO;
      goto out;
    }

  memset(&attr, 0, sizeof(attr));
  attr.mq_maxmsg = BMICVAL_BUFFERS + 4;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  (void)mq_unlink(BMICVAL_MQNAME);

  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_MESSAGE_QUEUE;
  mq = mq_open(BMICVAL_MQNAME, O_RDWR | O_CREAT, 0644, &attr);
  if (mq == (mqd_t)-1)
    {
      ret = bmicval_errno();
      goto out;
    }

  if (ioctl(fd, AUDIOIOC_REGISTERMQ, (unsigned long)mq) < 0)
    {
      ret = bmicval_errno();
      goto out;
    }

  mq_registered = true;
  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_ALLOCATE;

  for (index = 0; index < BMICVAL_BUFFERS; index++)
    {
      int alloc_ret;

      memset(&desc, 0, sizeof(desc));
      desc.numbytes = info.buffer_size;
      desc.u.pbuffer = &buffers[index];
      alloc_ret = ioctl(fd, AUDIOIOC_ALLOCBUFFER,
                        (unsigned long)(uintptr_t)&desc);
      if (alloc_ret < 0)
        {
          ret = bmicval_errno();
          goto out;
        }

      if (buffers[index] == NULL)
        {
          ret = -ENOMEM;
          goto out;
        }
    }

  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_ENQUEUE;
  for (index = 0; index < BMICVAL_BUFFERS; index++)
    {
      memset(&desc, 0, sizeof(desc));
      desc.numbytes = buffers[index]->nmaxbytes;
      desc.u.buffer = buffers[index];
      if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER,
                (unsigned long)(uintptr_t)&desc) < 0)
        {
          ret = bmicval_errno();
          goto out;
        }
    }

  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_START;
  if (ioctl(fd, AUDIOIOC_START, 0) < 0)
    {
      ret = bmicval_errno();
      goto out;
    }

  started = true;
  if (clock_gettime(CLOCK_REALTIME, &deadline) < 0)
    {
      ret = bmicval_errno();
      goto out;
    }

  deadline.tv_sec += BMICVAL_TIMEOUT_SEC;

  while (completed < BMICVAL_CAPTURE_BUFFERS)
    {
      struct ap_buffer_s *apb;
      ssize_t received;

      g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_RECEIVE;
      received = mq_timedreceive(mq, (char *)&msg, sizeof(msg), NULL,
                                 &deadline);
      if (received < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          ret = bmicval_errno();
          goto out;
        }

      if (received != sizeof(msg) || msg.msg_id != AUDIO_MSG_DEQUEUE ||
          msg.u.ptr == NULL)
        {
          continue;
        }

      apb = (struct ap_buffer_s *)msg.u.ptr;
      bmicval_accumulate(stats, apb, channels);
      completed++;
      g_bk7258_mic_validation_diag.buffers++;
      bmicval_publish(stats);

      if (completed == 1)
        {
          g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_PAUSE;
          if (ioctl(fd, AUDIOIOC_PAUSE, 0) < 0)
            {
              ret = bmicval_errno();
              goto out;
            }

          usleep(BMICVAL_PAUSE_US);
          g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_RESUME;
          if (ioctl(fd, AUDIOIOC_RESUME, 0) < 0)
            {
              ret = bmicval_errno();
              goto out;
            }
        }

      if (completed < BMICVAL_CAPTURE_BUFFERS)
        {
          apb->flags &= ~AUDIO_APB_FINAL;
          memset(&desc, 0, sizeof(desc));
          desc.numbytes = apb->nmaxbytes;
          desc.u.buffer = apb;
          g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_ENQUEUE;
          if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER,
                    (unsigned long)(uintptr_t)&desc) < 0)
            {
              ret = bmicval_errno();
              goto out;
            }
        }
    }

  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_STOP;
  if (ioctl(fd, AUDIOIOC_STOP, 0) < 0)
    {
      ret = bmicval_errno();
      goto out;
    }

  started = false;
  g_bk7258_mic_validation_diag.cycles = cycle;

out:
  if (started)
    {
      cleanup_ret = ioctl(fd, AUDIOIOC_STOP, 0);
      if (cleanup_ret < 0 && ret == OK)
        {
          ret = bmicval_errno();
        }
    }

  if (reserved)
    {
      cleanup_ret = ioctl(fd, AUDIOIOC_RELEASE, 0);
      if (cleanup_ret < 0 && ret == OK)
        {
          ret = bmicval_errno();
        }
    }

  if (mq_registered)
    {
      cleanup_ret = ioctl(fd, AUDIOIOC_UNREGISTERMQ, (unsigned long)mq);
      if (cleanup_ret < 0 && ret == OK)
        {
          ret = bmicval_errno();
        }
    }

  if (fd >= 0)
    {
      for (index = 0; index < BMICVAL_BUFFERS; index++)
        {
          if (buffers[index] != NULL)
            {
              memset(&desc, 0, sizeof(desc));
              desc.u.buffer = buffers[index];
              cleanup_ret = ioctl(fd, AUDIOIOC_FREEBUFFER,
                                  (unsigned long)(uintptr_t)&desc);
              if (cleanup_ret < 0 && ret == OK)
                {
                  ret = bmicval_errno();
                }

              buffers[index] = NULL;
            }
        }

      cleanup_ret = close(fd);
      if (cleanup_ret < 0 && ret == OK)
        {
          ret = bmicval_errno();
        }
    }

  if (mq != (mqd_t)-1)
    {
      mq_close(mq);
      (void)mq_unlink(BMICVAL_MQNAME);
    }

  return ret;
}

static int bmicval_thread(int argc, char **argv)
{
  struct bmicval_stats_s stats;
  uint32_t cycle;
  int ret = OK;

  (void)argc;
  (void)argv;

  memset((void *)&g_bk7258_mic_validation_diag, 0,
         sizeof(g_bk7258_mic_validation_diag));
  memset(&stats, 0, sizeof(stats));
  stats.left_min = INT16_MAX;
  stats.left_max = INT16_MIN;
  stats.right_min = INT16_MAX;
  stats.right_max = INT16_MIN;

  g_bk7258_mic_validation_diag.magic = BMICVAL_MAGIC;
  g_bk7258_mic_validation_diag.version = BMICVAL_VERSION;
  g_bk7258_mic_validation_diag.size =
    sizeof(g_bk7258_mic_validation_diag);
  g_bk7258_mic_validation_diag.state = BMICVAL_RUNNING;
  g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_INIT;
  g_bk7258_mic_validation_diag.channels = BK7258_BOARD_MIC_CHANNELS;

  usleep(BMICVAL_DELAY_US);

  for (cycle = 1; cycle <= BMICVAL_CYCLES; cycle++)
    {
      ret = bmicval_cycle(&stats, cycle);
      if (ret < 0)
        {
          break;
        }

      syslog(LOG_INFO, "BMICVAL cycle=%" PRIu32 "/%u PASS\n",
             cycle, BMICVAL_CYCLES);
    }

  bmicval_publish(&stats);
  if (ret == OK)
    {
      g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_SIGNAL;

      if (stats.samples == 0 || stats.left_max <= stats.left_min ||
          stats.left_energy == 0)
        {
          ret = -ENODATA;
        }

      if (ret == OK && BK7258_BOARD_MIC_CHANNELS == 2 &&
          (stats.right_max <= stats.right_min ||
           stats.right_energy == 0 ||
           stats.different < stats.samples / 100))
        {
          ret = -ENODATA;
        }
    }

  g_bk7258_mic_validation_diag.result = ret;
  g_bk7258_mic_validation_diag.state =
    ret == OK ? BMICVAL_PASSED : BMICVAL_FAILED;

  if (ret == OK)
    {
      syslog(LOG_INFO,
             "BMICVAL PASS cycles=%u ch=%u samples=%" PRIu32
             " L=[%" PRId32 ",%" PRId32 "] E=%" PRIu64
             " R=[%" PRId32 ",%" PRId32 "] E=%" PRIu64
             " diff=%" PRIu32 "\n",
             BMICVAL_CYCLES, BK7258_BOARD_MIC_CHANNELS, stats.samples,
             stats.left_min, stats.left_max, stats.left_energy,
             stats.right_min, stats.right_max, stats.right_energy,
             stats.different);
    }
  else
    {
      syslog(LOG_ERR,
             "BMICVAL FAIL stage=%" PRIu32 " cycle=%" PRIu32
             " ret=%d samples=%" PRIu32 "\n",
             g_bk7258_mic_validation_diag.stage,
             g_bk7258_mic_validation_diag.cycles, ret, stats.samples);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_mic_validation_start(void)
{
  int ret;

  ret = kthread_create("bmic-validate", SCHED_PRIORITY_DEFAULT,
                       BMICVAL_STACKSIZE, bmicval_thread, NULL);
  if (ret < 0)
    {
      memset((void *)&g_bk7258_mic_validation_diag, 0,
             sizeof(g_bk7258_mic_validation_diag));
      g_bk7258_mic_validation_diag.magic = BMICVAL_MAGIC;
      g_bk7258_mic_validation_diag.version = BMICVAL_VERSION;
      g_bk7258_mic_validation_diag.size =
        sizeof(g_bk7258_mic_validation_diag);
      g_bk7258_mic_validation_diag.state = BMICVAL_FAILED;
      g_bk7258_mic_validation_diag.result = ret;
      g_bk7258_mic_validation_diag.stage = BMICVAL_STAGE_INIT;
      return ret;
    }

  return OK;
}

#endif /* CONFIG_BK7258_MIC_LIFECYCLE_VALIDATION */
