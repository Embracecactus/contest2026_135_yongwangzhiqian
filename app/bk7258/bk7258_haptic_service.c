/****************************************************************************
 * app/bk7258/bk7258_haptic_service.c
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP worker owns the standard FF descriptor and effect. RPMsg callbacks
 * perform no VFS operations, including on disconnect.
 ****************************************************************************/
#include <nuttx/config.h>
#ifdef CONFIG_BK7258_HAPTIC_SERVICE

#include "bk7258_haptic_protocol.h"
#include "bk7258_haptic_service.h"
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>
#include <nuttx/input/ff.h>
#include <nuttx/irq.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

struct bkhaptic_server_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  spinlock_t lock;
  sem_t sem;
  bool initialized;
  bool endpoint_created;
  bool connected;
  bool stop_pending;
  bool pending;
  bool local_pending;
  bool local_stop_pending;
  bool active;
  bool local_active;
  bool local_owned;
  bool replay_valid;
  bool notice_pending;
  uint32_t epoch;
  uint32_t request_epoch;
  uint32_t notice_epoch;
  struct bkhaptic_rpc_request_s request;
  unsigned int local_duration_ms;
  uint32_t local_sequence;
  uint32_t local_completed;
  int local_result;
  struct bkhaptic_rpc_request_s last_request;
  struct bkhaptic_rpc_response_s last_response;
  struct bkhaptic_rpc_response_s notice;
  int fd;
  int effect_id;
};

static struct bkhaptic_server_s g_bkhaptic =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .lock = SP_UNLOCKED,
  .fd = -1,
  .effect_id = -1
};

static int bkhaptic_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static bool bkhaptic_connected(struct bkhaptic_server_s *s, uint32_t epoch)
{
  irqstate_t flags = spin_lock_irqsave(&s->lock);
  bool connected = s->connected && s->epoch == epoch && !s->stop_pending;
  spin_unlock_irqrestore(&s->lock, flags);
  return connected;
}

static int bkhaptic_close(struct bkhaptic_server_s *s)
{
  int fd = s->fd;

  s->fd = -1;
  s->effect_id = -1;
  return fd >= 0 && close(fd) < 0 ? bkhaptic_errno() : 0;
}

static int bkhaptic_event(struct bkhaptic_server_s *s, int value)
{
  struct ff_event_s event = { .code = s->effect_id, .value = value };
  ssize_t ret = write(s->fd, &event, sizeof(event));
  return ret < 0 ? bkhaptic_errno() :
         (ret == (ssize_t)sizeof(event) ? 0 : -EIO);
}

static int bkhaptic_stop(struct bkhaptic_server_s *s)
{
  int ret = 0;
  int erased;

  if (s->fd >= 0 && s->effect_id >= 0)
    {
      ret = bkhaptic_event(s, 0);
      erased = ioctl(s->fd, EVIOCRMFF, (unsigned long)s->effect_id);
      if (erased < 0 && ret == 0)
        {
          ret = bkhaptic_errno();
        }

      if (ret < 0)
        {
          /* The upper half's close hook retries stopping owned effects. */

          (void)bkhaptic_close(s);
        }
      else
        {
          s->effect_id = -1;
        }
    }

  return ret;
}

static int bkhaptic_open(struct bkhaptic_server_s *s)
{
  unsigned long bits[BITS_TO_LONGS(FF_CNT)] = {0};
  int effects = 0;
  int ret;

  if (s->fd < 0)
    {
      s->fd = open(CONFIG_BK7258_HAPTIC_DEVPATH, O_RDWR);
      if (s->fd < 0)
        {
          return bkhaptic_errno();
        }
    }

  ret = ioctl(s->fd, EVIOCGBIT, (unsigned long)(uintptr_t)bits);
  if (ret >= 0)
    {
      ret = ioctl(s->fd, EVIOCGEFFECTS, (unsigned long)(uintptr_t)&effects);
    }

  if (ret < 0)
    {
      ret = bkhaptic_errno();
    }
  else if (!test_bit(FF_RUMBLE, bits) || effects < 1)
    {
      ret = -ENOTSUP;
    }

  if (ret < 0)
    {
      (void)bkhaptic_close(s);
    }

  return ret;
}

static int bkhaptic_pulse(struct bkhaptic_server_s *s, unsigned int duration)
{
  struct ff_effect effect = {0};
  int ret = bkhaptic_open(s);
  if (ret < 0) return ret;
  effect.id = s->effect_id; effect.type = FF_RUMBLE; effect.replay.length = duration;
  effect.u.rumble.strong_magnitude = UINT16_MAX;
  if (ioctl(s->fd, EVIOCSFF, (unsigned long)(uintptr_t)&effect) < 0) return bkhaptic_errno();
  s->effect_id = effect.id; ret = bkhaptic_event(s, 1);
  if (ret < 0) (void)bkhaptic_stop(s);
  return ret;
}

static int bkhaptic_execute(struct bkhaptic_server_s *s,
                           const struct bkhaptic_rpc_request_s *request,
                           struct bkhaptic_rpc_response_s *response,
                           uint32_t epoch)
{
  int ret;

  if (!bkhaptic_connected(s, epoch))
    {
      return -ENOTCONN;
    }

  if (request->command == BKHAPTIC_RPC_STOP)
    {
      return bkhaptic_stop(s);
    }

  response->ready = 1;
  if (request->command == BKHAPTIC_RPC_STATUS)
    {
      return bkhaptic_open(s);
    }

  if (!bkhaptic_connected(s, epoch))
    {
      return -ENOTCONN;
    }

  /* Reuse our owned slot. Upload/playback enforce active and cooldown policy;
   * a new pulse must never stop an existing pulse just to upload another.
   */

  ret = bkhaptic_pulse(s, request->duration_ms);
  if (ret < 0) return ret;
  if (!bkhaptic_connected(s, epoch))
    {
      (void)bkhaptic_stop(s);
      return -ENOTCONN;
    }

  response->accepted_ms = request->duration_ms;
  return 0;
}


static int bkhaptic_send(struct bkhaptic_server_s *s,
                        const struct bkhaptic_rpc_response_s *response,
                        uint32_t epoch)
{
  int ret = nxmutex_lock(&s->endpoint_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (!s->endpoint_created || !bkhaptic_connected(s, epoch) ||
      !is_rpmsg_ept_ready(&s->endpoint))
    {
      ret = -ENOTCONN;
    }
  else
    {
      ret = rpmsg_trysend(&s->endpoint, response, sizeof(*response));
    }

  nxmutex_unlock(&s->endpoint_lock);
  return ret;
}

static int bkhaptic_worker(int argc, char **argv)
{
  struct bkhaptic_server_s *s = &g_bkhaptic;
  struct bkhaptic_rpc_request_s request;
  struct bkhaptic_rpc_response_s response;
  uint32_t epoch;
  irqstate_t flags;
  bool replay;
  bool stale;
  bool cleanup;
  int ret;

  (void)argc;
  (void)argv;
  for (;;)
    {
      (void)nxsem_wait_uninterruptible(&s->sem);
      for (;;)
        {
          flags = spin_lock_irqsave(&s->lock);
          if (s->stop_pending)
            {
              s->stop_pending = false;
              spin_unlock_irqrestore(&s->lock, flags);
              (void)bkhaptic_stop(s);
              (void)bkhaptic_close(s);
              continue;
            }

          if (s->notice_pending)
            {
              response = s->notice;
              epoch = s->notice_epoch;
              s->notice_pending = false;
              spin_unlock_irqrestore(&s->lock, flags);
              (void)bkhaptic_send(s, &response, epoch);
            continue;
          }

          if (s->local_stop_pending)
            {
              s->local_stop_pending = false;
              spin_unlock_irqrestore(&s->lock, flags);
              (void)bkhaptic_stop(s);
              flags = spin_lock_irqsave(&s->lock);
              s->local_owned = false;
              spin_unlock_irqrestore(&s->lock, flags);
              continue;
            }

          if (s->local_pending)
            {
              unsigned int duration = s->local_duration_ms;
              uint32_t sequence = s->local_sequence;
              s->local_pending = false; s->active = true; s->local_active = true;
              spin_unlock_irqrestore(&s->lock, flags);
              ret = bkhaptic_pulse(s, duration);
              if (ret < 0) syslog(LOG_ERR, "BKHAPTIC local pulse: %d\n", ret);
              if (ret >= 0)
                {
                  (void)usleep(duration * 1000);
                  ret = bkhaptic_stop(s);
                }
              flags = spin_lock_irqsave(&s->lock); s->active = false; s->local_active = false;
              s->local_owned = false;
              s->local_result = ret;
              s->local_completed = sequence;
              spin_unlock_irqrestore(&s->lock, flags);
              continue;
            }

          if (!s->pending)
            {
              spin_unlock_irqrestore(&s->lock, flags);
              break;
            }

          request = s->request;
          epoch = s->request_epoch;
          s->pending = false;
          s->active = true;
          replay = s->replay_valid &&
                   memcmp(&request, &s->last_request, sizeof(request)) == 0;
          stale = s->replay_valid && !replay &&
                  (request.session != s->last_request.session ||
                   request.sequence <= s->last_request.sequence);
          if (replay)
            {
              response = s->last_response;
            }

          spin_unlock_irqrestore(&s->lock, flags);
          if (!replay)
            {
              bkhaptic_rpc_make_response(&response, &request, 0);
              response.status = stale ? -EPROTO :
                bkhaptic_execute(s, &request, &response, epoch);
            }

          flags = spin_lock_irqsave(&s->lock);
          cleanup = !s->connected || s->epoch != epoch || s->stop_pending;
          if (!cleanup && !stale)
            {
              s->last_request = request;
              s->last_response = response;
              s->replay_valid = true;
            }

          s->active = false;
          spin_unlock_irqrestore(&s->lock, flags);
          if (cleanup)
            {
              (void)bkhaptic_stop(s);
              (void)bkhaptic_close(s);
            }
          else
            {
              (void)bkhaptic_send(s, &response, epoch);
            }
        }
    }

  return 0;
}

static int bkhaptic_server_cb(struct rpmsg_endpoint *endpoint, void *data,
                             size_t len, uint32_t src, void *priv)
{
  struct bkhaptic_server_s *s = priv;
  struct bkhaptic_rpc_request_s request;
  irqstate_t flags;
  int error = 0;

  (void)endpoint;
  (void)src;
  if (data == NULL || len != sizeof(request))
    {
      return -EINVAL;
    }

  memcpy(&request, data, sizeof(request));
  if (!bkhaptic_rpc_request_valid(&request))
    {
      error = -EINVAL;
    }

  flags = spin_lock_irqsave(&s->lock);
  if (!s->connected)
    {
      spin_unlock_irqrestore(&s->lock, flags);
      return -ENOTCONN;
    }

  if (!error && (s->pending || s->active || s->local_pending || s->local_owned))
    {
      if (s->local_active || s->local_pending || s->local_owned) error = -EBUSY;
      else
        {
      if (memcmp(&request, &s->request, sizeof(request)) == 0)
        {
          spin_unlock_irqrestore(&s->lock, flags);
          return 0;
        }

      error = request.session == s->request.session &&
              request.sequence == s->request.sequence ? -EPROTO : -EBUSY;
        }
    }

  if (error)
    {
      if (s->notice_pending)
        {
          spin_unlock_irqrestore(&s->lock, flags);
          return -EBUSY;
        }

      bkhaptic_rpc_make_response(&s->notice, &request, error);
      s->notice_epoch = s->epoch;
      s->notice_pending = true;
    }
  else
    {
      s->request = request;
      s->request_epoch = s->epoch;
      s->pending = true;
    }

  spin_unlock_irqrestore(&s->lock, flags);
  return nxsem_post(&s->sem);
}

static void bkhaptic_disconnect(struct bkhaptic_server_s *s)
{
  irqstate_t flags = spin_lock_irqsave(&s->lock);

  s->connected = false;
  s->epoch++;
  s->stop_pending = !s->local_pending && !s->local_active && !s->local_owned;
  s->pending = false;
  s->notice_pending = false;
  s->replay_valid = false;
  spin_unlock_irqrestore(&s->lock, flags);
  (void)nxsem_post(&s->sem);

  if (nxmutex_lock(&s->endpoint_lock) >= 0)
    {
      if (s->endpoint_created)
        {
          s->endpoint_created = false;
          rpmsg_destroy_ept(&s->endpoint);
        }

      nxmutex_unlock(&s->endpoint_lock);
    }
}

static void bkhaptic_unbind(struct rpmsg_endpoint *endpoint)
{
  bkhaptic_disconnect(endpoint->priv);
}

static bool bkhaptic_ns_match(struct rpmsg_device *rdev, void *priv,
                             const char *name, uint32_t dest)
{
  const char *cpu = rpmsg_get_cpuname(rdev);
  (void)priv;
  (void)dest;
  return cpu && strcmp(cpu, "cp") == 0 &&
         strcmp(name, BKHAPTIC_RPC_ENDPOINT) == 0;
}

static void bkhaptic_ns_bind(struct rpmsg_device *rdev, void *priv,
                            const char *name, uint32_t dest)
{
  struct bkhaptic_server_s *s = priv;
  irqstate_t flags;

  if (nxmutex_lock(&s->endpoint_lock) < 0)
    {
      return;
    }

  if (!s->endpoint_created)
    {
      s->endpoint.priv = s;
      if (rpmsg_create_ept(&s->endpoint, rdev, name, RPMSG_ADDR_ANY, dest,
                           bkhaptic_server_cb, bkhaptic_unbind) >= 0)
        {
          s->endpoint_created = true;
          flags = spin_lock_irqsave(&s->lock);
          s->connected = true;
          spin_unlock_irqrestore(&s->lock, flags);
        }
    }

  nxmutex_unlock(&s->endpoint_lock);
}

static void bkhaptic_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  const char *cpu = rpmsg_get_cpuname(rdev);
  if (cpu && strcmp(cpu, "cp") == 0)
    {
      bkhaptic_disconnect(priv);
    }
}

int bkhaptic_service_initialize(void)
{
  struct bkhaptic_server_s *s = &g_bkhaptic;
  bool sem_ready = false;
  bool registered = false;
  int ret = nxmutex_lock(&s->init_lock);
  pid_t pid;

  if (ret < 0)
    {
      return ret;
    }

  if (s->initialized)
    {
      nxmutex_unlock(&s->init_lock);
      return 0;
    }

  ret = nxsem_init(&s->sem, 0, 0);
  sem_ready = ret >= 0;
#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&s->sem, SEM_PRIO_NONE);
    }
#endif
  if (ret >= 0)
    {
      ret = rpmsg_register_callback(s, NULL, bkhaptic_device_destroy,
                                    bkhaptic_ns_match, bkhaptic_ns_bind);
      registered = ret >= 0;
    }

  if (ret >= 0)
    {
      pid = task_create("bkhaptic-rpc", CONFIG_BK7258_HAPTIC_RPC_PRIORITY,
                        CONFIG_BK7258_HAPTIC_RPC_STACKSIZE,
                        bkhaptic_worker, NULL);
      if (pid < 0)
        {
          ret = bkhaptic_errno();
        }
    }

  if (ret >= 0)
    {
      s->initialized = true;
      syslog(LOG_INFO, "BKHAPTIC SERVICE READY endpoint=%s device=%s\n",
             BKHAPTIC_RPC_ENDPOINT, CONFIG_BK7258_HAPTIC_DEVPATH);
    }
  else
    {
      if (registered)
        {
          rpmsg_unregister_callback(s, NULL, bkhaptic_device_destroy,
                                    bkhaptic_ns_match, bkhaptic_ns_bind);
          bkhaptic_disconnect(s);
        }

      if (sem_ready)
        {
          nxsem_destroy(&s->sem);
        }
    }

  nxmutex_unlock(&s->init_lock);
  return ret;
}

static int bkhaptic_product_queue(unsigned int duration_ms, uint32_t *sequence)
{
  struct bkhaptic_server_s *s = &g_bkhaptic;
  irqstate_t flags;
  if (duration_ms == 0 || duration_ms > 32767) return -EINVAL;
  flags = spin_lock_irqsave(&s->lock);
  if (!s->initialized) { spin_unlock_irqrestore(&s->lock, flags); return -ENODEV; }
  if (s->pending || s->active || s->local_pending || s->local_owned || s->local_stop_pending || s->stop_pending)
    { spin_unlock_irqrestore(&s->lock, flags); return -EBUSY; }
  s->local_duration_ms = duration_ms; s->local_pending = true;
  s->local_sequence++;
  if (sequence) *sequence = s->local_sequence;
  spin_unlock_irqrestore(&s->lock, flags);
  return nxsem_post(&s->sem);
}

int bkhaptic_service_pulse(unsigned int duration_ms)
{
  return bkhaptic_product_queue(duration_ms, NULL);
}

int bkhaptic_service_pulse_wait(unsigned int duration_ms)
{
  struct bkhaptic_server_s *s = &g_bkhaptic;
  uint32_t sequence;
  clock_t deadline;
  int ret;

  /* Only the serialized product owner may wait, and only between MIC owners.
   * A failed motor must not hold up capture indefinitely or play a late pulse. */
  if (duration_ms == 0 || duration_ms > 100u) return -EINVAL;
  ret = bkhaptic_product_queue(duration_ms, &sequence);
  if (ret < 0) return ret;
  deadline = clock_systime_ticks() + MSEC2TICK(duration_ms + 100u);
  for (;;)
    {
      irqstate_t flags = spin_lock_irqsave(&s->lock);
      bool done = s->local_completed == sequence;
      ret = s->local_result;
      spin_unlock_irqrestore(&s->lock, flags);
      if (done) return ret;
      if ((sclock_t)(clock_systime_ticks() - deadline) >= 0) break;
      (void)usleep(2000);
    }
  (void)bkhaptic_service_stop_product();
  return -ETIMEDOUT;
}

int bkhaptic_service_stop_product(void)
{
  struct bkhaptic_server_s *s = &g_bkhaptic; irqstate_t flags = spin_lock_irqsave(&s->lock);
  if (!s->initialized) { spin_unlock_irqrestore(&s->lock, flags); return -ENODEV; }
  if (!s->local_active && !s->local_pending && !s->local_owned) { spin_unlock_irqrestore(&s->lock, flags); return 0; }
  s->local_pending = false; s->local_stop_pending = true;
  spin_unlock_irqrestore(&s->lock, flags); return nxsem_post(&s->sem);
}
#endif
