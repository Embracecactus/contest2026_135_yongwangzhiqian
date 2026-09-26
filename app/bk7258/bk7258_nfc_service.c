/****************************************************************************
 * app/bk7258/bk7258_nfc_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP NFC service.  RPMsg receive paths only validate and queue; device I/O
 * is performed by the single worker.
 ****************************************************************************/
#include <nuttx/config.h>

#ifdef CONFIG_BK7258_NFC_SERVICE

#include "bk7258_nfc_core.h"
#include "bk7258_nfc_service.h"
#ifdef CONFIG_BK7258_PROVISION_GATT
#include "bk7258_provision_gatt.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <nuttx/clock.h>
#include <nuttx/contactless/ioctl.h>
#include <nuttx/signal.h>
#if defined(CONFIG_CL_MFRC522_FRAME) || defined(CONFIG_CL_MFRC522_RF)
#include <nuttx/contactless/mfrc522_rf.h>
#ifdef CONFIG_CL_MFRC522_FRAME
#include <nuttx/contactless/mfrc522_frame.h>
#endif
#endif
#ifdef CONFIG_CL_ISODEP
#include <nuttx/contactless/isodep.h>
#endif
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>


struct bknfc_source_s
{
  int fd;
};
struct bknfc_server_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  spinlock_t request_lock;
  sem_t request_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  bool active;
  bool pending;
  uint32_t epoch;
  uint32_t request_epoch;
  bool replay_valid;
  struct bknfc_rpc_request_s active_request;
  struct bknfc_rpc_request_s last_request;
  struct bknfc_rpc_response_s last_response;
  struct bknfc_source_s source;
};
static struct bknfc_server_s g_bknfc_server =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = SP_UNLOCKED,
  .source =
  {
    .fd = -1,
  },
};

static int bknfc_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}
static int bknfc_close(void *context);

static int bknfc_open(void *context)
{
  struct bknfc_source_s *source = context;

  if (source->fd >= 0)
    {
      return -EBUSY;
    }

  source->fd = open(CONFIG_BK7258_NFC_DEVPATH, O_RDONLY);
  if (source->fd < 0)
    {
      return bknfc_errno();
    }

#if defined(CONFIG_CL_MFRC522_FRAME) || defined(CONFIG_CL_MFRC522_RF)
  if (ioctl(source->fd, MFRC522IOC_SET_RF, 1) < 0)
    {
      int ret = bknfc_errno();
      /* 开启可能部分生效，失败也须尝试释放射频并关闭句柄。 */

      (void)bknfc_close(context);
      return ret;
    }

  nxsig_usleep(6000);
#endif
  return 0;
}
static int bknfc_read(void *context, void *buffer, size_t length)
{
  struct bknfc_source_s *source = context;
  struct picc_uid_s uid;
  int ret;

  if (source->fd < 0 || buffer == NULL || length != 1)
    {
      return -EINVAL;
    }

  /* The legacy read path ignores selection errors. The standard ioctl
   * reports them and returns the complete cascade result. Keep its UID
   * local: only a validated presence byte reaches the RPC core.
   */

  *(uint8_t *)buffer = 0;
  memset(&uid, 0, sizeof(uid));
  ret = ioctl(source->fd, MFRC522IOC_GET_PICC_UID, (unsigned long)&uid);
  if (ret < 0)
    {
      ret = bknfc_errno();
    }
  else if (ret != 0 || (uid.sak & PICC_TYPE_NOT_COMPLETE) != 0 ||
           (uid.size != 4 && uid.size != 7 && uid.size != 10))
    {
      ret = -EPROTO;
    }
  else
    {
      *(uint8_t *)buffer = 1;
      ret = 1;
    }

  memset(&uid, 0, sizeof(uid));
  return ret;
}
static int bknfc_close(void *context)
{
  struct bknfc_source_s *source = context;
  int fd = source->fd;
  int ret = 0;

  source->fd = -1;
  if (fd < 0)
    {
      return -EBADF;
    }

#if defined(CONFIG_CL_MFRC522_FRAME) || defined(CONFIG_CL_MFRC522_RF)
  if (ioctl(fd, MFRC522IOC_SET_RF, 0) < 0)
    {
      ret = bknfc_errno();
    }
#endif
  if (close(fd) < 0 && ret == 0)
    {
      ret = bknfc_errno();
    }

  return ret;
}

#if defined(CONFIG_CL_MFRC522_FRAME) || defined(CONFIG_CL_MFRC522_RF)
/* The generic driver enables its antenna during registration. Product idle
 * must release the field too, before any explicit scan or claim window.
 */
static int bknfc_idle(struct bknfc_source_s *source)
{
  source->fd = open(CONFIG_BK7258_NFC_DEVPATH, O_RDONLY);
  if (source->fd < 0) return bknfc_errno();
  return bknfc_close(source);
}
#endif
#if defined(CONFIG_CL_ISODEP) && defined(CONFIG_CL_MFRC522_FRAME)
static int bknfc_frame(void *arg, const uint8_t *tx, size_t tx_length,
                       uint8_t *rx, size_t *rx_length, uint32_t timeout_ms)
{
  struct bknfc_source_s *source = arg;
  struct mfrc522_exchange_s frame = {0};
  int ret;

  if (tx_length > sizeof(frame.tx))
    {
      return -EMSGSIZE;
    }

  frame.timeout_ms = timeout_ms;
  frame.tx_length = tx_length;
  memcpy(frame.tx, tx, tx_length);
  ret = ioctl(source->fd, MFRC522IOC_EXCHANGE, (unsigned long)&frame);
  if (ret < 0)
    {
      return bknfc_errno();
    }

  if (frame.rx_length > *rx_length)
    {
      return -EMSGSIZE;
    }

  *rx_length = frame.rx_length;
  memcpy(rx, frame.rx, frame.rx_length);
  return 0;
}

static int bknfc_delay(void *arg, uint32_t delay_us)
{
  (void)arg;
  return nxsig_usleep(delay_us);
}

static void bknfc_release_rf(void *arg)
{
  struct bknfc_source_s *source = arg;
  ioctl(source->fd, MFRC522IOC_SET_RF, 0);
}

static uint64_t bknfc_now_ms(void *arg)
{
  struct timespec now;
  (void)arg;
  clock_systime_timespec(&now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
#endif

static int bknfc_hce_exchange(void *context, bool report)
{
  const char *stage = "rf-prepare";
  int ret = -ENOSYS;

#if defined(CONFIG_CL_ISODEP) && defined(CONFIG_CL_MFRC522_FRAME)
  static const struct isodep_transport_s transport =
  {
    .exchange = bknfc_frame,
    .delay_us = bknfc_delay,
    .release = bknfc_release_rf,
    .now_ms = bknfc_now_ms,
  };
  static const uint8_t select[] =
  {
    0x00, 0xa4, 0x04, 0x00, 0x08,
    0xf0, 0x53, 0x48, 0x41, 0x4e, 0x49, 0x55, 0x01
  };
  static const uint8_t expected[] = {0x53, 0x48, 0x4e, 0x01, 0x90, 0x00};
  struct bknfc_source_s *source = context;
  struct isodep_session_s session = {0};
  struct picc_uid_s uid = {0};
  uint8_t response[32];
  size_t length = sizeof(response);
  bknfc_release_rf(source);
  ret = nxsig_usleep(6000);
  if (ret < 0)
    {
      goto out;
    }

  if (ioctl(source->fd, MFRC522IOC_SET_RF, 1) < 0)
    {
      ret = bknfc_errno();
      goto out;
    }

  ret = nxsig_usleep(6000);
  if (ret >= 0)
    {
      stage = "uid-select";
      ret = ioctl(source->fd, MFRC522IOC_GET_PICC_UID, (unsigned long)&uid);
      if (ret < 0)
        {
          ret = bknfc_errno();
        }
    }

  if (ret >= 0)
    {
      stage = "isodep-activate";
      ret = isodep_activate(&session, &transport, source, uid.sak);
    }

  memset(&uid, 0, sizeof(uid));
  if (ret >= 0)
    {
      stage = "select-aid";
      ret = isodep_transceive(&session, select, sizeof(select), response,
                              &length, 3000);
      if (ret == 0 && (length != sizeof(expected) ||
                      memcmp(response, expected, sizeof(expected)) != 0))
        {
          ret = -EPROTO;
        }
    }

#ifdef CONFIG_BK7258_PROVISION_GATT
  if (ret == 0)
    {
      uint8_t command[5 + BKPROV_GATT_LOCATOR_SIZE] =
        {0x80, 0xda, 0x00, 0x00, BKPROV_GATT_LOCATOR_SIZE};
      stage = "locator-exchange";
      ret = bkprov_gatt_locator(command + 5);
      if (ret == 0)
        {
          length = sizeof(response);
          ret = isodep_transceive(&session, command, sizeof(command),
                                  response, &length, 3000);
          if (ret == 0 && (length != 2 || response[0] != 0x90 ||
                          response[1] != 0x00))
            {
              ret = -EPROTO;
            }
        }

      memset(command, 0, sizeof(command));
    }
#endif
  memset(response, 0, sizeof(response));
  isodep_release(&session);
  bknfc_release_rf(source);

out:
#else
  (void)context;
#endif

  if (report)
    {
      syslog(ret == 0 ? LOG_INFO : LOG_WARNING,
             "BKNFC HCE terminal stage=%s ret=%d\n", stage, ret);
    }

  return ret;
}

static int bknfc_hce(void *context)
{
  return bknfc_hce_exchange(context, true);
}

static const struct bknfc_source_ops_s g_bknfc_ops =
{
  .open = bknfc_open,
  .read = bknfc_read,
  .close = bknfc_close,
  .hce = bknfc_hce,
};
static int bknfc_send(struct bknfc_server_s *server,
                      const struct bknfc_rpc_response_s *response,
                         uint32_t epoch)
{
  irqstate_t flags;
  bool current;
  int ret = nxmutex_lock(&server->endpoint_lock);

  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  current = server->epoch == epoch;
  spin_unlock_irqrestore(&server->request_lock, flags);

  if (!current ||
      !__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE) ||
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
static int bknfc_worker(int argc, char **argv)
{
  struct bknfc_server_s *server = &g_bknfc_server;
#if defined(CONFIG_CL_MFRC522_FRAME) || defined(CONFIG_CL_MFRC522_RF)
  bool idle_ready = false;
  int idle_error = 0;
#endif
#if defined(CONFIG_BK7258_PROVISION_GATT) && defined(CONFIG_CL_ISODEP) && \
    defined(CONFIG_CL_MFRC522_FRAME)
  uint8_t delivered[BKPROV_GATT_LOCATOR_SIZE] = {0};
  bool delivered_valid = false;
#endif

  (void)argc;
  (void)argv;
  for (;;)
    {
      struct bknfc_rpc_request_s request;
      struct bknfc_rpc_response_s response;
      irqstate_t flags;
      uint32_t epoch;

#if defined(CONFIG_CL_MFRC522_FRAME) || defined(CONFIG_CL_MFRC522_RF)
      /* Board registration is deferred. Retry the initial RF release until
       * the device exists instead of leaving its power-on field enabled.
       */
      if (!idle_ready)
        {
          int ret = bknfc_idle(&server->source);
          if (ret == 0)
            {
              idle_ready = true;
              syslog(LOG_INFO, "BKNFC idle field released\n");
            }
          else if (ret != idle_error)
            {
              syslog(LOG_WARNING, "BKNFC idle field release pending: %d\n",
                     ret);
            }

          idle_error = ret;
        }

      /* The same worker owns both explicit probes and discovery. RF activity
       * is limited to an open physical-presence provisioning window.
       */
      int waitret = nxsem_tickwait_uninterruptible(&server->request_sem,
                                                   MSEC2TICK(500));
      if (waitret == -ETIMEDOUT)
        {
#if defined(CONFIG_BK7258_PROVISION_GATT) && defined(CONFIG_CL_ISODEP)
          uint8_t locator[BKPROV_GATT_LOCATOR_SIZE];
          if (bkprov_gatt_locator(locator) < 0)
            {
              delivered_valid = false;
              memset(delivered, 0, sizeof(delivered));
            }
          else if (idle_ready && (!delivered_valid ||
                    memcmp(delivered, locator, sizeof(locator)) != 0) &&
                   bknfc_open(&server->source) == 0)
            {
              int result = bknfc_hce_exchange(&server->source, false);
              (void)bknfc_close(&server->source);
              if (result == 0)
                {
                  memcpy(delivered, locator, sizeof(delivered));
                  delivered_valid = true;
                }
            }

          memset(locator, 0, sizeof(locator));
#endif
          continue;
        }
      if (waitret < 0)
#else
      if (nxsem_wait_uninterruptible(&server->request_sem) < 0)
#endif
        {
          continue;
        }
      flags = spin_lock_irqsave(&server->request_lock);
      /* A disconnect may leave a semaphore token behind.  Only the pending
       * slot owns work; consuming another token must never execute it twice.
       */

      if (!server->active || !server->pending ||
          server->request_epoch != server->epoch ||
          !__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
        {
          spin_unlock_irqrestore(&server->request_lock, flags);
          continue;
        }

      server->pending = false;
      epoch = server->request_epoch;
      memcpy(&request, &server->active_request, sizeof(request));
      spin_unlock_irqrestore(&server->request_lock, flags);

      (void)bknfc_rpc_handle_request(&request, &response, &g_bknfc_ops,
                                     &server->source);
      flags = spin_lock_irqsave(&server->request_lock);
      /* Old I/O still closes its own fd, but must not publish a result or
       * clear a request accepted by a reconnected peer.
       */

      if (server->epoch == epoch && server->active &&
          server->request_epoch == epoch)
        {
          memcpy(&server->last_request, &request, sizeof(request));
          memcpy(&server->last_response, &response, sizeof(response));
          server->replay_valid = true;
          server->active = false;
        }
      spin_unlock_irqrestore(&server->request_lock, flags);

      (void)bknfc_send(server, &response, epoch);
    }

  return 0;
}
static int bknfc_server_cb(struct rpmsg_endpoint *endpoint, void *data,
                           size_t len, uint32_t src, void *priv)
{
  struct bknfc_server_s *server = priv;
  const struct bknfc_rpc_request_s *request = data;
  struct bknfc_rpc_response_s response;
  irqstate_t flags;
  uint32_t epoch;
  bool replay = false;
  bool duplicate;
  int ret;

  (void)endpoint;
  (void)src;
  if (request == NULL || len != sizeof(*request) ||
      request->magic != BKNFC_RPC_MAGIC ||
      request->version != BKNFC_RPC_VERSION)
    {
      return -EINVAL;
    }
  flags = spin_lock_irqsave(&server->request_lock);
  epoch = server->epoch;
  spin_unlock_irqrestore(&server->request_lock, flags);

  if (!bknfc_rpc_request_valid(request))
    {
      bknfc_rpc_make_response(&response, request, -EINVAL);
      return bknfc_send(server, &response, epoch);
    }
  flags = spin_lock_irqsave(&server->request_lock);
  if (server->epoch != epoch ||
      !__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
    {
      spin_unlock_irqrestore(&server->request_lock, flags);
      return -ENOTCONN;
    }

  if (server->replay_valid &&
      request->session == server->last_request.session &&
      request->sequence == server->last_request.sequence)
    {
      if (memcmp(request, &server->last_request, sizeof(*request)) != 0)
        {
          spin_unlock_irqrestore(&server->request_lock, flags);
          bknfc_rpc_make_response(&response, request, -EPROTO);
          return bknfc_send(server, &response, epoch);
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

      bknfc_rpc_make_response(&response, request, -EBUSY);
      return bknfc_send(server, &response, epoch);
    }
  else
    {
      memcpy(&server->active_request, request, sizeof(*request));
      server->active = true;
      server->pending = true;
      server->request_epoch = epoch;
    }

  spin_unlock_irqrestore(&server->request_lock, flags);
  if (replay)
    {
      return bknfc_send(server, &response, epoch);
    }

  ret = nxsem_post(&server->request_sem);
  if (ret >= 0)
    {
      return 0;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  if (server->epoch == epoch && server->request_epoch == epoch)
    {
      server->active = false;
      server->pending = false;
    }
  spin_unlock_irqrestore(&server->request_lock, flags);
  bknfc_rpc_make_response(&response, request, ret);
  return bknfc_send(server, &response, epoch);
}
static void bknfc_disconnect(struct bknfc_server_s *server)
{
  irqstate_t flags;

  if (nxmutex_lock(&server->endpoint_lock) >= 0)
    {
      flags = spin_lock_irqsave(&server->request_lock);
      server->epoch++;
      server->active = false;
      server->pending = false;
      server->replay_valid = false;
      __atomic_store_n(&server->endpoint_created, false, __ATOMIC_RELEASE);
      spin_unlock_irqrestore(&server->request_lock, flags);

      if (server->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&server->endpoint);
        }

      memset(&server->endpoint, 0, sizeof(server->endpoint));
      nxmutex_unlock(&server->endpoint_lock);
    }
}

static void bknfc_server_unbind(struct rpmsg_endpoint *endpoint)
{
  bknfc_disconnect(endpoint->priv);
}

static bool bknfc_ns_match(struct rpmsg_device *rdev, void *priv,
                           const char *name, uint32_t dest)
{
  const char *cpu = rpmsg_get_cpuname(rdev);

  (void)priv;
  (void)dest;
  return cpu != NULL && strcmp(cpu, "cp") == 0 &&
         strcmp(name, BKNFC_RPC_ENDPOINT) == 0;
}
static void bknfc_ns_bind(struct rpmsg_device *rdev, void *priv,
                          const char *name, uint32_t dest)
{
  struct bknfc_server_s *server = priv;

  if (nxmutex_lock(&server->endpoint_lock) < 0)
    {
      return;
    }

  if (!__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
    {
      server->endpoint.priv = server;
      if (rpmsg_create_ept(&server->endpoint, rdev, name, RPMSG_ADDR_ANY,
                           dest, bknfc_server_cb, bknfc_server_unbind) >= 0)
        {
          __atomic_store_n(&server->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }
  nxmutex_unlock(&server->endpoint_lock);
}
static void bknfc_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  const char *cpu = rpmsg_get_cpuname(rdev);

  if (cpu != NULL && strcmp(cpu, "cp") == 0)
    {
      bknfc_disconnect(priv);
    }
}

int bk7258_nfc_service_prepare(void)
{
  return 0;
}
int bk7258_nfc_service_start(void)
{
  struct bknfc_server_s *server = &g_bknfc_server;
  bool sem_ready = false;
  bool callback_registered = false;
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
      sem_ready = true;
    }
#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&server->request_sem, SEM_PRIO_NONE);
    }
#endif
  if (ret >= 0)
    {
      ret = rpmsg_register_callback(server, NULL, bknfc_device_destroy,
                                    bknfc_ns_match, bknfc_ns_bind);
      callback_registered = ret >= 0;
    }
  if (ret >= 0)
    {
      pid = task_create("bknfc-rpc", CONFIG_BK7258_NFC_RPC_PRIORITY,
                        CONFIG_BK7258_NFC_RPC_STACKSIZE, bknfc_worker,
                        NULL);
      if (pid < 0)
        {
          ret = bknfc_errno();
        }
    }
  if (ret >= 0)
    {
      __atomic_store_n(&server->initialized, true, __ATOMIC_RELEASE);
      syslog(LOG_INFO,
             "BKNFC SERVICE READY endpoint=%s device=%s privacy=presence-only\n",
             BKNFC_RPC_ENDPOINT, CONFIG_BK7258_NFC_DEVPATH);
    }
  else
    {
      if (callback_registered)
        {
          rpmsg_unregister_callback(server, NULL, bknfc_device_destroy,
                                    bknfc_ns_match, bknfc_ns_bind);
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

      if (sem_ready)
        {
          (void)nxsem_destroy(&server->request_sem);
        }
      server->active = false;
      server->pending = false;
      server->replay_valid = false;
    }

  nxmutex_unlock(&server->init_lock);
  return ret;
}
#endif /* CONFIG_BK7258_NFC_SERVICE */
