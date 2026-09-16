/****************************************************************************
 * app/bk7258/bk7258_voice_button.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * A GPIO lower-half is the only owner of pin policy.  This module only polls
 * the public GPIOC_READ ABI and publishes a tiny, one-way RPMsg lease.
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_voice_button.h"
#ifdef CONFIG_BK7258_PRODUCT_KEYS
#include "bk7258_product_keys.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/clock.h>
#ifdef CONFIG_BK7258_PRODUCT_KEYS
#  include <nuttx/input/buttons.h>
#else
#  include <sys/ioctl.h>
#  include <nuttx/ioexpander/gpio.h>
#endif
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#define BKVOICE_BUTTON_POLL_MS     10u
#define BKVOICE_BUTTON_DEBOUNCE_MS 30u
#define BKVOICE_BUTTON_STOP_MS     1000u
#define BKVOICE_BUTTON_PRIORITY    100
#define BKVOICE_BUTTON_STACKSIZE   2048

struct bkvoice_button_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t lifecycle_lock;
  mutex_t endpoint_lock;
  sem_t worker_done;
  sem_t worker_ready;
  char devpath[PATH_MAX];
  volatile bool started;
  volatile bool stop_requested;
  volatile bool endpoint_created;
  bool worker_owned;
  bool callback_registered;
  bool active_low;
  bool stable_known;
  uint32_t stable_pressed;
  volatile int worker_result;
  volatile uint32_t arm_epoch;
  volatile bool armed;
  volatile bool release_since_disconnect;
  uint32_t sequence;
};

static struct bkvoice_button_s g_bkvoice_button =
{
  .lifecycle_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
};

static int bkvoice_button_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static int bkvoice_button_deadline(uint32_t timeout_ms,
                                   struct timespec *deadline)
{
  uint64_t nsec;

  if (clock_gettime(CLOCK_REALTIME, deadline) < 0)
    {
      return bkvoice_button_errno();
    }

  nsec = (uint64_t)deadline->tv_nsec +
         (uint64_t)(timeout_ms % 1000u) * 1000000u;
  deadline->tv_sec += (time_t)(timeout_ms / 1000u) +
                      (time_t)(nsec / 1000000000u);
  deadline->tv_nsec = (long)(nsec % 1000000000u);
  return 0;
}

/* Each call consumes a sequence number even when the endpoint is absent or
 * out of TX buffers.  The AP therefore treats any successfully received
 * record as a newer lease, never as proof that earlier records arrived.
 */

static int bkvoice_button_send_locked(struct bkvoice_button_s *button,
                                      uint32_t pressed)
{
  struct bkvoice_button_event_s event;
  int ret;

  if (button->sequence == UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  event.magic = BKVOICE_BUTTON_MAGIC;
  event.version = BKVOICE_BUTTON_VERSION;
  event.sequence = ++button->sequence;
  event.pressed = pressed;
  event.reserved[0] = 0;
  event.reserved[1] = 0;

  if (!__atomic_load_n(&button->endpoint_created, __ATOMIC_ACQUIRE) ||
      !is_rpmsg_ept_ready(&button->endpoint))
    {
      return -ENOTCONN;
    }

  ret = rpmsg_trysend(&button->endpoint, &event, sizeof(event));
  return ret < 0 ? ret : 0;
}

static int bkvoice_button_send(struct bkvoice_button_s *button, uint32_t pressed)
{
  int ret = nxmutex_lock(&button->endpoint_lock);

  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_button_send_locked(button, pressed);
  nxmutex_unlock(&button->endpoint_lock);
  return ret;
}

static int bkvoice_button_endpoint_cb(struct rpmsg_endpoint *endpoint,
                                      void *data, size_t len, uint32_t src,
                                      void *priv)
{
  (void)endpoint;
  (void)data;
  (void)len;
  (void)src;
  (void)priv;
  return -ENOSYS;
}

static void bkvoice_button_endpoint_unbind(struct rpmsg_endpoint *endpoint)
{
  struct bkvoice_button_s *button = endpoint->priv;

  if (button != NULL)
    {
      /* An AP restart must not turn a held button into a new press. */

      __atomic_store_n(&button->endpoint_created, false, __ATOMIC_RELEASE);
      __atomic_store_n(&button->armed, false, __ATOMIC_RELEASE);
      __atomic_store_n(&button->release_since_disconnect, false,
                       __ATOMIC_RELEASE);
      __atomic_add_fetch(&button->arm_epoch, 1u, __ATOMIC_ACQ_REL);
    }
}

static void bkvoice_button_ns_bind(struct rpmsg_device *rdev, void *priv,
                                   const char *name, uint32_t dest)
{
  struct bkvoice_button_s *button = priv;

  if (nxmutex_lock(&button->endpoint_lock) < 0)
    {
      return;
    }

  if (__atomic_load_n(&button->started, __ATOMIC_ACQUIRE) &&
      !__atomic_load_n(&button->endpoint_created, __ATOMIC_ACQUIRE))
    {
      button->endpoint.priv = button;
      if (rpmsg_create_ept(&button->endpoint, rdev, name, RPMSG_ADDR_ANY,
                           dest, bkvoice_button_endpoint_cb,
                           bkvoice_button_endpoint_unbind) >= 0)
        {
          __atomic_store_n(&button->endpoint_created, true,
                           __ATOMIC_RELEASE);
          if (__atomic_load_n(&button->release_since_disconnect,
                              __ATOMIC_ACQUIRE))
            {
              __atomic_store_n(&button->armed, true, __ATOMIC_RELEASE);
              (void)bkvoice_button_send_locked(button, false);
            }
        }
    }

  nxmutex_unlock(&button->endpoint_lock);
}

static void bkvoice_button_device_created(struct rpmsg_device *rdev,
                                           void *priv)
{
  const char *cpu = rpmsg_get_cpuname(rdev);

  if (cpu != NULL && strcmp(cpu, "ap") == 0)
    {
      bkvoice_button_ns_bind(rdev, priv, BKVOICE_BUTTON_ENDPOINT,
                             RPMSG_ADDR_ANY);
    }
}

static void bkvoice_button_device_destroy(struct rpmsg_device *rdev,
                                          void *priv)
{
  struct bkvoice_button_s *button = priv;
  const char *cpu = rpmsg_get_cpuname(rdev);

  if (cpu == NULL || strcmp(cpu, "ap") != 0)
    {
      return;
    }

  if (nxmutex_lock(&button->endpoint_lock) >= 0)
    {
      __atomic_store_n(&button->endpoint_created, false, __ATOMIC_RELEASE);
      __atomic_store_n(&button->armed, false, __ATOMIC_RELEASE);
      __atomic_store_n(&button->release_since_disconnect, false,
                       __ATOMIC_RELEASE);
      __atomic_add_fetch(&button->arm_epoch, 1u, __ATOMIC_ACQ_REL);
      if (button->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&button->endpoint);
        }

      memset(&button->endpoint, 0, sizeof(button->endpoint));
      nxmutex_unlock(&button->endpoint_lock);
    }
}

static void bkvoice_button_stable(struct bkvoice_button_s *button,
                                  uint32_t pressed)
{
  bool changed = !button->stable_known || button->stable_pressed != pressed;

  button->stable_known = true;
  button->stable_pressed = pressed;
  if (pressed == 0)
    {
      /* Initial release and a later physical release are the only events
       * that arm publication.  A reconnect may use only a release observed
       * after its preceding disconnect.
       */

      __atomic_store_n(&button->release_since_disconnect, true,
                       __ATOMIC_RELEASE);
      __atomic_store_n(&button->armed, true, __ATOMIC_RELEASE);
      (void)bkvoice_button_send(button, false);
    }
  else if (changed && __atomic_load_n(&button->armed, __ATOMIC_ACQUIRE))
    {
      (void)bkvoice_button_send(button, true);
    }
}

static int bkvoice_button_worker(int argc, char *argv[])
{
  struct bkvoice_button_s *button = &g_bkvoice_button;
  uint32_t sample;
  uint32_t candidate = 0;
  bool candidate_known = false;
  clock_t candidate_since = 0;
  clock_t heartbeat_since = 0;
  uint32_t observed_epoch;
  int fd;

  fd = open(button->devpath, O_RDONLY
#ifdef CONFIG_BK7258_PRODUCT_KEYS
            | O_NONBLOCK
#endif
           );
  if (fd < 0)
    {
      __atomic_store_n(&button->worker_result, bkvoice_button_errno(),
                       __ATOMIC_RELEASE);
      (void)sem_post(&button->worker_ready);
      (void)sem_post(&button->worker_done);
      return 0;
    }

  __atomic_store_n(&button->worker_result, OK, __ATOMIC_RELEASE);
  observed_epoch = __atomic_load_n(&button->arm_epoch, __ATOMIC_ACQUIRE);
  (void)sem_post(&button->worker_ready);
  heartbeat_since = clock_systime_ticks();
  while (!__atomic_load_n(&button->stop_requested, __ATOMIC_ACQUIRE))
    {
      bool sample_valid = false;
      clock_t now;

/* A failed/nonblocking read deliberately does not renew the AP lease or send
 * a heartbeat based on an old successful sample. */
#ifdef CONFIG_BK7258_PRODUCT_KEYS
      {
        btn_buttonset_t raw;
        ssize_t bytes = read(fd, &raw, sizeof(raw));
        if (bytes == sizeof(raw) && (raw & ~BKVOICE_PRODUCT_KEY_VALID) == 0)
          {
            sample = raw;
            sample_valid = true;
          }
      }
#else
      {
        bool raw = false;
        if (ioctl(fd, GPIOC_READ, (unsigned long)(uintptr_t)&raw) >= 0)
          {
            sample = button->active_low ? !raw : raw;
            sample_valid = true;
          }
      }
#endif
      if (sample_valid)
        {
          uint32_t epoch = __atomic_load_n(&button->arm_epoch,
                                           __ATOMIC_ACQUIRE);

          if (epoch != observed_epoch)
            {
              /* A disconnect invalidates a release sampled before it. */

              observed_epoch = epoch;
              candidate_known = false;
              button->stable_known = false;
            }

          now = clock_systime_ticks();
          if (!candidate_known || sample != candidate)
            {
              candidate = sample;
              candidate_known = true;
              candidate_since = now;
            }
          else if ((!button->stable_known ||
                    button->stable_pressed != candidate) &&
                   (clock_t)(now - candidate_since) >=
                   MSEC2TICK(BKVOICE_BUTTON_DEBOUNCE_MS))
            {
              bkvoice_button_stable(button, candidate);
            }

          if (button->stable_known &&
              __atomic_load_n(&button->armed, __ATOMIC_ACQUIRE) &&
              (clock_t)(now - heartbeat_since) >=
              MSEC2TICK(BKVOICE_BUTTON_HEARTBEAT_MS))
            {
              heartbeat_since = now;
              (void)bkvoice_button_send(button, button->stable_pressed);
            }
        }

      (void)nxsig_usleep(BKVOICE_BUTTON_POLL_MS * 1000u);
    }

  (void)close(fd);
  (void)sem_post(&button->worker_done);
  return 0;
}

int bkvoice_button_start(const char *devpath, bool active_low)
{
  struct bkvoice_button_s *button = &g_bkvoice_button;
  bool done_initialized = false;
  bool ready_initialized = false;
  int ret;

  if (devpath == NULL || devpath[0] == '\0' || strlen(devpath) >= PATH_MAX)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&button->lifecycle_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&button->started, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&button->lifecycle_lock);
      return -EBUSY;
    }

  memset(button->devpath, 0, sizeof(button->devpath));
  memcpy(button->devpath, devpath, strlen(devpath));
  button->active_low = active_low;
  button->stable_known = false;
  button->worker_owned = false;
  __atomic_store_n(&button->armed, false, __ATOMIC_RELEASE);
  __atomic_store_n(&button->release_since_disconnect, false,
                   __ATOMIC_RELEASE);
  button->sequence = 0;
  __atomic_store_n(&button->worker_result, -EINPROGRESS,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&button->arm_epoch, 1u, __ATOMIC_RELEASE);
  __atomic_store_n(&button->stop_requested, false, __ATOMIC_RELEASE);

  ret = sem_init(&button->worker_done, 0, 0);
  if (ret < 0)
    {
      ret = bkvoice_button_errno();
    }
  else
    {
      done_initialized = true;
    }

  if (ret >= 0 && sem_init(&button->worker_ready, 0, 0) < 0)
    {
      ret = bkvoice_button_errno();
    }
  else if (ret >= 0)
    {
      ready_initialized = true;
    }

  if (ret >= 0)
    {
      __atomic_store_n(&button->started, true, __ATOMIC_RELEASE);
      ret = rpmsg_register_callback(button, bkvoice_button_device_created,
                                    bkvoice_button_device_destroy,
                                    NULL, NULL);
      button->callback_registered = ret >= 0;
    }

  if (ret >= 0)
    {
      /* A console command exits immediately after connect.  Give the GPIO
       * owner its own task group so command exit cannot cancel polling.
       */

      ret = task_create("bkvoice-button", BKVOICE_BUTTON_PRIORITY,
                        BKVOICE_BUTTON_STACKSIZE, bkvoice_button_worker, NULL);
      if (ret < 0)
        {
          ret = bkvoice_button_errno();
        }
      else
        {
          button->worker_owned = true;
          {
            struct timespec deadline;

            ret = bkvoice_button_deadline(BKVOICE_BUTTON_STOP_MS,
                                          &deadline);
            if (ret >= 0)
              {
                do
                  {
                    ret = sem_timedwait(&button->worker_ready, &deadline);
                  }
                while (ret < 0 && errno == EINTR);

                if (ret < 0)
                  {
                    __atomic_store_n(&button->stop_requested, true,
                                     __ATOMIC_RELEASE);
                    ret = bkvoice_button_errno();
                  }
                else
                  {
                    ret = __atomic_load_n(&button->worker_result,
                                          __ATOMIC_ACQUIRE);
                  }
              }
          }
        }
    }

  if (ret < 0)
    {
      if (button->worker_owned)
        {
          /* Keep the task and its synchronization objects owned on failure.
           * stop() waits for its final acknowledgement before releasing them.
           */

          __atomic_store_n(&button->stop_requested, true, __ATOMIC_RELEASE);
          nxmutex_unlock(&button->lifecycle_lock);
          return ret;
        }

      __atomic_store_n(&button->started, false, __ATOMIC_RELEASE);
      if (button->callback_registered)
        {
          rpmsg_unregister_callback(button, bkvoice_button_device_created,
                                    bkvoice_button_device_destroy,
                                    NULL, NULL);
          button->callback_registered = false;
        }

      if (nxmutex_lock(&button->endpoint_lock) >= 0)
        {
          __atomic_store_n(&button->endpoint_created, false,
                           __ATOMIC_RELEASE);
          if (button->endpoint.rdev != NULL)
            {
              rpmsg_destroy_ept(&button->endpoint);
            }

          memset(&button->endpoint, 0, sizeof(button->endpoint));
          nxmutex_unlock(&button->endpoint_lock);
        }

      if (done_initialized)
        {
          (void)sem_destroy(&button->worker_done);
        }

      if (ready_initialized)
        {
          (void)sem_destroy(&button->worker_ready);
        }
    }

  nxmutex_unlock(&button->lifecycle_lock);
  return ret;
}

int bkvoice_button_stop(void)
{
  struct bkvoice_button_s *button = &g_bkvoice_button;
  struct timespec deadline;
  int ret;

  ret = nxmutex_lock(&button->lifecycle_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!__atomic_load_n(&button->started, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&button->lifecycle_lock);
      return -EALREADY;
    }

  __atomic_store_n(&button->stop_requested, true, __ATOMIC_RELEASE);
  ret = OK;
  if (button->worker_owned)
    {
      ret = bkvoice_button_deadline(BKVOICE_BUTTON_STOP_MS, &deadline);
      if (ret >= 0)
        {
          do
            {
              ret = sem_timedwait(&button->worker_done, &deadline);
            }
          while (ret < 0 && errno == EINTR);

          if (ret < 0)
            {
              ret = bkvoice_button_errno();
            }
        }
    }

  if (ret < 0)
    {
      /* Keep started set and the task ownership intact; a later stop() may
       * finish teardown, but no new GPIO owner can start in the meantime.
       */

      nxmutex_unlock(&button->lifecycle_lock);
      return ret;
    }

  button->worker_owned = false;
  (void)bkvoice_button_send(button, false);
  if (button->callback_registered)
    {
      rpmsg_unregister_callback(button, bkvoice_button_device_created, bkvoice_button_device_destroy,
                                NULL, NULL);
      button->callback_registered = false;
    }

  if (nxmutex_lock(&button->endpoint_lock) >= 0)
    {
      __atomic_store_n(&button->endpoint_created, false, __ATOMIC_RELEASE);
      if (button->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&button->endpoint);
        }

      memset(&button->endpoint, 0, sizeof(button->endpoint));
      nxmutex_unlock(&button->endpoint_lock);
    }

  (void)sem_destroy(&button->worker_done);
  (void)sem_destroy(&button->worker_ready);
  __atomic_store_n(&button->started, false, __ATOMIC_RELEASE);
  nxmutex_unlock(&button->lifecycle_lock);
  return 0;
}

bool bkvoice_button_running(void)
{
  return __atomic_load_n(&g_bkvoice_button.started, __ATOMIC_ACQUIRE);
}
