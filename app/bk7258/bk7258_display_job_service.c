/****************************************************************************
 * app/bk7258/bk7258_display_job_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <errno.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>

#include "bk7258_display_job_service.h"
#include "bk7258_media_volume.h"

#ifndef BKDISPLAY_JOB_ROOT
#  define BKDISPLAY_JOB_ROOT "/mnt/sdnand"
#endif

static bool g_job_leased;
static bool g_job_mounted;
static atomic_bool g_job_worker;
static sem_t g_job_wake = SEM_INITIALIZER(0);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bkdisplay_job_volume_release(void *context)
{
  int ret;

  (void)context;
  if (g_job_mounted)
    {
      if (umount(BKDISPLAY_JOB_ROOT) < 0)
        {
          return -errno;
        }

      g_job_mounted = false;
    }

  if (!g_job_leased)
    {
      return 0;
    }

  ret = bk7258_media_volume_release(BK7258_MEDIA_VOLUME_INSTALL);
  if (ret == 0)
    {
      g_job_leased = false;
    }

  return ret;
}

static int bkdisplay_job_volume_acquire(void *context)
{
  int ret;

  (void)context;
  ret = bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_INSTALL);
  if (ret < 0)
    {
      return ret;
    }

  g_job_leased = true;
  if ((mkdir("/mnt", 0777) < 0 && errno != EEXIST) ||
      (mkdir(BKDISPLAY_JOB_ROOT, 0777) < 0 && errno != EEXIST))
    {
      return -errno;
    }

  if (mount(CONFIG_BK7258_DISPLAY_BLOCKDEV, BKDISPLAY_JOB_ROOT,
            "vfat", 0, NULL) < 0)
    {
      return -errno;
    }

  g_job_mounted = true;
  return 0;
}

static const struct bkdisplay_job_ops_s g_job_ops =
{
  bkdisplay_job_volume_acquire,
  bkdisplay_job_volume_release
};

static struct bkdisplay_job_s g_display_job =
{
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .ops = &g_job_ops,
  .root = BKDISPLAY_JOB_ROOT
};

static uint64_t bkdisplay_job_now(void)
{
  struct timespec now;

  return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ?
         (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 : 0;
}

static void bkdisplay_job_notify(void)
{
  int value;

  /* The mailbox carries commands; the semaphore only coalesces wakeups. */

  if (atomic_load(&g_job_worker) &&
      sem_getvalue(&g_job_wake, &value) == 0 && value <= 0)
    {
      (void)sem_post(&g_job_wake);
    }
}

static int bkdisplay_job_worker(int argc, char *argv[])
{
  struct bkdisplay_job_status_s status;
  struct timespec deadline;
  int ret;

  (void)argc;
  (void)argv;
  for (; ; )
    {
      ret = bkdisplay_job_step(&g_display_job, bkdisplay_job_now());
      if (bkdisplay_job_status(&g_display_job, &status) < 0)
        {
          /* Metadata contention only; no device or packet timeout policy. */

          (void)nxsig_usleep(1000);
          continue;
        }

      if (status.state >= BKDISPLAY_JOB_DONE)
        {
          atomic_store(&g_job_worker, false);
          return status.error;
        }

      if (ret != 0) continue;
      deadline.tv_sec = status.deadline_ms / 1000;
      deadline.tv_nsec = (status.deadline_ms % 1000) * 1000000;
      ret = nxsem_clockwait_uninterruptible(&g_job_wake, CLOCK_MONOTONIC,
                                             &deadline);
      if (ret < 0 && ret != -ETIMEDOUT)
        {
          (void)bkdisplay_job_gate(&g_display_job, false);
        }
    }
}

int bk7258_display_job_begin(uint64_t binding, size_t size,
                             uint64_t deadline_ms, uint64_t *id)
{
  bool expected = false;
  uint64_t now = bkdisplay_job_now();
  pid_t pid;
  int ret;

  if ((uint64_t)(time_t)(deadline_ms / 1000) != deadline_ms / 1000)
    {
      return -ERANGE;
    }

  if (!atomic_compare_exchange_strong(&g_job_worker, &expected, true))
    {
      return -EBUSY;
    }

  ret = bkdisplay_job_begin(&g_display_job, binding, size, now,
                            deadline_ms, id);
  if (ret < 0)
    {
      atomic_store(&g_job_worker, false);
      return ret;
    }

  pid = task_create("bkpack", CONFIG_BK7258_DISPLAY_SERVICE_PRIORITY,
                    CONFIG_BK7258_DISPLAY_SERVICE_STACKSIZE,
                    bkdisplay_job_worker, NULL);
  if (pid < 0)
    {
      ret = -errno;
      bkdisplay_job_start_failed(&g_display_job, *id, ret);
      atomic_store(&g_job_worker, false);
      return ret;
    }

  return 0;
}

int bk7258_display_job_append(uint64_t binding, uint64_t id, size_t offset,
                              const void *data, size_t size)
{
  int ret = bkdisplay_job_append(&g_display_job, binding, id,
                                 offset, data, size);
  if (ret == 0)
    {
      bkdisplay_job_notify();
    }

  return ret;
}

int bk7258_display_job_finish(uint64_t binding, uint64_t id)
{
  int ret = bkdisplay_job_finish(&g_display_job, binding, id);
  if (ret == 0)
    {
      bkdisplay_job_notify();
    }

  return ret;
}

int bk7258_display_job_cancel(uint64_t binding, uint64_t id)
{
  int ret = bkdisplay_job_cancel(&g_display_job, binding, id);
  if (ret == 0)
    {
      bkdisplay_job_notify();
    }

  return ret;
}

int bk7258_display_job_status(struct bkdisplay_job_status_s *status)
{
  return bkdisplay_job_status(&g_display_job, status);
}

int bk7258_display_job_quiesce(bool stop)
{
  int ret = bkdisplay_job_gate(&g_display_job, !stop);

  if (ret < 0)
    {
      return ret;
    }

  if (stop)
    {
      bkdisplay_job_notify();
      ret = bkdisplay_job_quiesced(&g_display_job);
      return ret == 0 && atomic_load(&g_job_worker) ? -EAGAIN : ret;
    }

  return 0;
}
