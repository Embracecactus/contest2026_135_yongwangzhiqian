/****************************************************************************
 * app/bk7258/bk7258_health_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-owned read-only device-health source and RPMsg transport.  The receive
 * callback only validates and queues work; battery ADC and temperature RPMsg
 * reads run in the dedicated worker.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_HEALTH_SERVICE

#include "bk7258_health_core.h"
#include "bk7258_health_protocol.h"
#include "bk7258_health_service.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/irq.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/power/battery_ioctl.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_temperature.h>

#define BKHEALTH_REFRESH_MS 60000u

struct bkhealth_source_context_s
{
  int battery_fd;
};

struct bkhealth_server_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  spinlock_t request_lock;
  sem_t request_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  bool active;
  bool replay_valid;
  struct bkhealth_rpc_request_s active_request;
  struct bkhealth_rpc_request_s last_request;
  struct bkhealth_rpc_response_s last_response;
  struct bk7258_health_service_snapshot_s snapshot;
  bool snapshot_valid;
  struct bkhealth_source_context_s source;
};

static struct bkhealth_server_s g_bkhealth_server =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = SP_UNLOCKED,
  .source =
  {
    .battery_fd = -1,
  },
};

static int bkhealth_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static int bkhealth_battery_open(void *context)
{
  struct bkhealth_source_context_s *source = context;
  int fd;

  if (source->battery_fd >= 0)
    {
      return -EBUSY;
    }

  fd = open(CONFIG_BK7258_HEALTH_BATTERY_DEVPATH, O_RDONLY);
  if (fd < 0)
    {
      return bkhealth_errno();
    }

  source->battery_fd = fd;
  return 0;
}

static int bkhealth_battery_state(void *context, uint32_t *state)
{
  struct bkhealth_source_context_s *source = context;
  int value = 0;
  int ret;

  if (state == NULL || source->battery_fd < 0)
    {
      return -EINVAL;
    }

  ret = ioctl(source->battery_fd, BATIOC_STATE,
              (unsigned long)(uintptr_t)&value);
  if (ret < 0)
    {
      return bkhealth_errno();
    }

  *state = (uint32_t)value;
  return 0;
}

static int bkhealth_battery_voltage_mv(void *context, int32_t *voltage_mv)
{
  struct bkhealth_source_context_s *source = context;
  int value = 0;
  int ret;

  if (voltage_mv == NULL || source->battery_fd < 0)
    {
      return -EINVAL;
    }

  ret = ioctl(source->battery_fd, BATIOC_GET_VOLTAGE,
              (unsigned long)(uintptr_t)&value);
  if (ret < 0)
    {
      return bkhealth_errno();
    }

  *voltage_mv = value;
  return 0;
}

static int bkhealth_battery_close(void *context)
{
  struct bkhealth_source_context_s *source = context;
  int fd = source->battery_fd;

  source->battery_fd = -1;
  if (fd < 0)
    {
      return -EBADF;
    }

  return close(fd) < 0 ? bkhealth_errno() : 0;
}

static int bkhealth_temperature_read(
  void *context, struct bkhealth_temperature_sample_s *sample)
{
  struct bk7258_temperature_sample_s chip_sample;
  int ret;

  (void)context;
  if (sample == NULL)
    {
      return -EINVAL;
    }

  memset(&chip_sample, 0, sizeof(chip_sample));
  ret = bk7258_temperature_read(&chip_sample);
  if (ret < 0)
    {
      return ret;
    }

  memset(sample, 0, sizeof(*sample));
  sample->generation = chip_sample.generation;
  sample->sequence = chip_sample.sequence;
  sample->raw_code = chip_sample.raw_code;
  sample->reference_raw = chip_sample.reference_raw;
  sample->temperature_millicelsius = chip_sample.temperature_millicelsius;
  if ((chip_sample.flags & BK7258_TEMPERATURE_FLAG_RAW_VALID) != 0)
    {
      sample->flags |= BKHEALTH_TEMPERATURE_RAW_VALID;
    }

  if ((chip_sample.flags & BK7258_TEMPERATURE_FLAG_CALIBRATED) != 0)
    {
      sample->flags |= BKHEALTH_TEMPERATURE_CALIBRATED;
    }

  return 0;
}

static const struct bkhealth_source_ops_s g_bkhealth_source_ops =
{
  .battery_open = bkhealth_battery_open,
  .battery_state = bkhealth_battery_state,
  .battery_voltage_mv = bkhealth_battery_voltage_mv,
  .battery_close = bkhealth_battery_close,
  .temperature_read = bkhealth_temperature_read,
};

static int bkhealth_send(struct bkhealth_server_s *server,
                         const struct bkhealth_rpc_response_s *response)
{
  int ret = nxmutex_lock(&server->endpoint_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (!__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE) ||
      !is_rpmsg_ept_ready(&server->endpoint))
    {
      ret = -ENOTCONN;
    }
  else
    {
      ret = rpmsg_trysend(&server->endpoint, response, sizeof(*response));
    }

  nxmutex_unlock(&server->endpoint_lock);
  return ret;
}

static void bkhealth_publish_snapshot(
  struct bkhealth_server_s *server,
  const struct bkhealth_rpc_response_s *response)
{
  struct bk7258_health_service_snapshot_s snapshot;
  irqstate_t flags;

  memset(&snapshot, 0, sizeof(snapshot));
  if (response->battery_state_status == 0 &&
      (response->flags & BKHEALTH_FLAG_BATTERY_STATE_VALID) != 0)
    {
      snapshot.flags |= BK7258_HEALTH_SNAPSHOT_BATTERY_STATE_VALID;
      snapshot.battery_state = response->battery_state;
    }

  if (response->battery_voltage_status == 0 &&
      (response->flags & BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID) != 0)
    {
      snapshot.flags |= BK7258_HEALTH_SNAPSHOT_BATTERY_VOLTAGE_VALID;
      snapshot.battery_voltage_mv = response->battery_voltage_mv;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  snapshot.sequence = server->snapshot.sequence == UINT32_MAX ? 1u :
                      server->snapshot.sequence + 1u;
  memcpy(&server->snapshot, &snapshot, sizeof(snapshot));
  server->snapshot_valid = true;
  spin_unlock_irqrestore(&server->request_lock, flags);
}

static void bkhealth_collect_periodic(struct bkhealth_server_s *server)
{
  struct bkhealth_rpc_request_s request;
  struct bkhealth_rpc_response_s response;

  memset(&request, 0, sizeof(request));
  request.magic = BKHEALTH_RPC_MAGIC;
  request.version = BKHEALTH_RPC_VERSION;
  request.command = BKHEALTH_RPC_STATUS;
  request.session = 1;
  request.sequence = 1;
  (void)bkhealth_rpc_handle_request(&request, &response,
                                    &g_bkhealth_source_ops,
                                    &server->source);
  bkhealth_publish_snapshot(server, &response);
}

static int bkhealth_worker(int argc, char **argv)
{
  struct bkhealth_server_s *server = &g_bkhealth_server;
  bool first_sample = true;

  (void)argc;
  (void)argv;

  for (; ; )
    {
      struct bkhealth_rpc_request_s request;
      struct bkhealth_rpc_response_s response;
      irqstate_t flags;
      bool active;
      int wait_ret = 0;

      if (!first_sample)
        {
          wait_ret = nxsem_tickwait_uninterruptible(
            &server->request_sem, MSEC2TICK(BKHEALTH_REFRESH_MS));
        }

      first_sample = false;

      flags = spin_lock_irqsave(&server->request_lock);
      active = server->active;
      if (active)
        {
          memcpy(&request, &server->active_request, sizeof(request));
        }

      spin_unlock_irqrestore(&server->request_lock, flags);

      if (!active)
        {
          if (wait_ret >= 0 || wait_ret == -ETIMEDOUT)
            {
              bkhealth_collect_periodic(server);
            }

          continue;
        }

      (void)bkhealth_rpc_handle_request(&request, &response,
                                        &g_bkhealth_source_ops,
                                        &server->source);
      bkhealth_publish_snapshot(server, &response);

      flags = spin_lock_irqsave(&server->request_lock);
      memcpy(&server->last_request, &request, sizeof(request));
      memcpy(&server->last_response, &response, sizeof(response));
      server->replay_valid = true;
      server->active = false;
      spin_unlock_irqrestore(&server->request_lock, flags);

      /* A matching client retry replays this completed response if no RPMsg
       * TX buffer was available.  Never block this worker on a TX buffer.
       */

      (void)bkhealth_send(server, &response);
    }

  return 0;
}

static int bkhealth_server_cb(struct rpmsg_endpoint *endpoint, void *data,
                              size_t len, uint32_t src, void *priv)
{
  struct bkhealth_server_s *server = priv;
  const struct bkhealth_rpc_request_s *request = data;
  struct bkhealth_rpc_response_s response;
  irqstate_t flags;
  bool replay = false;
  bool duplicate;
  int ret;

  (void)endpoint;
  (void)src;

  if (request == NULL || len != sizeof(*request) ||
      request->magic != BKHEALTH_RPC_MAGIC ||
      request->version != BKHEALTH_RPC_VERSION)
    {
      return -EINVAL;
    }

  if (!bkhealth_rpc_request_valid(request))
    {
      bkhealth_rpc_make_response(&response, request, -EINVAL);
      return bkhealth_send(server, &response);
    }

  flags = spin_lock_irqsave(&server->request_lock);
  if (server->replay_valid &&
      request->session == server->last_request.session &&
      request->sequence == server->last_request.sequence)
    {
      if (memcmp(request, &server->last_request, sizeof(*request)) != 0)
        {
          spin_unlock_irqrestore(&server->request_lock, flags);
          bkhealth_rpc_make_response(&response, request, -EPROTO);
          return bkhealth_send(server, &response);
        }

      memcpy(&response, &server->last_response, sizeof(response));
      replay = true;
    }
  else if (server->active)
    {
      duplicate = memcmp(request, &server->active_request,
                         sizeof(*request)) == 0;
      spin_unlock_irqrestore(&server->request_lock, flags);
      if (duplicate)
        {
          return 0;
        }

      bkhealth_rpc_make_response(&response, request, -EBUSY);
      return bkhealth_send(server, &response);
    }
  else
    {
      memcpy(&server->active_request, request, sizeof(*request));
      server->active = true;
    }

  spin_unlock_irqrestore(&server->request_lock, flags);
  if (replay)
    {
      return bkhealth_send(server, &response);
    }

  ret = nxsem_post(&server->request_sem);
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&server->request_lock);
      server->active = false;
      spin_unlock_irqrestore(&server->request_lock, flags);
      bkhealth_rpc_make_response(&response, request, ret);
      return bkhealth_send(server, &response);
    }

  return 0;
}

static bool bkhealth_ns_match(struct rpmsg_device *rdev, void *priv,
                              const char *name, uint32_t dest)
{
  const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv;
  (void)dest;
  return cpuname != NULL && strcmp(cpuname, "cp") == 0 &&
         strcmp(name, BKHEALTH_RPC_ENDPOINT) == 0;
}

static void bkhealth_ns_bind(struct rpmsg_device *rdev, void *priv,
                             const char *name, uint32_t dest)
{
  struct bkhealth_server_s *server = priv;
  int ret;

  ret = nxmutex_lock(&server->endpoint_lock);
  if (ret < 0)
    {
      return;
    }

  if (!__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
    {
      server->endpoint.priv = server;
      ret = rpmsg_create_ept(&server->endpoint, rdev, name,
                             RPMSG_ADDR_ANY, dest,
                             bkhealth_server_cb, NULL);
      if (ret >= 0)
        {
          __atomic_store_n(&server->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&server->endpoint_lock);
}

static void bkhealth_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  struct bkhealth_server_s *server = priv;
  const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, "cp") != 0)
    {
      return;
    }

  __atomic_store_n(&server->endpoint_created, false, __ATOMIC_RELEASE);
  if (nxmutex_lock(&server->endpoint_lock) >= 0)
    {
      if (server->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&server->endpoint);
        }

      memset(&server->endpoint, 0, sizeof(server->endpoint));
      nxmutex_unlock(&server->endpoint_lock);
    }
}

int bk7258_health_service_prepare(void)
{
  return 0;
}

int bk7258_health_service_start(void)
{
  struct bkhealth_server_s *server = &g_bkhealth_server;
  bool callback_registered = false;
  bool semaphore_initialized = false;
  pid_t pid;
  int ret;

  ret = nxmutex_lock(&server->init_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&server->initialized, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&server->init_lock);
      return 0;
    }

  ret = nxsem_init(&server->request_sem, 0, 0);
  if (ret >= 0)
    {
      semaphore_initialized = true;
    }

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&server->request_sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(server, NULL,
                                    bkhealth_device_destroy,
                                    bkhealth_ns_match,
                                    bkhealth_ns_bind);
      callback_registered = ret >= 0;
    }

  if (ret >= 0)
    {
      pid = task_create("bkhealth-rpc", CONFIG_BK7258_HEALTH_RPC_PRIORITY,
                        CONFIG_BK7258_HEALTH_RPC_STACKSIZE,
                        bkhealth_worker, NULL);
      if (pid < 0)
        {
          ret = bkhealth_errno();
        }
    }

  if (ret >= 0)
    {
      __atomic_store_n(&server->initialized, true, __ATOMIC_RELEASE);
      syslog(LOG_INFO,
             "BKHEALTH SERVICE READY endpoint=%s battery=%s "
             "temperature=raw-first percent=unavailable\n",
             BKHEALTH_RPC_ENDPOINT, CONFIG_BK7258_HEALTH_BATTERY_DEVPATH);
    }
  else
    {
      if (callback_registered)
        {
          rpmsg_unregister_callback(server, NULL,
                                    bkhealth_device_destroy,
                                    bkhealth_ns_match,
                                    bkhealth_ns_bind);
        }

      if (nxmutex_lock(&server->endpoint_lock) >= 0)
        {
          __atomic_store_n(&server->endpoint_created, false,
                           __ATOMIC_RELEASE);
          if (server->endpoint.rdev != NULL)
            {
              rpmsg_destroy_ept(&server->endpoint);
            }

          memset(&server->endpoint, 0, sizeof(server->endpoint));
          nxmutex_unlock(&server->endpoint_lock);
        }

      if (semaphore_initialized)
        {
          (void)nxsem_destroy(&server->request_sem);
        }

      server->active = false;
      server->replay_valid = false;
    }

  nxmutex_unlock(&server->init_lock);
  return ret;
}

int bk7258_health_service_snapshot(
  struct bk7258_health_service_snapshot_s *snapshot)
{
  struct bkhealth_server_s *server = &g_bkhealth_server;
  irqstate_t flags;

  if (snapshot == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  if (!server->snapshot_valid)
    {
      spin_unlock_irqrestore(&server->request_lock, flags);
      memset(snapshot, 0, sizeof(*snapshot));
      return -EAGAIN;
    }

  memcpy(snapshot, &server->snapshot, sizeof(*snapshot));
  spin_unlock_irqrestore(&server->request_lock, flags);
  return 0;
}

#endif /* CONFIG_BK7258_HEALTH_SERVICE */
