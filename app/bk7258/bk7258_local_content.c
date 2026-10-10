/****************************************************************************
 * app/bk7258/bk7258_local_content.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include "bk7258_local_content.h"
#include "voice/audio_playback.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <nuttx/spinlock.h>
#include <mbedtls/sha256.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef BKCONTENT_PATH
#define BKCONTENT_PATH CONFIG_MEDIA_SERVER_CONFIG_PATH "/local_rhythm.pcm"
#endif

#define BKCONTENT_BYTES 128000u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static spinlock_t g_lock = SP_UNLOCKED;
static struct bkcontent_status_s g_status;
static bool g_admitted;
static bool g_cancel;
static bool g_stop_sent;
static uint64_t g_created;
static audio_playback_t *g_player;
static unsigned int g_stoppers;
static int g_cleanup_error;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t content_now(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool content_busy_locked(void)
{
  return g_status.phase >= BKCONTENT_PENDING &&
         g_status.phase <= BKCONTENT_PLAYING;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool bkcontent_busy(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  bool busy = content_busy_locked();
  spin_unlock_irqrestore(&g_lock, flags);
  return busy;
}

void bkcontent_status(struct bkcontent_status_s *status)
{
  if (status != NULL)
    {
      irqstate_t flags = spin_lock_irqsave(&g_lock);
      *status = g_status;
      spin_unlock_irqrestore(&g_lock, flags);
    }
}

int bkcontent_submit(unsigned int action, uint64_t content, uint32_t *id)
{
  irqstate_t flags;
  int ret = 0;

  if (id == NULL || (action != 5 && action != 6) ||
      (action == 5 ? content != 1 : content != 0)) return -EINVAL;
  *id = 0;
  flags = spin_lock_irqsave(&g_lock);
  if (!g_admitted) ret = -ESHUTDOWN;
  else if (g_cleanup_error) ret = g_cleanup_error;
  else if (action == 6)
    {
      if (!content_busy_locked()) ret = -EALREADY;
      else
        {
          g_cancel = true;
          if (g_status.phase != BKCONTENT_PLAYING)
            {
              g_status.phase = BKCONTENT_CANCELED;
              g_status.error = -ECANCELED;
            }
        }
    }
  else if (content_busy_locked()) ret = -EBUSY;
  else if (g_status.id == UINT32_MAX) ret = -EOVERFLOW;
  else
    {
      g_status.id++;
      g_status.content = content;
      g_status.phase = BKCONTENT_PENDING;
      g_status.error = 0;
      g_cancel = false;
      g_stop_sent = false;
      g_created = content_now();
    }

  if (!ret) *id = g_status.id;
  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

int bkcontent_ready(uint32_t id)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  int ret = -ESTALE;
  if (g_admitted && !g_cancel && id == g_status.id &&
      g_status.phase == BKCONTENT_PENDING)
    {
      g_status.phase = BKCONTENT_READY;
      ret = 0;
    }

  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

int bkcontent_cancel(uint32_t id)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  int ret = -ESTALE;
  if (id && id == g_status.id && content_busy_locked())
    {
      g_cancel = true;
      if (g_status.phase != BKCONTENT_PLAYING)
        {
          g_status.phase = BKCONTENT_CANCELED;
          g_status.error = -ECANCELED;
        }

      ret = 0;
    }

  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

/* A short reference protects the handle while stop performs Media I/O with
 * no metadata lock held. The sole writer unpublishes before close.
 */

void bkcontent_step(bool admitted)
{
  audio_playback_t *stop = NULL;
  uint64_t now = content_now();
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  g_admitted = admitted;
  if (content_busy_locked() &&
      (!admitted || now < g_created || now - g_created >
       (g_status.phase == BKCONTENT_PLAYING ? 8000u : 2000u)))
    {
      g_cancel = true;
      if (g_status.phase != BKCONTENT_PLAYING)
        {
          g_status.phase = BKCONTENT_CANCELED;
          g_status.error = -ECANCELED;
        }
    }

  if (g_cancel && g_player != NULL && !g_stop_sent)
    {
      stop = g_player;
      g_stoppers++;
      g_stop_sent = true;
    }

  spin_unlock_irqrestore(&g_lock, flags);
  if (stop != NULL)
    {
      audio_playback_stop(stop);
      flags = spin_lock_irqsave(&g_lock);
      g_stoppers--;
      spin_unlock_irqrestore(&g_lock, flags);
    }
}

static bool content_canceled(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  bool canceled = g_cancel || !g_admitted;
  spin_unlock_irqrestore(&g_lock, flags);
  return canceled;
}

int bkcontent_quiesce(void)
{
  bkcontent_step(false);
  if (bkcontent_busy()) return -EAGAIN;
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  int error = g_cleanup_error;
  spin_unlock_irqrestore(&g_lock, flags);
  if (error)
    {
      error = audio_playback_cleanup(100);
      flags = spin_lock_irqsave(&g_lock);
      g_cleanup_error = error;
      spin_unlock_irqrestore(&g_lock, flags);
    }

  return error;
}

static int content_check(int fd, uint8_t *buffer, size_t capacity)
{
  static const uint8_t expected[32] =
  {
    0xc1, 0x34, 0x0e, 0xf7, 0x2d, 0xfb, 0xe5, 0x6d,
    0x6f, 0x17, 0xb5, 0x8b, 0x3e, 0x4b, 0xa0, 0xf2,
    0x6f, 0x63, 0xd2, 0x6a, 0xd3, 0x97, 0x8f, 0x18,
    0x28, 0x5d, 0xfc, 0xfd, 0x96, 0xfa, 0x2b, 0xc4
  };

  mbedtls_sha256_context hash;
  struct stat info;
  uint8_t digest[32];
  size_t total = 0;
  int ret;

  if (fstat(fd, &info) < 0) return -errno;
  if (!S_ISREG(info.st_mode) || info.st_size != BKCONTENT_BYTES)
    {
      return -EBADMSG;
    }

  mbedtls_sha256_init(&hash);
  ret = mbedtls_sha256_starts(&hash, 0);
  while (!ret && total < BKCONTENT_BYTES)
    {
      if (content_canceled())
        {
          ret = -ECANCELED;
          break;
        }

      ssize_t size = read(fd, buffer, capacity);
      if (size < 0 && errno == EINTR) continue;
      if (size <= 0)
        {
          ret = size < 0 ? -errno : -EBADMSG;
          break;
        }

      total += size;
      ret = mbedtls_sha256_update(&hash, buffer, size);
    }

  if (!ret) ret = mbedtls_sha256_finish(&hash, digest);
  mbedtls_sha256_free(&hash);
  if (!ret && (total != BKCONTENT_BYTES || memcmp(digest, expected, 32)))
    ret = -EBADMSG;
  if (!ret && lseek(fd, 0, SEEK_SET) < 0) ret = -errno;
  return ret;
}

void bkcontent_work(void)
{
  uint8_t buffer[640];
  audio_playback_t *player = NULL;
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  if (g_status.phase != BKCONTENT_READY || g_cancel || !g_admitted)
    {
      spin_unlock_irqrestore(&g_lock, flags);
      return;
    }

  g_status.phase = BKCONTENT_PLAYING;
  spin_unlock_irqrestore(&g_lock, flags);

  /* The read-only ROMFS asset has no SD/MSC lease and cannot select a path.
   * Validate the entire bounded asset before any audible side effect.
   */

  int fd = open(BKCONTENT_PATH, O_RDONLY);
  int ret = fd < 0 ? -errno : content_check(fd, buffer, sizeof(buffer));
  if (!ret && content_canceled()) ret = -ECANCELED;
  if (!ret)
    {
      player = audio_playback_open(NULL, 16000, 1, 16);
      if (player == NULL) ret = -(errno ? errno : EIO);
      flags = spin_lock_irqsave(&g_lock);
      g_player = player;
      spin_unlock_irqrestore(&g_lock, flags);
    }

  size_t total = 0;
  while (!ret && total < BKCONTENT_BYTES)
    {
      if (content_canceled())
        {
          ret = -ECANCELED;
          break;
        }

      ssize_t size = read(fd, buffer, sizeof(buffer));
      if (size < 0 && errno == EINTR) continue;
      if (size <= 0 || (size & 1))
        {
          ret = size < 0 ? -errno : -EBADMSG;
          break;
        }

      int written = audio_playback_write(player, buffer, size);
      ret = written < 0 ? written : written != size ? -EIO : 0;
      total += size;
    }

  if (!ret && !content_canceled()) ret = audio_playback_drain(player, 2000);
  if (fd >= 0 && close(fd) < 0 && !ret) ret = -errno;
  flags = spin_lock_irqsave(&g_lock);
  g_player = NULL;
  spin_unlock_irqrestore(&g_lock, flags);
  for (; ; )
    {
      flags = spin_lock_irqsave(&g_lock);
      unsigned int readers = g_stoppers;
      spin_unlock_irqrestore(&g_lock, flags);
      if (!readers) break;
      usleep(1000);
    }

  if (player != NULL)
    {
      int closed = audio_playback_close(player);
      if (closed < 0) ret = closed;
      flags = spin_lock_irqsave(&g_lock);
      g_cleanup_error = closed < 0 ? closed : 0;
      spin_unlock_irqrestore(&g_lock, flags);
    }

  flags = spin_lock_irqsave(&g_lock);
  if (g_cancel && !ret) ret = -ECANCELED;
  g_status.error = ret;
  g_status.phase = ret == -ECANCELED ? BKCONTENT_CANCELED :
                   ret ? BKCONTENT_FAILED : BKCONTENT_DONE;
  spin_unlock_irqrestore(&g_lock, flags);
}
