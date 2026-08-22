/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/
 * bk7258_ota_rpmsg.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned AP-source to CP Pair Installer transport.  RPMsg callbacks only
 * copy bounded messages and wake role-local task context.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_OTA_RPMSG

#include <errno.h>
#include <stdbool.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_ota.h>
#include <arch/chip/bk7258_ota_rpmsg.h>
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_BK7258_OTA_SOURCE_FILE)
#  include <arch/chip/bk7258_ota_source_file.h>
#endif
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_BK7258_OTA_SOURCE_HTTP)
#  include <arch/chip/bk7258_ota_source_http.h>
#endif
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_rptun.h"

#define BK7258_OTA_RPMSG_EPT_NAME       "bk7258-ota-v2"
#define BK7258_OTA_RPMSG_MAGIC          0x32544f42u /* "BOT2" */
#define BK7258_OTA_RPMSG_VERSION        2u
#define BK7258_OTA_RPMSG_PAYLOAD_SIZE   432u
#define BK7258_OTA_RPMSG_SEND_MS        1000u
#define BK7258_OTA_RPMSG_CONTROL_ACK_MS 5000u

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_OTA_RPMSG_REMOTE_NAME  "cp"
#else
#  define BK7258_OTA_RPMSG_REMOTE_NAME  "ap"
#endif

enum bk7258_ota_rpmsg_command_e
{
  BK7258_OTA_RPMSG_START = 1,
  BK7258_OTA_RPMSG_READ,
  BK7258_OTA_RPMSG_DATA,
  BK7258_OTA_RPMSG_PROGRESS,
  BK7258_OTA_RPMSG_CANCEL,
  BK7258_OTA_RPMSG_COMPLETE,
  BK7258_OTA_RPMSG_APPLY_FILE,
  BK7258_OTA_RPMSG_APPLY_HTTP,
  BK7258_OTA_RPMSG_MANAGER_STATUS,
  BK7258_OTA_RPMSG_MANAGER_CANCEL,
  BK7258_OTA_RPMSG_CONTROL_REPLY
};

struct bk7258_ota_rpmsg_header_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t generation;
  uint32_t session;
  uint32_t sequence;
  uint32_t image;
  uint32_t offset;
  uint32_t length;
  int32_t status;
};

struct bk7258_ota_rpmsg_message_s
{
  struct bk7258_ota_rpmsg_header_s header;
  uint8_t payload[BK7258_OTA_RPMSG_PAYLOAD_SIZE];
};

struct bk7258_ota_rpmsg_dev_s
{
  struct rpmsg_endpoint ept;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile int connection_error;
  spinlock_t lock;
#ifdef CONFIG_BK7258_AP_CORE
  mutex_t session_lock;
  sem_t event_sem;
  volatile bool stage_active;
  volatile bool cancel_requested;
  bool read_pending;
  bool progress_pending;
  bool complete_pending;
  struct bk7258_ota_rpmsg_header_s read_request;
  struct bk7258_ota_progress_s progress;
  uint32_t shared_progress_sequence;
  uint32_t complete_session;
  int complete_status;
  uint32_t next_session;
  const struct bk7258_ota_source_ops_s *source;
  void *source_context;
#if defined(CONFIG_BK7258_OTA_SOURCE_FILE) || \
    defined(CONFIG_BK7258_OTA_SOURCE_HTTP)
  sem_t control_sem;
  volatile bool control_busy;
  uint16_t control_command;
  uint32_t control_timeout_ms;
  char control_location[BK7258_OTA_CONTROL_URL_SIZE];
  char control_ca_path[BK7258_OTA_CONTROL_CA_SIZE];
#endif
#else
  sem_t worker_sem;
  sem_t data_sem;
  sem_t control_sem;
  mutex_t control_lock;
  volatile bool stage_busy;
  volatile bool cancel_requested;
  struct bk7258_ota_manifest_s manifest;
  uint32_t active_session;
  uint32_t next_sequence;
  uint32_t waiting_sequence;
  bool data_valid;
  uint32_t data_length;
  int data_status;
  uint8_t data[BK7258_OTA_RPMSG_PAYLOAD_SIZE];
  uint32_t next_control_session;
  uint32_t waiting_control_session;
  bool control_reply_valid;
  uint32_t control_reply_length;
  int control_reply_status;
  uint8_t control_reply[sizeof(struct bk7258_ota_manager_status_s)];
#endif
};

static_assert(sizeof(struct bk7258_ota_rpmsg_header_s) == 36u,
              "BK7258 OTA RPMsg header ABI changed");
static_assert(sizeof(struct bk7258_ota_rpmsg_message_s) == 468u,
              "BK7258 OTA RPMsg message exceeds the 496-byte payload");
static_assert(sizeof(struct bk7258_ota_manifest_s) <=
              BK7258_OTA_RPMSG_PAYLOAD_SIZE,
              "BK7258 OTA manifest does not fit one START message");
static_assert(BK7258_OTA_CONTROL_PATH_SIZE <=
              BK7258_OTA_RPMSG_PAYLOAD_SIZE,
              "BK7258 OTA control path does not fit one message");
static_assert(BK7258_OTA_CONTROL_URL_SIZE + BK7258_OTA_CONTROL_CA_SIZE <=
              BK7258_OTA_RPMSG_PAYLOAD_SIZE,
              "BK7258 OTA HTTP control does not fit one message");
static_assert(sizeof(struct bk7258_ota_manager_status_s) <=
              BK7258_OTA_RPMSG_PAYLOAD_SIZE,
              "BK7258 OTA manager status does not fit one message");

static struct bk7258_ota_rpmsg_dev_s g_bk7258_ota_rpmsg =
{
#ifdef CONFIG_BK7258_AP_CORE
  .session_lock = NXMUTEX_INITIALIZER,
#else
  .control_lock = NXMUTEX_INITIALIZER,
#endif
};

#define BK7258_OTA_SHARED_PROGRESS_MAGIC 0x50544f42u /* "BOTP" */
#define BK7258_OTA_SHARED_PROGRESS_VERSION 1u
#define BK7258_OTA_SHARED_PROGRESS_ADDR \
  (BK7258_RPTUN_RESOURCE_ADDR + BK7258_RPTUN_RESOURCE_SIZE)

struct bk7258_ota_shared_progress_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t session;
  volatile uint32_t sequence;
  int32_t status;
  uint32_t phase;
  uint32_t image;
  uint32_t completed;
  uint32_t total;
};

static_assert(sizeof(struct bk7258_ota_shared_progress_s) <=
              BK7258_RPTUN_CARVEOUT_OFFSET -
              (BK7258_RPTUN_RESOURCE_OFFSET +
               BK7258_RPTUN_RESOURCE_SIZE),
              "BK7258 OTA shared progress exceeds the resource gap");

#ifndef CONFIG_BK7258_AP_CORE
static void bk7258_ota_rpmsg_publish_progress(
  uint32_t session, const struct bk7258_ota_progress_s *progress)
{
  volatile struct bk7258_ota_shared_progress_s *shared =
    (volatile struct bk7258_ota_shared_progress_s *)
      BK7258_OTA_SHARED_PROGRESS_ADDR;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t sequence;

  sequence = __atomic_load_n((uint32_t *)(uintptr_t)&shared->sequence,
                             __ATOMIC_RELAXED);
  if ((sequence & 1u) != 0u)
    {
      sequence++;
    }

  __atomic_store_n((uint32_t *)(uintptr_t)&shared->sequence,
                   sequence + 1u, __ATOMIC_RELEASE);
  shared->magic = BK7258_OTA_SHARED_PROGRESS_MAGIC;
  shared->version = BK7258_OTA_SHARED_PROGRESS_VERSION;
  shared->size = sizeof(*shared);
  shared->generation = control->generation;
  shared->session = session;
  shared->status = 0;
  shared->phase = progress->phase;
  shared->image = progress->image;
  shared->completed = progress->completed;
  shared->total = progress->total;
  __asm volatile ("dmb sy" ::: "memory");
  __atomic_store_n((uint32_t *)(uintptr_t)&shared->sequence,
                   sequence + 2u, __ATOMIC_RELEASE);
}
#else
static bool bk7258_ota_rpmsg_snapshot_progress(
  uint32_t session, struct bk7258_ota_progress_s *progress,
  uint32_t *sequence)
{
  volatile struct bk7258_ota_shared_progress_s *shared =
    (volatile struct bk7258_ota_shared_progress_s *)
      BK7258_OTA_SHARED_PROGRESS_ADDR;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t first;
  uint32_t second;
  unsigned int attempt;

  for (attempt = 0; attempt < 3u; attempt++)
    {
      first = __atomic_load_n(
                (uint32_t *)(uintptr_t)&shared->sequence,
                __ATOMIC_ACQUIRE);
      if ((first & 1u) != 0u)
        {
          continue;
        }

      if (shared->magic != BK7258_OTA_SHARED_PROGRESS_MAGIC ||
          shared->version != BK7258_OTA_SHARED_PROGRESS_VERSION ||
          shared->size != sizeof(*shared) ||
          shared->generation != control->generation ||
          shared->session != session || shared->status < 0)
        {
          return false;
        }

      progress->phase = (enum bk7258_ota_phase_e)shared->phase;
      progress->image = (enum bk7258_ota_image_e)shared->image;
      progress->completed = shared->completed;
      progress->total = shared->total;
      __asm volatile ("dmb sy" ::: "memory");
      second = __atomic_load_n(
                 (uint32_t *)(uintptr_t)&shared->sequence,
                 __ATOMIC_ACQUIRE);
      if (first == second && (second & 1u) == 0u)
        {
          *sequence = second;
          return true;
        }
    }

  return false;
}

static void bk7258_ota_rpmsg_sync_progress(
  struct bk7258_ota_rpmsg_dev_s *priv)
{
  struct bk7258_ota_progress_s progress;
  uint32_t sequence;

  if (!__atomic_load_n(&priv->stage_active, __ATOMIC_ACQUIRE) ||
      priv->source == NULL || priv->source->checkpoint == NULL ||
      !bk7258_ota_rpmsg_snapshot_progress(priv->next_session,
                                           &progress, &sequence) ||
      sequence == priv->shared_progress_sequence)
    {
      return;
    }

  priv->shared_progress_sequence = sequence;
  (void)priv->source->checkpoint(priv->source_context, &progress);
}
#endif

static size_t bk7258_ota_rpmsg_size(uint32_t payload)
{
  return sizeof(struct bk7258_ota_rpmsg_header_s) + payload;
}

static void bk7258_ota_rpmsg_flush(sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static bool bk7258_ota_rpmsg_ready(void)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  return __atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) &&
         control->state == BK7258_RPTUN_STATE_CONNECTED &&
         is_rpmsg_ept_ready(&priv->ept);
}

static int bk7258_ota_rpmsg_send(
  const struct bk7258_ota_rpmsg_message_s *msg, uint32_t payload)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_OTA_RPMSG_SEND_MS);
  int ret = -ENOTCONN;

  if (payload > BK7258_OTA_RPMSG_PAYLOAD_SIZE)
    {
      return -EMSGSIZE;
    }

  do
    {
      if (!bk7258_ota_rpmsg_ready())
        {
          return -ENOTCONN;
        }

      ret = rpmsg_trysend(&priv->ept, msg,
                          bk7258_ota_rpmsg_size(payload));
      if (ret >= 0)
        {
          return OK;
        }
      if (ret != -ENOMEM && ret != -EAGAIN)
        {
          return ret;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  return -ETIMEDOUT;
}

static void bk7258_ota_rpmsg_header(
  struct bk7258_ota_rpmsg_header_s *header, uint16_t command,
  uint32_t session)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  memset(header, 0, sizeof(*header));
  header->magic = BK7258_OTA_RPMSG_MAGIC;
  header->version = BK7258_OTA_RPMSG_VERSION;
  header->command = command;
  header->generation = control->generation;
  header->session = session;
}

static bool bk7258_ota_rpmsg_valid(
  const struct bk7258_ota_rpmsg_message_s *msg, size_t len)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  size_t payload;

  if (msg == NULL || len < sizeof(msg->header) ||
      len > sizeof(*msg))
    {
      return false;
    }

  payload = len - sizeof(msg->header);
  if (msg->header.magic != BK7258_OTA_RPMSG_MAGIC ||
      msg->header.version != BK7258_OTA_RPMSG_VERSION ||
      msg->header.generation == 0u ||
      msg->header.generation != control->generation ||
      msg->header.session == 0u)
    {
      return false;
    }

  switch (msg->header.command)
    {
      case BK7258_OTA_RPMSG_START:
        return payload == sizeof(struct bk7258_ota_manifest_s) &&
               msg->header.length == payload;

      case BK7258_OTA_RPMSG_DATA:
        return payload == msg->header.length &&
               payload <= BK7258_OTA_RPMSG_PAYLOAD_SIZE;

      case BK7258_OTA_RPMSG_APPLY_FILE:
        return payload == msg->header.length && payload >= 2u &&
               payload <= BK7258_OTA_CONTROL_PATH_SIZE;

      case BK7258_OTA_RPMSG_APPLY_HTTP:
        return payload == msg->header.length && payload >= 3u &&
               payload <= BK7258_OTA_CONTROL_URL_SIZE +
                          BK7258_OTA_CONTROL_CA_SIZE;

      case BK7258_OTA_RPMSG_CONTROL_REPLY:
        return payload == msg->header.length &&
               (payload == 0u ||
                payload == sizeof(struct bk7258_ota_manager_status_s));

      case BK7258_OTA_RPMSG_READ:
      case BK7258_OTA_RPMSG_PROGRESS:
      case BK7258_OTA_RPMSG_CANCEL:
      case BK7258_OTA_RPMSG_COMPLETE:
      case BK7258_OTA_RPMSG_MANAGER_STATUS:
      case BK7258_OTA_RPMSG_MANAGER_CANCEL:
        return payload == 0u;

      default:
        return false;
    }
}

#ifdef CONFIG_BK7258_AP_CORE

static int bk7258_ota_rpmsg_control_reply(uint32_t session, int status,
                                          const void *payload,
                                          uint32_t length, bool blocking)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;
  struct bk7258_ota_rpmsg_message_s reply;
  int ret;

  if ((payload == NULL) != (length == 0u) ||
      length > sizeof(reply.payload))
    {
      return -EINVAL;
    }

  memset(&reply, 0, sizeof(reply));
  bk7258_ota_rpmsg_header(&reply.header,
                          BK7258_OTA_RPMSG_CONTROL_REPLY, session);
  reply.header.status = status;
  reply.header.length = length;
  if (length > 0u)
    {
      memcpy(reply.payload, payload, length);
    }

  if (blocking)
    {
      return bk7258_ota_rpmsg_send(&reply, length);
    }

  ret = rpmsg_trysend(&priv->ept, &reply, bk7258_ota_rpmsg_size(length));
  return ret < 0 ? ret : OK;
}

#if defined(CONFIG_BK7258_OTA_SOURCE_FILE) || \
    defined(CONFIG_BK7258_OTA_SOURCE_HTTP)
static int bk7258_ota_rpmsg_control_worker(int argc, char *argv[])
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;

  (void)argc;
  (void)argv;
  for (;;)
    {
      uint16_t command;
      uint32_t timeout_ms;
      char location[BK7258_OTA_CONTROL_URL_SIZE];
      char ca_path[BK7258_OTA_CONTROL_CA_SIZE];
      bool manager_called = false;
      int ret = -ENOSYS;

      if (nxsem_wait_uninterruptible(&priv->control_sem) < 0)
        {
          continue;
        }

      command = priv->control_command;
      timeout_ms = priv->control_timeout_ms;
      memcpy(location, priv->control_location, sizeof(location));
      memcpy(ca_path, priv->control_ca_path, sizeof(ca_path));
      __asm volatile ("dmb sy" ::: "memory");

#ifdef CONFIG_BK7258_OTA_SOURCE_FILE
      if (command == BK7258_OTA_RPMSG_APPLY_FILE)
        {
          struct bk7258_ota_file_source_s source;

          ret = bk7258_ota_file_source_initialize(&source, location);
          if (ret == 0)
            {
              manager_called = true;
              ret = bk7258_ota_manager_apply(
                      bk7258_ota_file_source_ops(), &source, timeout_ms);
            }
        }
#endif

#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP
      if (command == BK7258_OTA_RPMSG_APPLY_HTTP)
        {
          struct bk7258_ota_http_source_s source = {0};

          ret = bk7258_ota_http_source_initialize(
                  &source, location, ca_path);
          if (ret == 0)
            {
              manager_called = true;
              ret = bk7258_ota_manager_apply(
                      bk7258_ota_http_source_ops(), &source, timeout_ms);
            }
        }
#endif

      if (ret < 0 && !manager_called)
        {
          (void)bk7258_ota_manager_report_failure(ret);
        }
      __atomic_store_n(&priv->control_busy, false, __ATOMIC_RELEASE);
    }

  return 0;
}
#endif

static int bk7258_ota_rpmsg_send_cancel(uint32_t session)
{
  struct bk7258_ota_rpmsg_message_s msg;

  bk7258_ota_rpmsg_header(&msg.header, BK7258_OTA_RPMSG_CANCEL, session);
  return bk7258_ota_rpmsg_send(&msg, 0u);
}

static int bk7258_ota_rpmsg_serve_read(
  struct bk7258_ota_rpmsg_dev_s *priv,
  const struct bk7258_ota_rpmsg_header_s *request)
{
  struct bk7258_ota_rpmsg_message_s reply;
  int ret;

  bk7258_ota_rpmsg_header(&reply.header, BK7258_OTA_RPMSG_DATA,
                          request->session);
  reply.header.sequence = request->sequence;
  reply.header.image = request->image;
  reply.header.offset = request->offset;
  if (request->image > BK7258_OTA_IMAGE_AP || request->length == 0u ||
      request->length > sizeof(reply.payload) || priv->source == NULL)
    {
      reply.header.status = -EINVAL;
      return bk7258_ota_rpmsg_send(&reply, 0u);
    }

  ret = priv->source->read_at(priv->source_context,
                              (enum bk7258_ota_image_e)request->image,
                              request->offset, reply.payload,
                              request->length);
  if (ret != 0)
    {
      reply.header.status = ret < 0 ? ret : -EIO;
      return bk7258_ota_rpmsg_send(&reply, 0u);
    }

  reply.header.length = request->length;
  return bk7258_ota_rpmsg_send(&reply, request->length);
}

int bk7258_ota_rpmsg_stage(const struct bk7258_ota_source_ops_s *source,
                           void *context, uint32_t timeout_ms)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;
  struct bk7258_ota_rpmsg_message_s start_msg;
  struct bk7258_ota_manifest_s manifest;
  clock_t started;
  clock_t limit;
  bool opened = false;
  int ret;

  if (source == NULL || source->open == NULL || source->read_at == NULL ||
      timeout_ms == 0u)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->session_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!bk7258_ota_rpmsg_ready())
    {
      ret = -ENOTCONN;
      goto out_source;
    }

  memset(&manifest, 0, sizeof(manifest));
  opened = true;
  ret = source->open(context, &manifest);
  if (ret != 0)
    {
      ret = ret < 0 ? ret : -EIO;
      goto out_source;
    }

  if (++priv->next_session == 0u)
    {
      priv->next_session++;
    }

  bk7258_ota_rpmsg_flush(&priv->event_sem);
  priv->source = source;
  priv->source_context = context;
  priv->cancel_requested = false;
  priv->read_pending = false;
  priv->progress_pending = false;
  priv->complete_pending = false;
  priv->shared_progress_sequence = 0u;
  __atomic_store_n(&priv->stage_active, true, __ATOMIC_RELEASE);

  memset(&start_msg, 0, sizeof(start_msg));
  bk7258_ota_rpmsg_header(&start_msg.header, BK7258_OTA_RPMSG_START,
                          priv->next_session);
  start_msg.header.length = sizeof(manifest);
  memcpy(start_msg.payload, &manifest, sizeof(manifest));
  ret = bk7258_ota_rpmsg_send(&start_msg, sizeof(manifest));
  if (ret < 0)
    {
      goto out_active;
    }

  started = clock_systime_ticks();
  limit = MSEC2TICK(timeout_ms);
  for (;;)
    {
      struct bk7258_ota_rpmsg_header_s read_request;
      struct bk7258_ota_progress_s progress;
      bool have_read;
      bool have_progress;
      bool have_complete;
      irqstate_t flags;
      clock_t elapsed = clock_systime_ticks() - started;

      if (elapsed >= limit)
        {
          ret = -ETIMEDOUT;
          (void)bk7258_ota_rpmsg_send_cancel(priv->next_session);
          break;
        }

      ret = nxsem_tickwait_uninterruptible(&priv->event_sem,
                                            limit - elapsed);
      if (ret < 0)
        {
          (void)bk7258_ota_rpmsg_send_cancel(priv->next_session);
          break;
        }

      flags = spin_lock_irqsave(&priv->lock);
      have_read = priv->read_pending;
      have_progress = priv->progress_pending;
      have_complete = priv->complete_pending;
      if (have_read)
        {
          read_request = priv->read_request;
          priv->read_pending = false;
        }
      if (have_progress)
        {
          progress = priv->progress;
          priv->progress_pending = false;
        }
      if (have_complete)
        {
          priv->complete_pending = false;
        }
      spin_unlock_irqrestore(&priv->lock, flags);

      bk7258_ota_rpmsg_sync_progress(priv);

      if (have_read)
        {
          ret = bk7258_ota_rpmsg_serve_read(priv, &read_request);
          if (ret < 0)
            {
              (void)bk7258_ota_rpmsg_send_cancel(priv->next_session);
              break;
            }
        }

      if (have_progress && source->checkpoint != NULL)
        {
          ret = source->checkpoint(context, &progress);
          if (ret < 0)
            {
              __atomic_store_n(&priv->cancel_requested, true,
                               __ATOMIC_RELEASE);
              (void)bk7258_ota_rpmsg_send_cancel(priv->next_session);
            }
        }

      if (__atomic_exchange_n(&priv->cancel_requested, false,
                              __ATOMIC_ACQ_REL))
        {
          ret = -ECANCELED;
          (void)bk7258_ota_rpmsg_send_cancel(priv->next_session);
        }

      if (have_complete && priv->complete_session == priv->next_session)
        {
          ret = priv->complete_status;
          break;
        }
    }

out_active:
  __atomic_store_n(&priv->stage_active, false, __ATOMIC_RELEASE);
  priv->source = NULL;
  priv->source_context = NULL;
out_source:
  if (ret < 0 && opened && source->cancel != NULL)
    {
      (void)source->cancel(context);
    }
  if (source->close != NULL)
    {
      source->close(context);
    }
  nxmutex_unlock(&priv->session_lock);
  return ret;
}

int bk7258_ota_rpmsg_cancel(void)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;

  if (!__atomic_load_n(&priv->stage_active, __ATOMIC_ACQUIRE))
    {
      return -ENOENT;
    }

  __atomic_store_n(&priv->cancel_requested, true, __ATOMIC_RELEASE);
  (void)nxsem_post(&priv->event_sem);
  return OK;
}

#else /* CONFIG_BK7258_AP_CORE */

struct bk7258_ota_rpmsg_source_s
{
  struct bk7258_ota_rpmsg_dev_s *dev;
  uint32_t session;
};

static int bk7258_ota_rpmsg_source_open(
  void *context, struct bk7258_ota_manifest_s *manifest)
{
  struct bk7258_ota_rpmsg_source_s *source = context;

  memcpy(manifest, &source->dev->manifest, sizeof(*manifest));
  return OK;
}

static int bk7258_ota_rpmsg_source_read(
  void *context, enum bk7258_ota_image_e image, uint32_t offset,
  uint8_t *buffer, size_t nbytes)
{
  struct bk7258_ota_rpmsg_source_s *source = context;
  struct bk7258_ota_rpmsg_dev_s *priv = source->dev;
  size_t done = 0u;

  while (done < nbytes)
    {
      struct bk7258_ota_rpmsg_message_s request;
      uint32_t count = nbytes - done;
      int ret;

      if (__atomic_load_n(&priv->cancel_requested, __ATOMIC_ACQUIRE))
        {
          return -ECANCELED;
        }
      if (count > BK7258_OTA_RPMSG_PAYLOAD_SIZE)
        {
          count = BK7258_OTA_RPMSG_PAYLOAD_SIZE;
        }
      if (++priv->next_sequence == 0u)
        {
          priv->next_sequence++;
        }

      bk7258_ota_rpmsg_flush(&priv->data_sem);
      priv->waiting_sequence = priv->next_sequence;
      priv->data_valid = false;
      priv->data_status = -EINPROGRESS;
      bk7258_ota_rpmsg_header(&request.header, BK7258_OTA_RPMSG_READ,
                              source->session);
      request.header.sequence = priv->next_sequence;
      request.header.image = image;
      request.header.offset = offset + done;
      request.header.length = count;
      ret = bk7258_ota_rpmsg_send(&request, 0u);
      if (ret < 0)
        {
          return ret;
        }

      ret = nxsem_tickwait_uninterruptible(
              &priv->data_sem,
              MSEC2TICK(CONFIG_BK7258_OTA_RPMSG_READ_TIMEOUT_MS));
      if (ret < 0)
        {
          return ret;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (__atomic_load_n(&priv->cancel_requested, __ATOMIC_ACQUIRE))
        {
          return -ECANCELED;
        }
      if (!priv->data_valid || priv->data_status < 0 ||
          priv->data_length != count)
        {
          return priv->data_status < 0 ? priv->data_status : -ESTALE;
        }

      memcpy(buffer + done, priv->data, count);
      done += count;
    }

  return OK;
}

static int bk7258_ota_rpmsg_source_checkpoint(
  void *context, const struct bk7258_ota_progress_s *progress)
{
  struct bk7258_ota_rpmsg_source_s *source = context;
  struct bk7258_ota_rpmsg_dev_s *priv = source->dev;

  if (__atomic_load_n(&priv->cancel_requested, __ATOMIC_ACQUIRE))
    {
      return -ECANCELED;
    }

  bk7258_ota_rpmsg_publish_progress(source->session, progress);
  return OK;
}

static int bk7258_ota_rpmsg_source_cancel(void *context)
{
  (void)context;
  return OK;
}

static void bk7258_ota_rpmsg_source_close(void *context)
{
  (void)context;
}

static const struct bk7258_ota_source_ops_s g_bk7258_ota_proxy_ops =
{
  .open = bk7258_ota_rpmsg_source_open,
  .read_at = bk7258_ota_rpmsg_source_read,
  .checkpoint = bk7258_ota_rpmsg_source_checkpoint,
  .cancel = bk7258_ota_rpmsg_source_cancel,
  .close = bk7258_ota_rpmsg_source_close,
};

static int bk7258_ota_rpmsg_control_request(
  uint16_t command, const void *payload, uint32_t length,
  uint32_t operation_timeout_ms,
  struct bk7258_ota_manager_status_s *manager_status)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;
  struct bk7258_ota_rpmsg_message_s request;
  uint32_t wait_ms;
  irqstate_t flags;
  int ret;

  if ((payload == NULL) != (length == 0u) ||
      length > sizeof(request.payload) || operation_timeout_ms == 0u)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->control_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!bk7258_ota_rpmsg_ready())
    {
      ret = -ENOTCONN;
      goto out_unlock;
    }

  if (++priv->next_control_session == 0u)
    {
      priv->next_control_session++;
    }

  bk7258_ota_rpmsg_flush(&priv->control_sem);
  flags = spin_lock_irqsave(&priv->lock);
  priv->waiting_control_session = priv->next_control_session;
  priv->control_reply_valid = false;
  priv->control_reply_length = 0u;
  priv->control_reply_status = -EINPROGRESS;
  spin_unlock_irqrestore(&priv->lock, flags);

  memset(&request, 0, sizeof(request));
  bk7258_ota_rpmsg_header(&request.header, command,
                          priv->next_control_session);
  request.header.offset = operation_timeout_ms;
  request.header.length = length;
  if (length > 0u)
    {
      memcpy(request.payload, payload, length);
    }

  ret = bk7258_ota_rpmsg_send(&request, length);
  if (ret < 0)
    {
      goto out_clear;
    }

  if (command == BK7258_OTA_RPMSG_APPLY_FILE ||
      command == BK7258_OTA_RPMSG_APPLY_HTTP)
    {
      wait_ms = BK7258_OTA_RPMSG_CONTROL_ACK_MS;
    }
  else
    {
      wait_ms = operation_timeout_ms > UINT32_MAX - 5000u ?
                UINT32_MAX : operation_timeout_ms + 5000u;
    }
  ret = nxsem_tickwait_uninterruptible(&priv->control_sem,
                                        MSEC2TICK(wait_ms));
  if (ret < 0)
    {
      goto out_clear;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->control_reply_valid)
    {
      ret = -ESTALE;
    }
  else
    {
      ret = priv->control_reply_status;
      if (manager_status != NULL)
        {
          if (priv->control_reply_length != sizeof(*manager_status))
            {
              ret = -EPROTO;
            }
          else
            {
              memcpy(manager_status, priv->control_reply,
                     sizeof(*manager_status));
            }
        }
      else if (priv->control_reply_length != 0u)
        {
          ret = -EPROTO;
        }
    }
  spin_unlock_irqrestore(&priv->lock, flags);

out_clear:
  flags = spin_lock_irqsave(&priv->lock);
  priv->waiting_control_session = 0u;
  priv->control_reply_valid = false;
  spin_unlock_irqrestore(&priv->lock, flags);
out_unlock:
  nxmutex_unlock(&priv->control_lock);
  return ret;
}

int bk7258_ota_rpmsg_apply_file(const char *root, uint32_t timeout_ms)
{
  size_t length;

  if (root == NULL || timeout_ms == 0u)
    {
      return -EINVAL;
    }

  length = strnlen(root, BK7258_OTA_CONTROL_PATH_SIZE);
  if (length == 0u || length >= BK7258_OTA_CONTROL_PATH_SIZE)
    {
      return -ENAMETOOLONG;
    }

  return bk7258_ota_rpmsg_control_request(
           BK7258_OTA_RPMSG_APPLY_FILE, root, length + 1u,
           timeout_ms, NULL);
}

int bk7258_ota_rpmsg_apply_http(const char *catalog_url,
                                const char *ca_path,
                                uint32_t timeout_ms)
{
  uint8_t payload[BK7258_OTA_CONTROL_URL_SIZE +
                  BK7258_OTA_CONTROL_CA_SIZE];
  size_t url_length;
  size_t ca_length;

  if (catalog_url == NULL || ca_path == NULL || timeout_ms == 0u)
    {
      return -EINVAL;
    }

  url_length = strnlen(catalog_url, BK7258_OTA_CONTROL_URL_SIZE);
  ca_length = strnlen(ca_path, BK7258_OTA_CONTROL_CA_SIZE);
  if (url_length == 0u || url_length >= BK7258_OTA_CONTROL_URL_SIZE ||
      ca_length >= BK7258_OTA_CONTROL_CA_SIZE)
    {
      return -ENAMETOOLONG;
    }

  memcpy(payload, catalog_url, url_length + 1u);
  memcpy(payload + url_length + 1u, ca_path, ca_length + 1u);
  return bk7258_ota_rpmsg_control_request(
           BK7258_OTA_RPMSG_APPLY_HTTP, payload,
           url_length + ca_length + 2u, timeout_ms, NULL);
}

int bk7258_ota_rpmsg_manager_status(
  struct bk7258_ota_manager_status_s *status, uint32_t timeout_ms)
{
  if (status == NULL)
    {
      return -EINVAL;
    }

  return bk7258_ota_rpmsg_control_request(
           BK7258_OTA_RPMSG_MANAGER_STATUS, NULL, 0u,
           timeout_ms, status);
}

int bk7258_ota_rpmsg_manager_cancel(uint32_t timeout_ms)
{
  return bk7258_ota_rpmsg_control_request(
           BK7258_OTA_RPMSG_MANAGER_CANCEL, NULL, 0u,
           timeout_ms, NULL);
}

static int bk7258_ota_rpmsg_worker(int argc, char *argv[])
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;

  (void)argc;
  (void)argv;
  for (;;)
    {
      struct bk7258_ota_rpmsg_source_s source;
      struct bk7258_ota_rpmsg_message_s complete;
      int ret;

      if (nxsem_wait_uninterruptible(&priv->worker_sem) < 0)
        {
          continue;
        }

      source.dev = priv;
      source.session = priv->active_session;
      ret = bk7258_ota_stage_pair(&g_bk7258_ota_proxy_ops, &source);
      bk7258_ota_rpmsg_header(&complete.header,
                              BK7258_OTA_RPMSG_COMPLETE,
                              source.session);
      complete.header.status = ret;
      (void)bk7258_ota_rpmsg_send(&complete, 0u);
      __atomic_store_n(&priv->stage_busy, false, __ATOMIC_RELEASE);
    }

  return 0;
}

#endif /* CONFIG_BK7258_AP_CORE */

static int bk7258_ota_rpmsg_ept_cb(FAR struct rpmsg_endpoint *ept,
                                    FAR void *data, size_t len,
                                    uint32_t src, FAR void *priv_)
{
  struct bk7258_ota_rpmsg_dev_s *priv = priv_;
  struct bk7258_ota_rpmsg_message_s *msg = data;
  irqstate_t flags;

  (void)ept;
  (void)src;
  if (!bk7258_ota_rpmsg_valid(msg, len))
    {
      return -EPROTO;
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (msg->header.command == BK7258_OTA_RPMSG_MANAGER_STATUS)
    {
      struct bk7258_ota_manager_status_s status;
      int ret;

#ifdef CONFIG_BK7258_OTA_MANAGER
      bk7258_ota_rpmsg_sync_progress(priv);
      ret = bk7258_ota_manager_get_status(&status);
#else
      memset(&status, 0, sizeof(status));
      ret = -ENOSYS;
#endif
      return bk7258_ota_rpmsg_control_reply(
               msg->header.session, ret,
               ret == 0 ? &status : NULL,
               ret == 0 ? sizeof(status) : 0u, false);
    }

  if (msg->header.command == BK7258_OTA_RPMSG_MANAGER_CANCEL)
    {
      int ret;

#ifdef CONFIG_BK7258_OTA_MANAGER
      ret = bk7258_ota_manager_cancel();
#else
      ret = -ENOSYS;
#endif
      return bk7258_ota_rpmsg_control_reply(
               msg->header.session, ret, NULL, 0u, false);
    }

  if (msg->header.command == BK7258_OTA_RPMSG_APPLY_FILE ||
      msg->header.command == BK7258_OTA_RPMSG_APPLY_HTTP)
    {
#if defined(CONFIG_BK7258_OTA_SOURCE_FILE) || \
    defined(CONFIG_BK7258_OTA_SOURCE_HTTP)
      bool expected = false;
      const char *location = (const char *)msg->payload;
      const char *terminator;
      const char *ca_path = "";
      size_t location_size = msg->header.length;
      size_t ca_size = 1u;
      bool supported = false;

#ifdef CONFIG_BK7258_OTA_SOURCE_FILE
      supported = msg->header.command == BK7258_OTA_RPMSG_APPLY_FILE;
#endif
#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP
      supported = supported ||
                  msg->header.command == BK7258_OTA_RPMSG_APPLY_HTTP;
#endif

      if (!supported || msg->header.offset < 60000u ||
          msg->header.offset > 3600000u ||
          location[0] == '\0')
        {
          return bk7258_ota_rpmsg_control_reply(
                   msg->header.session, -EINVAL, NULL, 0u, false);
        }

      if (msg->header.command == BK7258_OTA_RPMSG_APPLY_FILE)
        {
          if (memchr(location, '\0', msg->header.length) !=
              location + msg->header.length - 1u ||
              msg->header.length > BK7258_OTA_CONTROL_PATH_SIZE)
            {
              return bk7258_ota_rpmsg_control_reply(
                       msg->header.session, -EINVAL, NULL, 0u, false);
            }
        }
      else
        {
          terminator = memchr(location, '\0', msg->header.length);
          if (terminator == NULL || terminator == location ||
              terminator == location + msg->header.length - 1u)
            {
              return bk7258_ota_rpmsg_control_reply(
                       msg->header.session, -EINVAL, NULL, 0u, false);
            }

          location_size = (size_t)(terminator - location) + 1u;
          ca_path = terminator + 1;
          ca_size = msg->header.length - location_size;
          if (location_size > BK7258_OTA_CONTROL_URL_SIZE ||
              ca_size > BK7258_OTA_CONTROL_CA_SIZE ||
              memchr(ca_path, '\0', ca_size) != ca_path + ca_size - 1u)
            {
              return bk7258_ota_rpmsg_control_reply(
                       msg->header.session, -EINVAL, NULL, 0u, false);
            }
        }

      if (!__atomic_compare_exchange_n(&priv->control_busy, &expected, true,
                                       false, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE))
        {
          return bk7258_ota_rpmsg_control_reply(
                   msg->header.session, -EBUSY, NULL, 0u, false);
        }

      memset(priv->control_location, 0, sizeof(priv->control_location));
      memset(priv->control_ca_path, 0, sizeof(priv->control_ca_path));
      memcpy(priv->control_location, location, location_size);
      memcpy(priv->control_ca_path, ca_path, ca_size);
      priv->control_command = msg->header.command;
      priv->control_timeout_ms = msg->header.offset;
      __asm volatile ("dmb sy" ::: "memory");
      {
        int ret = nxsem_post(&priv->control_sem);

        if (ret < 0)
          {
            __atomic_store_n(&priv->control_busy, false,
                             __ATOMIC_RELEASE);
            return bk7258_ota_rpmsg_control_reply(
                     msg->header.session, ret, NULL, 0u, false);
          }
      }
      return bk7258_ota_rpmsg_control_reply(
               msg->header.session, 0, NULL, 0u, false);
#else
      return bk7258_ota_rpmsg_control_reply(
               msg->header.session, -ENOSYS, NULL, 0u, false);
#endif
    }

  if (!__atomic_load_n(&priv->stage_active, __ATOMIC_ACQUIRE) ||
      msg->header.session != priv->next_session)
    {
      return -ESTALE;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (msg->header.command == BK7258_OTA_RPMSG_READ)
    {
      if (priv->read_pending)
        {
          spin_unlock_irqrestore(&priv->lock, flags);
          return -EBUSY;
        }

      priv->read_request = msg->header;
      priv->read_pending = true;
    }
  else if (msg->header.command == BK7258_OTA_RPMSG_PROGRESS)
    {
      priv->progress.phase =
        (enum bk7258_ota_phase_e)msg->header.status;
      priv->progress.image =
        (enum bk7258_ota_image_e)msg->header.image;
      priv->progress.completed = msg->header.offset;
      priv->progress.total = msg->header.length;
      priv->progress_pending = true;
    }
  else if (msg->header.command == BK7258_OTA_RPMSG_COMPLETE)
    {
      priv->complete_session = msg->header.session;
      priv->complete_status = msg->header.status;
      priv->complete_pending = true;
    }
  else
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ENOMSG;
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return nxsem_post(&priv->event_sem);
#else
  if (msg->header.command == BK7258_OTA_RPMSG_CONTROL_REPLY)
    {
      flags = spin_lock_irqsave(&priv->lock);
      if (priv->waiting_control_session == 0u ||
          msg->header.session != priv->waiting_control_session)
        {
          spin_unlock_irqrestore(&priv->lock, flags);
          return -ESTALE;
        }

      priv->control_reply_status = msg->header.status;
      priv->control_reply_length = msg->header.length;
      if (msg->header.length > 0u)
        {
          memcpy(priv->control_reply, msg->payload, msg->header.length);
        }
      priv->control_reply_valid = true;
      spin_unlock_irqrestore(&priv->lock, flags);
      return nxsem_post(&priv->control_sem);
    }

  if (msg->header.command == BK7258_OTA_RPMSG_START)
    {
      bool expected = false;
      struct bk7258_ota_rpmsg_message_s complete;

      if (msg->header.length != sizeof(priv->manifest))
        {
          return -EINVAL;
        }
      if (!__atomic_compare_exchange_n(&priv->stage_busy, &expected, true,
                                       false, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE))
        {
          bk7258_ota_rpmsg_header(&complete.header,
                                  BK7258_OTA_RPMSG_COMPLETE,
                                  msg->header.session);
          complete.header.status = -EBUSY;
          return rpmsg_trysend(&priv->ept, &complete,
                               sizeof(complete.header)) < 0 ? -EAGAIN : OK;
        }

      memcpy(&priv->manifest, msg->payload, sizeof(priv->manifest));
      priv->active_session = msg->header.session;
      priv->cancel_requested = false;
      __asm volatile ("dmb sy" ::: "memory");
      return nxsem_post(&priv->worker_sem);
    }

  if (msg->header.command == BK7258_OTA_RPMSG_CANCEL &&
      msg->header.session == priv->active_session)
    {
      __atomic_store_n(&priv->cancel_requested, true, __ATOMIC_RELEASE);
      (void)nxsem_post(&priv->data_sem);
      return OK;
    }

  if (msg->header.command == BK7258_OTA_RPMSG_DATA &&
      msg->header.session == priv->active_session &&
      msg->header.sequence == priv->waiting_sequence)
    {
      priv->data_status = msg->header.status;
      priv->data_length = msg->header.length;
      if (msg->header.status >= 0 &&
          msg->header.length <= sizeof(priv->data))
        {
          memcpy(priv->data, msg->payload, msg->header.length);
          priv->data_valid = true;
        }
      else
        {
          priv->data_valid = false;
        }

      __asm volatile ("dmb sy" ::: "memory");
      return nxsem_post(&priv->data_sem);
    }

  return -ENOMSG;
#endif
}

static void bk7258_ota_rpmsg_device_created(FAR struct rpmsg_device *rdev,
                                             FAR void *priv_)
{
  struct bk7258_ota_rpmsg_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL ||
      strcmp(cpuname, BK7258_OTA_RPMSG_REMOTE_NAME) != 0)
    {
      return;
    }

#ifdef CONFIG_BK7258_AP_CORE
  priv->ept.priv = priv;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, BK7258_OTA_RPMSG_EPT_NAME,
    RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, bk7258_ota_rpmsg_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
#else
  priv->connection_error = OK;
#endif
}

#ifndef CONFIG_BK7258_AP_CORE
static bool bk7258_ota_rpmsg_ns_match(FAR struct rpmsg_device *rdev,
                                      FAR void *priv_, FAR const char *name,
                                      uint32_t dest)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  (void)dest;
  return cpuname != NULL &&
         strcmp(cpuname, BK7258_OTA_RPMSG_REMOTE_NAME) == 0 &&
         strcmp(name, BK7258_OTA_RPMSG_EPT_NAME) == 0;
}

static void bk7258_ota_rpmsg_ns_bind(FAR struct rpmsg_device *rdev,
                                     FAR void *priv_, FAR const char *name,
                                     uint32_t dest)
{
  struct bk7258_ota_rpmsg_dev_s *priv = priv_;

  priv->ept.priv = priv;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, name, RPMSG_ADDR_ANY, dest,
    bk7258_ota_rpmsg_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
      bk7258_rptun_mark_connected();
    }
}
#endif

static void bk7258_ota_rpmsg_device_destroy(FAR struct rpmsg_device *rdev,
                                             FAR void *priv_)
{
  struct bk7258_ota_rpmsg_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL ||
      strcmp(cpuname, BK7258_OTA_RPMSG_REMOTE_NAME) != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  priv->connection_error = -ENOTCONN;
#ifdef CONFIG_BK7258_AP_CORE
  priv->complete_status = -ENOTCONN;
  priv->complete_session = priv->next_session;
  priv->complete_pending = true;
  (void)nxsem_post(&priv->event_sem);
#else
  __atomic_store_n(&priv->cancel_requested, true, __ATOMIC_RELEASE);
  priv->control_reply_status = -ENOTCONN;
  priv->control_reply_length = 0u;
  priv->control_reply_valid = true;
  (void)nxsem_post(&priv->data_sem);
  (void)nxsem_post(&priv->control_sem);
#endif
  if (priv->ept.rdev != NULL)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

int bk7258_ota_rpmsg_initialize(void)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;
  bool expected = false;
  bool registered = false;
  bool first_sem = false;
  bool second_sem = false;
#ifndef CONFIG_BK7258_AP_CORE
  bool third_sem = false;
#endif
  int ret;

  if (!__atomic_compare_exchange_n(&priv->initialized, &expected, true,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return OK;
    }

#ifdef CONFIG_BK7258_AP_CORE
  ret = nxsem_init(&priv->event_sem, 0, 0);
#else
  ret = nxsem_init(&priv->worker_sem, 0, 0);
#endif
  if (ret >= 0)
    {
      first_sem = true;
    }

#ifndef CONFIG_BK7258_AP_CORE
  if (ret >= 0)
    {
      ret = nxsem_init(&priv->data_sem, 0, 0);
      second_sem = ret >= 0;
    }
#endif

#if defined(CONFIG_BK7258_AP_CORE) && \
    (defined(CONFIG_BK7258_OTA_SOURCE_FILE) || \
     defined(CONFIG_BK7258_OTA_SOURCE_HTTP))
  if (ret >= 0)
    {
      ret = nxsem_init(&priv->control_sem, 0, 0);
      second_sem = ret >= 0;
    }
#elif !defined(CONFIG_BK7258_AP_CORE)
  if (ret >= 0)
    {
      ret = nxsem_init(&priv->control_sem, 0, 0);
      third_sem = ret >= 0;
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(priv,
                                    bk7258_ota_rpmsg_device_created,
                                    bk7258_ota_rpmsg_device_destroy,
#ifdef CONFIG_BK7258_AP_CORE
                                    NULL, NULL);
#else
                                    bk7258_ota_rpmsg_ns_match,
                                    bk7258_ota_rpmsg_ns_bind);
#endif
      registered = ret >= 0;
    }

#if defined(CONFIG_BK7258_AP_CORE) && \
    (defined(CONFIG_BK7258_OTA_SOURCE_FILE) || \
     defined(CONFIG_BK7258_OTA_SOURCE_HTTP))
  if (ret >= 0)
    {
      pid_t pid;

      pid = kthread_create("bk7258-ota-apctl",
                           CONFIG_BK7258_OTA_RPMSG_PRIORITY,
                           CONFIG_BK7258_OTA_RPMSG_AP_STACKSIZE,
                           bk7258_ota_rpmsg_control_worker, NULL);
      ret = (int)pid;
      if (pid >= 0)
        {
#ifdef CONFIG_SMP
          cpu_set_t cpuset;

          CPU_ZERO(&cpuset);
          CPU_SET(0, &cpuset);
          ret = sched_setaffinity(pid, sizeof(cpuset), &cpuset);
          if (ret < 0)
            {
              kthread_delete(pid);
            }
#else
          ret = OK;
#endif
        }
    }
#elif !defined(CONFIG_BK7258_AP_CORE)
  if (ret >= 0)
    {
      ret = kthread_create("bk7258-ota-cp",
                           CONFIG_BK7258_OTA_RPMSG_PRIORITY,
                           CONFIG_BK7258_OTA_RPMSG_STACKSIZE,
                           bk7258_ota_rpmsg_worker, NULL);
      if (ret >= 0)
        {
          ret = OK;
        }
    }
#endif

  if (ret < 0)
    {
      if (registered)
        {
          rpmsg_unregister_callback(priv,
                                    bk7258_ota_rpmsg_device_created,
                                    bk7258_ota_rpmsg_device_destroy,
#ifdef CONFIG_BK7258_AP_CORE
                                    NULL, NULL);
#else
                                    bk7258_ota_rpmsg_ns_match,
                                    bk7258_ota_rpmsg_ns_bind);
#endif
        }
#ifndef CONFIG_BK7258_AP_CORE
      if (third_sem)
        {
          (void)nxsem_destroy(&priv->control_sem);
        }
      if (second_sem)
        {
          (void)nxsem_destroy(&priv->data_sem);
        }
#elif defined(CONFIG_BK7258_OTA_SOURCE_FILE) || \
      defined(CONFIG_BK7258_OTA_SOURCE_HTTP)
      if (second_sem)
        {
          (void)nxsem_destroy(&priv->control_sem);
        }
#endif
      if (first_sem)
        {
#ifdef CONFIG_BK7258_AP_CORE
          (void)nxsem_destroy(&priv->event_sem);
#else
          (void)nxsem_destroy(&priv->worker_sem);
#endif
        }

      memset(&priv->ept, 0, sizeof(priv->ept));
      __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
      priv->connection_error = ret;
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
    }

  return ret;
}

#endif /* CONFIG_BK7258_OTA_RPMSG */
