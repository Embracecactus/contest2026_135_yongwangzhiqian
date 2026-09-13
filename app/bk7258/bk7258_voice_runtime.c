/****************************************************************************
 * app/bk7258/bk7258_voice_runtime.c
 * SPDX-License-Identifier: Apache-2.0
 *
 * One AP owner for physical PTT, the authenticated companion and audio.
 ****************************************************************************/

#include <nuttx/config.h>
#include "bk7258_voice_runtime.h"
#ifdef CONFIG_BK7258_WIFI_VNET
#include <arch/chip/bk7258_wifi.h>
#endif
#include "bk7258_voice_button.h"
#ifdef CONFIG_BK7258_PRODUCT_KEYS
#include "bk7258_product_keys.h"
#endif
#if defined(CONFIG_BK7258_PRODUCT_KEYS) && defined(CONFIG_BK7258_PM_SOFT_OFF)
#include <arch/chip/bk7258_pm.h>
#ifdef CONFIG_BK7258_USBMODE
#include <arch/chip/bk7258_usbmode.h>
#endif
#include <unistd.h>
#define BKVOICE_RUNTIME_SOFT_OFF 1
#endif
#include "bk7258_voice_config.h"
#include "bk7258_voice_session.h"
#include "bk7258_voice_tls.h"
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
#include "bk7258_voice_wake_session.h"
#endif
#ifdef CONFIG_BK7258_OTA_MANAGER
#include <arch/chip/bk7258_active_image.h>
#include <arch/chip/bk7258_ota_catalog.h>
#include <arch/chip/bk7258_ota_manager.h>
#endif
#if defined(CONFIG_BK7258_OTA_MANAGER) && \
    defined(CONFIG_BK7258_OTA_SOURCE_HTTP) && \
    defined(CONFIG_BK7258_OTA_RPMSG) && \
    defined(CONFIG_BK7258_VOICE_OTA_PERSISTENCE)
#include "bk7258_voice_ota_flow.h"
#include "bk7258_voice_ota_store.h"
#include "bk7258_control_ota_request.h"
#include "bk7258_voice_ota_admission.h"
#include "bk7258_voice_ota_cancel.h"
#include <arch/chip/bk7258_ota_rpmsg.h>
#include <arch/chip/bk7258_ota_source_http.h>
#define BKVOICE_RUNTIME_OTA 1
#endif
#ifdef CONFIG_BK7258_HEALTH_SERVICE
#include "bk7258_health_protocol.h"
#include "bk7258_health_service.h"
#endif
#ifdef CONFIG_BK7258_PROVISION_GATT
#include "bk7258_provision_owner.h"
#include "bk7258_provision_identity.h"
#include "bk7258_provision_network.h"
#include "bk7258_provision_storage.h"
#include "bk7258_provision_settings.h"
#endif

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bk7258_cloud_runtime.h"
#ifdef CONFIG_BK7258_PREFERENCES
#include "bk7258_preferences.h"
#endif
#include <syslog.h>
#include <time.h>
#include <mbedtls/platform_util.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/spinlock.h>

#define BKVOICE_RX_SLOTS 4u
#define BKVOICE_IO_MS 3000u
#define BKVOICE_CONNECT_MS 12000u
#define BKVOICE_RX_IDLE_MS 300000u
#define BKVOICE_UPLOAD_MS 30000u
#ifdef BKVOICE_RUNTIME_SOFT_OFF
#define BKVOICE_SOFT_OFF_STATUS_POLL_MS 1000u
#endif
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
_Static_assert(sizeof(CONFIG_BK7258_VOICE_KWS_MODEL_PATH) > 1,
               "wake runtime requires a model path");
_Static_assert(sizeof(CONFIG_BK7258_VOICE_KWS_MODEL_SHA256) == 65,
               "wake runtime requires a 64-character SHA-256");
#endif
#ifdef BKVOICE_RUNTIME_OTA
#define BKVOICE_OTA_CATALOG_URL_BYTES 256u
#define BKVOICE_OTA_JOIN_MS 5000u
#define BKVOICE_OTA_TARGET_APPROVAL_MS 5000u
#define BKVOICE_OTA_RESTORE_RETRY_MS 1000u
#define BKVOICE_OTA_TRIAL_POLL_MS 1000u

enum bkvoice_control_ota_state_e
{
  BKVOICE_CONTROL_OTA_IDLE = 0,
  BKVOICE_CONTROL_OTA_QUEUED,
  BKVOICE_CONTROL_OTA_ACTIVE,
  BKVOICE_CONTROL_OTA_TERMINAL,
};
#endif

struct bkvoice_runtime_s
{
  struct bkvoice_config_s config;
  struct bkcloud_runtime_s *cloud;
  uint64_t control_refresh_ms;
#ifdef CONFIG_BK7258_PROVISION_GATT
  struct bkprov_identity_s identity;
  bool identity_pending;
  bool identity_bind_pending;
  bool identity_bound;
  int identity_result;
  uint64_t provision_restore_ms;
  uint64_t configuration_restore_ms;
#endif
  struct bkvoice_tls_s tls;
  struct bkvoice_wss_s wss;
  struct bkvoice_session_s session;
  struct bkvoice_ptt_s *ptt;
  uint32_t boot_generation;
  sem_t *wake;
  struct rpmsg_endpoint button_endpoint;
  mutex_t endpoint_lock;
  spinlock_t button_lock;
  bool button_link;
  uint32_t button_epoch;
  uint32_t button_sequence;
  uint64_t button_received_ms;
  uint32_t button_pressed;
#ifdef CONFIG_BK7258_PRODUCT_KEYS
  struct bkvoice_product_keys_s product_keys;
#endif
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  volatile bool soft_off_pending;
  bool soft_off_accepted;
  bool soft_off_blockdev_held;
  int soft_off_failure;
  uint64_t soft_off_status_ms;
  volatile unsigned int soft_off_mutations;
#endif
  uint32_t owner_button_epoch;
  bool armed;
  bool pressed;
  uint32_t presses;
#ifdef CONFIG_BK7258_VOICE_HIL_TEST
  uint64_t test_until;
#endif
  int last_error;
  bool cleanup_pending;
  bool initialized;
  pthread_t receiver;
  sem_t receiver_done;
  bool receiver_joinable;
  volatile bool receiver_stop;
  struct bkvoice_gateway_frame_s rx[BKVOICE_RX_SLOTS];
  volatile uint32_t rx_head;
  volatile uint32_t rx_tail;
  uint8_t *upload;
  size_t upload_size;
  size_t uploaded;
  uint64_t upload_deadline;
  uint32_t status_connection_generation;
  uint32_t status_health_sequence;
#ifdef CONFIG_BK7258_OTA_MANAGER
  struct bk7258_mcuboot_version_s firmware_version;
  uint8_t firmware_root_sha256[BK7258_OTA_SHA256_SIZE];
  bool firmware_identity_valid;
#endif
#ifdef BKVOICE_RUNTIME_OTA
  pthread_t ota_worker;
  sem_t ota_done;
  sem_t ota_target_decision;
  uint32_t ota_request_sequence;
  uint8_t ota_manifest_sha256[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES];
  char ota_catalog_url[BKVOICE_OTA_CATALOG_URL_BYTES];
  int ota_result;
  uint8_t ota_report_phase;
  uint8_t ota_report_progress;
  volatile uint8_t ota_job_state;
  volatile bool ota_cancel_requested;
  volatile uint8_t ota_target_admission;
  bool ota_worker_joinable;
  bool ota_store_ready;
  bool ota_intent_present;
  bool ota_request_prepared;
  bool ota_target_valid;
  bool ota_target_commit_started;
  volatile int ota_target_admission_result;
  uint64_t ota_store_revision;
  uint64_t ota_restore_ms;
  uint64_t ota_trial_poll_ms;
  struct bkvoice_ota_intent_s ota_intent;
  struct bk7258_mcuboot_version_s ota_target_version;
  uint32_t ota_target_security_counter;
  struct bkcontrol_ota_request_s *ota_direct_request;
  struct bkcontrol_ota_status_s ota_direct_status;
  bool ota_direct;
  bool ota_direct_status_valid;
#endif
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
  struct bkvoice_wake_session_s *wake_session;
  int wake_result;
  bool wake_attempted;
#endif
};

static struct bkvoice_runtime_s g_runtime =
{
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .button_lock = SP_UNLOCKED,
};

static uint64_t bkvoice_now(void)
{
  return bkvoice_config_now_ms(NULL);
}

#ifdef BKVOICE_RUNTIME_SOFT_OFF
static int bkvoice_soft_off_progress(struct bkvoice_runtime_s *runtime);

static bool bkvoice_soft_off_pending(const struct bkvoice_runtime_s *runtime)
{
  return __atomic_load_n(&runtime->soft_off_pending, __ATOMIC_ACQUIRE);
}

#ifdef BKVOICE_RUNTIME_OTA
static bool bkvoice_soft_off_mutation_enter(struct bkvoice_runtime_s *runtime)
{
  if (bkvoice_soft_off_pending(runtime)) return false;
  __atomic_add_fetch(&runtime->soft_off_mutations, 1u, __ATOMIC_ACQ_REL);
  if (bkvoice_soft_off_pending(runtime))
    {
      __atomic_sub_fetch(&runtime->soft_off_mutations, 1u, __ATOMIC_RELEASE);
      return false;
    }
  return true;
}

static void bkvoice_soft_off_mutation_leave(struct bkvoice_runtime_s *runtime)
{
  __atomic_sub_fetch(&runtime->soft_off_mutations, 1u, __ATOMIC_RELEASE);
}
#endif

static int bkvoice_soft_off_admit(const struct bkvoice_runtime_s *runtime)
{
#ifdef BKVOICE_RUNTIME_OTA
  if (__atomic_load_n(&runtime->soft_off_mutations, __ATOMIC_ACQUIRE) != 0u)
    {
      return -EBUSY;
    }
  if (!runtime->ota_store_ready || runtime->ota_intent_present ||
      runtime->ota_direct_request != NULL || runtime->ota_worker_joinable ||
      runtime->ota_target_commit_started ||
      __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
        BKVOICE_OTA_JOB_EMPTY ||
      bkvoice_ota_target_admission_load(&runtime->ota_target_admission) !=
        BKVOICE_OTA_TARGET_IDLE)
    {
      return -EBUSY;
    }
#endif
#ifdef CONFIG_BK7258_OTA_MANAGER
  struct bk7258_ota_manager_status_s ota;
  int ret = bk7258_ota_manager_get_status(&ota);

  if (ret < 0)
    {
      return ret;
    }

  if (ota.state != BK7258_OTA_MANAGER_IDLE &&
      ota.state != BK7258_OTA_MANAGER_FAILED &&
      ota.state != BK7258_OTA_MANAGER_CANCELED)
    {
      return -EBUSY;
    }
#endif
#ifdef CONFIG_BK7258_PROVISION_GATT
  if (runtime->identity_pending || bkprov_owner_busy() ||
      bkprov_network_busy())
    {
      return -EBUSY;
    }
#endif
  return runtime->upload != NULL || runtime->cleanup_pending ? -EBUSY : 0;
}
#endif

#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
static bool bkvoice_wake_committed(const struct bkvoice_runtime_s *runtime)
{
  return runtime->cloud != NULL && runtime->config.initialized &&
         runtime->identity_bound && !runtime->identity_pending &&
         !bkprov_network_busy();
}

static int bkvoice_wake_prepare(struct bkvoice_runtime_s *runtime)
{
  const struct bkvoice_wake_session_config_s config =
  {
    .model_path = CONFIG_BK7258_VOICE_KWS_MODEL_PATH,
    .model_sha256_hex = CONFIG_BK7258_VOICE_KWS_MODEL_SHA256,
    .arena_bytes = CONFIG_BK7258_VOICE_KWS_ARENA_BYTES,
    .listener_stack_size = CONFIG_BK7258_VOICE_CAPTURE_STACKSIZE,
    .listener_join_timeout_ms = CONFIG_BK7258_VOICE_CAPTURE_JOIN_TIMEOUT_MS,
  };
  int ret;

  if (runtime->wake_session != NULL) return 0;
  if (runtime->wake_attempted) return runtime->wake_result;
  if (!bkvoice_wake_committed(runtime)) return -EAGAIN;
  ret = bkcloud_runtime_ready(runtime->cloud);
  if (ret <= 0) return ret < 0 ? ret : -EAGAIN;

  runtime->wake_attempted = true;
  ret = bkvoice_wake_session_open(&runtime->wake_session, &config,
                                  runtime->ptt, runtime->cloud,
                                  runtime->wake);
  runtime->wake_result = ret;
  if (ret < 0)
    {
      runtime->last_error = ret;
      syslog(LOG_WARNING, "BKVOICE WAKE unavailable=%d mic=0\n", ret);
    }
  return ret;
}

static int bkvoice_wake_close(struct bkvoice_runtime_s *runtime)
{
  int ret = bkvoice_wake_session_close(&runtime->wake_session);

  if (ret == 0)
    {
      runtime->wake_attempted = false;
      runtime->wake_result = 0;
    }
  return ret;
}
#endif

#ifdef CONFIG_BK7258_HEALTH_SERVICE
static uint8_t bkvoice_runtime_battery_state(uint32_t state)
{
  switch (state)
    {
      case BKHEALTH_BATTERY_UNKNOWN:
        return BKVOICE_COMPANION_BATTERY_STATE_UNKNOWN;
      case BKHEALTH_BATTERY_FAULT:
        return BKVOICE_COMPANION_BATTERY_STATE_FAULT;
      case BKHEALTH_BATTERY_IDLE:
        return BKVOICE_COMPANION_BATTERY_STATE_IDLE;
      case BKHEALTH_BATTERY_FULL:
        return BKVOICE_COMPANION_BATTERY_STATE_FULL;
      case BKHEALTH_BATTERY_CHARGING:
        return BKVOICE_COMPANION_BATTERY_STATE_CHARGING;
      case BKHEALTH_BATTERY_DISCHARGING:
        return BKVOICE_COMPANION_BATTERY_STATE_DISCHARGING;
      default:
        break;
    }
  return BKVOICE_COMPANION_BATTERY_UNKNOWN;
}

static uint8_t bkvoice_runtime_charging(uint8_t state)
{
  if (state == BKVOICE_COMPANION_BATTERY_STATE_CHARGING)
    {
      return 1;
    }

  if (state == BKVOICE_COMPANION_BATTERY_STATE_IDLE ||
      state == BKVOICE_COMPANION_BATTERY_STATE_FULL ||
      state == BKVOICE_COMPANION_BATTERY_STATE_DISCHARGING)
    {
      return 0;
    }

  return 2;
}
#endif

static int bkvoice_runtime_report_device_status(
  struct bkvoice_runtime_s *runtime)
{
  struct bkvoice_gateway_snapshot_s gateway;
  struct bkvoice_gateway_status_s status;
  uint32_t health_sequence = 0;

  if (!runtime->session.ready)
    {
      return 0;
    }

  memset(&gateway, 0, sizeof(gateway));
  bkvoice_gateway_snapshot(&runtime->session.gateway, &gateway);
  if (gateway.companion_state != BKVOICE_COMPANION_IDLE ||
      gateway.connection_generation == 0)
    {
      return 0;
    }

  memset(&status, 0, sizeof(status));
  status.battery_percent = BKVOICE_COMPANION_BATTERY_UNKNOWN;
  status.charging = 2;
  status.battery_state = BKVOICE_COMPANION_BATTERY_UNKNOWN;
  status.battery_voltage_mv = BKVOICE_COMPANION_BATTERY_MV_UNKNOWN;
  status.firmware_major = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_minor = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_revision = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_build = BKVOICE_COMPANION_FW_BUILD_UNKNOWN;

#ifdef CONFIG_BK7258_OTA_MANAGER
  if (runtime->firmware_identity_valid)
    {
      status.firmware_major = runtime->firmware_version.major;
      status.firmware_minor = runtime->firmware_version.minor;
      status.firmware_revision = runtime->firmware_version.revision;
      status.firmware_build = runtime->firmware_version.build;
      memcpy(status.firmware_root_sha256, runtime->firmware_root_sha256,
             sizeof(status.firmware_root_sha256));
      status.firmware_identity_valid = true;
    }
#endif

#ifdef CONFIG_BK7258_HEALTH_SERVICE
  {
    struct bk7258_health_service_snapshot_s health;

    if (bk7258_health_service_snapshot(&health) == 0)
      {
        health_sequence = health.sequence;
        if ((health.flags & BK7258_HEALTH_SNAPSHOT_BATTERY_STATE_VALID) != 0)
          {
            status.battery_state =
              bkvoice_runtime_battery_state(health.battery_state);
            status.charging =
              bkvoice_runtime_charging(status.battery_state);
          }

        if ((health.flags & BK7258_HEALTH_SNAPSHOT_BATTERY_VOLTAGE_VALID) != 0)
          {
            status.battery_voltage_mv = (uint32_t)health.battery_voltage_mv;
          }
      }
  }
#endif

  if (runtime->status_connection_generation ==
        gateway.connection_generation &&
      runtime->status_health_sequence == health_sequence)
    {
      return 0;
    }

  int ret = bkvoice_gateway_report_status(&runtime->session.gateway, &status);
  if (ret >= 0)
    {
      runtime->status_connection_generation = gateway.connection_generation;
      runtime->status_health_sequence = health_sequence;
    }

  return ret;
}

#ifdef BKVOICE_RUNTIME_OTA
struct bkvoice_ota_source_proxy_s
{
  struct bkvoice_runtime_s *runtime;
  const struct bk7258_ota_source_ops_s *source;
  void *context;
};

static bool bkvoice_ota_intent_equal(
  const struct bkvoice_ota_intent_s *left,
  const struct bkvoice_ota_intent_s *right)
{
  return left->state == right->state &&
         memcmp(left->manifest_sha256, right->manifest_sha256,
                sizeof(left->manifest_sha256)) == 0 &&
         bk7258_mcuboot_version_equal(&left->source_version,
                                      &right->source_version) &&
         left->source_security_counter == right->source_security_counter &&
         bk7258_mcuboot_version_equal(&left->target_version,
                                      &right->target_version) &&
         left->target_security_counter == right->target_security_counter &&
         left->source_boot_generation == right->source_boot_generation;
}

static bool bkvoice_ota_store_retryable(int error)
{
  return error == -EAGAIN || error == -EINPROGRESS ||
         error == -ENODEV || error == -ENOTCONN ||
         error == -ETIMEDOUT || error == -EXDEV;
}

static int bkvoice_ota_store_commit_runtime(
  struct bkvoice_runtime_s *runtime,
  const struct bkvoice_ota_intent_s *intent)
{
  struct bkvoice_ota_intent_s observed;
  uint64_t revision = 0;
  int ret = bkvoice_ota_store_commit(intent);

  if (ret == -EINPROGRESS)
    {
      (void)bkvoice_ota_store_reload();
      ret = bkvoice_ota_store_load(&observed, &revision);
      if (ret == 0 && bkvoice_ota_intent_equal(&observed, intent))
        {
          ret = 0;
        }
      else
        {
          return ret < 0 ? ret : -EAGAIN;
        }
    }

  if (ret == 0)
    {
      runtime->ota_intent = *intent;
      runtime->ota_intent_present = true;
    }

  return ret;
}

static int bkvoice_ota_store_clear_runtime(struct bkvoice_runtime_s *runtime)
{
  struct bkvoice_ota_intent_s observed;
  uint64_t revision = 0;
  int ret;

  if (!runtime->ota_intent_present)
    {
      return 0;
    }

  ret = bkvoice_ota_store_clear(runtime->ota_intent.manifest_sha256);
  if (ret == -EINPROGRESS)
    {
      (void)bkvoice_ota_store_reload();
      ret = bkvoice_ota_store_load(&observed, &revision);
      if (ret == -ENOENT)
        {
          ret = 0;
        }
      else
        {
          return ret < 0 ? ret : -EAGAIN;
        }
    }

  if (ret == 0 || ret == -ENOENT)
    {
      memset(&runtime->ota_intent, 0, sizeof(runtime->ota_intent));
      runtime->ota_intent_present = false;
      return 0;
    }

  return ret;
}

static void bkvoice_ota_restore_progress(struct bkvoice_runtime_s *runtime)
{
  uint64_t now;
  int ret;

  if (runtime->ota_store_ready)
    {
      return;
    }

  now = bkvoice_now();
  if (now < runtime->ota_restore_ms)
    {
      return;
    }

  runtime->ota_restore_ms = now + BKVOICE_OTA_RESTORE_RETRY_MS;
  ret = bkvoice_ota_store_load(&runtime->ota_intent,
                               &runtime->ota_store_revision);
  if (ret == 0)
    {
      runtime->ota_intent_present = true;
      runtime->ota_store_ready = true;
      return;
    }

  if (ret == -ENOENT)
    {
      memset(&runtime->ota_intent, 0, sizeof(runtime->ota_intent));
      runtime->ota_intent_present = false;
      runtime->ota_store_ready = true;
      return;
    }

  if (ret == -EINPROGRESS)
    {
      (void)bkvoice_ota_store_reload();
    }

  runtime->last_error = ret;
  if (!bkvoice_ota_store_retryable(ret))
    {
      syslog(LOG_ERR, "BKVOICE OTA intent_restore_fail=%d\n", ret);
    }
}

static int bkvoice_ota_source_open(
  void *context, struct bk7258_ota_manifest_s *manifest)
{
  struct bkvoice_ota_source_proxy_s *proxy = context;
  int ret = proxy->source->open(proxy->context, manifest);

  if (ret == 0)
    {
      struct timespec deadline;

      if (__atomic_load_n(&proxy->runtime->ota_cancel_requested,
                          __ATOMIC_ACQUIRE))
        {
          return -ECANCELED;
        }

      proxy->runtime->ota_target_version = manifest->image_version;
      proxy->runtime->ota_target_security_counter =
        manifest->security_counter;
      __atomic_store_n(&proxy->runtime->ota_target_admission_result,
                       -EINPROGRESS, __ATOMIC_RELAXED);
      bkvoice_ota_target_admission_publish(
        &proxy->runtime->ota_target_admission);
      (void)sem_post(proxy->runtime->wake);

      if (clock_gettime(CLOCK_REALTIME, &deadline) < 0)
        {
          return -errno;
        }

      deadline.tv_sec += BKVOICE_OTA_TARGET_APPROVAL_MS / 1000u;
      deadline.tv_nsec +=
        (long)(BKVOICE_OTA_TARGET_APPROVAL_MS % 1000u) * 1000l * 1000l;
      if (deadline.tv_nsec >= 1000l * 1000l * 1000l)
        {
          deadline.tv_sec++;
          deadline.tv_nsec -= 1000l * 1000l * 1000l;
        }

      for (;;)
        {
          uint8_t admission =
            bkvoice_ota_target_admission_load(
              &proxy->runtime->ota_target_admission);

          if (admission == BKVOICE_OTA_TARGET_APPROVED)
            {
              return 0;
            }

          if (admission == BKVOICE_OTA_TARGET_REJECTED)
            {
              return __atomic_load_n(
                       &proxy->runtime->ota_target_admission_result,
                       __ATOMIC_RELAXED);
            }

          if (admission == BKVOICE_OTA_TARGET_READY &&
              __atomic_load_n(&proxy->runtime->ota_cancel_requested,
                              __ATOMIC_ACQUIRE))
            {
              if (bkvoice_ota_target_admission_claim_rejection(
                    &proxy->runtime->ota_target_admission))
                {
                  __atomic_store_n(
                    &proxy->runtime->ota_target_admission_result,
                    -ECANCELED, __ATOMIC_RELAXED);
                  bkvoice_ota_target_admission_finish(
                    &proxy->runtime->ota_target_admission, false);
                  return -ECANCELED;
                }

              continue;
            }

          if (sem_timedwait(&proxy->runtime->ota_target_decision,
                            &deadline) == 0)
            {
              continue;
            }

          if (errno == EINTR)
            {
              continue;
            }

          ret = errno == ETIMEDOUT ? -ETIMEDOUT : -errno;
          return ret;
        }
    }

  return ret;
}

static int bkvoice_ota_source_read(
  void *context, enum bk7258_ota_image_e image, uint32_t offset,
  uint8_t *buffer, size_t nbytes)
{
  struct bkvoice_ota_source_proxy_s *proxy = context;
  return proxy->source->read_at(proxy->context, image, offset, buffer, nbytes);
}

static int bkvoice_ota_source_checkpoint(
  void *context, const struct bk7258_ota_progress_s *progress)
{
  struct bkvoice_ota_source_proxy_s *proxy = context;
  return proxy->source->checkpoint == NULL ? 0 :
         proxy->source->checkpoint(proxy->context, progress);
}

static int bkvoice_ota_source_cancel(void *context)
{
  struct bkvoice_ota_source_proxy_s *proxy = context;
  return proxy->source->cancel == NULL ? 0 :
         proxy->source->cancel(proxy->context);
}

static void bkvoice_ota_source_close(void *context)
{
  struct bkvoice_ota_source_proxy_s *proxy = context;
  if (proxy->source->close != NULL)
    {
      proxy->source->close(proxy->context);
    }
}

static const struct bk7258_ota_source_ops_s g_bkvoice_ota_source_ops =
{
  .open = bkvoice_ota_source_open,
  .read_at = bkvoice_ota_source_read,
  .checkpoint = bkvoice_ota_source_checkpoint,
  .cancel = bkvoice_ota_source_cancel,
  .close = bkvoice_ota_source_close,
};

static int bkvoice_ota_request(
  void *context, uint32_t request_sequence,
  const uint8_t manifest_sha256[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES])
{
  struct bkvoice_runtime_s *runtime = context;

  if (runtime == NULL || request_sequence == 0 || manifest_sha256 == NULL)
    {
      return -EINVAL;
    }

  if (!runtime->config.initialized && runtime->ota_direct_request == NULL)
    {
      return -ENOKEY;
    }

  if (!runtime->ota_store_ready)
    {
      return -EAGAIN;
    }

#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
  if (runtime->wake_session != NULL)
    {
      int ret = bkvoice_wake_session_step(runtime->wake_session, false,
                                          bkvoice_now());
      if (ret < 0) return ret;
    }
#endif

  if (__atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
      BKVOICE_OTA_JOB_EMPTY)
    {
      return -EBUSY;
    }

#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (!bkvoice_soft_off_mutation_enter(runtime)) return -ESHUTDOWN;
  if (__atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
      BKVOICE_OTA_JOB_EMPTY)
    {
      bkvoice_soft_off_mutation_leave(runtime);
      return -EBUSY;
    }
#endif

  runtime->ota_request_sequence = request_sequence;
  memcpy(runtime->ota_manifest_sha256, manifest_sha256,
         sizeof(runtime->ota_manifest_sha256));
  runtime->ota_report_phase = 0;
  runtime->ota_report_progress = 0;
  runtime->ota_result = -EINPROGRESS;
  runtime->ota_request_prepared = false;
  runtime->ota_target_valid = false;
  runtime->ota_target_commit_started = false;
  __atomic_store_n(&runtime->ota_target_admission_result, 0,
                   __ATOMIC_RELAXED);
  while (sem_trywait(&runtime->ota_target_decision) == 0)
    {
    }
  __atomic_store_n(&runtime->ota_target_admission, BKVOICE_OTA_TARGET_IDLE,
                   __ATOMIC_RELEASE);
  memset(&runtime->ota_target_version, 0,
         sizeof(runtime->ota_target_version));
  runtime->ota_target_security_counter = 0;
  __atomic_store_n(&runtime->ota_cancel_requested, false, __ATOMIC_RELEASE);
  __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_QUEUED,
                   __ATOMIC_RELEASE);
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  bkvoice_soft_off_mutation_leave(runtime);
#endif
  (void)sem_post(runtime->wake);
  return 0;
}

static int bkvoice_ota_catalog_url(struct bkvoice_runtime_s *runtime)
{
  static const char hex[] = "0123456789abcdef";
  char digest[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES * 2u + 1u];
  int length;

  for (size_t index = 0; index < sizeof(runtime->ota_manifest_sha256); index++)
    {
      digest[index * 2u] = hex[runtime->ota_manifest_sha256[index] >> 4];
      digest[index * 2u + 1u] = hex[runtime->ota_manifest_sha256[index] & 15u];
    }

  digest[sizeof(digest) - 1u] = '\0';
  length = snprintf(runtime->ota_catalog_url,
                    sizeof(runtime->ota_catalog_url),
                    "https://%s:%u/firmware/v1/%s/catalog.json",
                    runtime->config.host, (unsigned int)runtime->config.port,
                    digest);
  return length < 0 || (size_t)length >= sizeof(runtime->ota_catalog_url) ?
         -ENAMETOOLONG : 0;
}

static void *bkvoice_ota_worker(void *context)
{
  struct bkvoice_runtime_s *runtime = context;
  struct bk7258_ota_http_source_s source = {0};
  struct bkvoice_ota_source_proxy_s proxy;
  const struct bk7258_ota_source_ops_s *ops = NULL;
  mbedtls_x509_crt direct_ca;
  struct in_addr direct_peer;
  bool direct_ca_initialized = false;
  int ret;

  if (__atomic_load_n(&runtime->ota_cancel_requested, __ATOMIC_ACQUIRE))
    {
      ret = -ECANCELED;
      goto out;
    }

  if (runtime->ota_direct_request != NULL)
    {
      mbedtls_x509_crt_init(&direct_ca);
      direct_ca_initialized = true;
      ret = mbedtls_x509_crt_parse(
              &direct_ca,
              (const unsigned char *)runtime->ota_direct_request->ca_pem,
              strlen(runtime->ota_direct_request->ca_pem) + 1u);
      if (ret != 0)
        {
          ret = -EKEYREJECTED;
          goto out;
        }

      memcpy(&direct_peer.s_addr, runtime->ota_direct_request->ipv4,
             sizeof(direct_peer.s_addr));
      ret = bk7258_ota_http_source_initialize_with_server_ca(
              &source, runtime->ota_catalog_url, &direct_peer, &direct_ca);
    }
  else
    {
      ret = bk7258_ota_http_source_initialize_with_credentials(
              &source, runtime->ota_catalog_url,
              &runtime->config.peer_address, &runtime->config.ca,
              &runtime->config.certificate, &runtime->config.private_key);
    }
  if (ret < 0)
    {
      goto out;
    }

  if (runtime->ota_direct_request != NULL)
    {
      ret = bk7258_ota_http_source_expect_catalog(
              &source, runtime->ota_direct_request->catalog_sha256);
      if (ret < 0)
        {
          goto close_source;
        }
    }

  ops = bk7258_ota_http_source_ops();
  proxy.runtime = runtime;
  proxy.source = ops;
  proxy.context = &source;
  if (__atomic_load_n(&runtime->ota_cancel_requested, __ATOMIC_ACQUIRE))
    {
      ret = -ECANCELED;
    }
  else
    {
      ret = bk7258_ota_manager_apply(
              &g_bkvoice_ota_source_ops, &proxy,
              CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
    }

  /* The manager normally closes every admitted source.  Keep this guard for
   * an initialization/configuration failure before the manager can admit it.
   */

close_source:
  if (source.priv != NULL)
    {
      if (ops == NULL)
        {
          ops = bk7258_ota_http_source_ops();
        }

      if (ops != NULL && ops->close != NULL)
        {
          ops->close(&source);
        }
    }

out:
  if (direct_ca_initialized)
    {
      mbedtls_x509_crt_free(&direct_ca);
    }

  runtime->ota_result = ret;
  __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_DONE,
                   __ATOMIC_RELEASE);
  (void)sem_post(&runtime->ota_done);
  (void)sem_post(runtime->wake);
  return NULL;
}

static void bkvoice_ota_flush_done(struct bkvoice_runtime_s *runtime)
{
  while (sem_trywait(&runtime->ota_done) == 0)
    {
    }
}

static int bkvoice_ota_start(struct bkvoice_runtime_s *runtime)
{
  pthread_attr_t attr;
  int ret;

  if (__atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
      BKVOICE_OTA_JOB_QUEUED)
    {
      return 0;
    }

  if (runtime->ota_direct_request != NULL)
    {
      size_t length = strlen(runtime->ota_direct_request->url);
      if (length >= sizeof(runtime->ota_catalog_url))
        {
          ret = -ENAMETOOLONG;
        }
      else
        {
          memcpy(runtime->ota_catalog_url, runtime->ota_direct_request->url,
                 length + 1u);
          ret = 0;
        }
    }
  else
    {
      ret = bkvoice_ota_catalog_url(runtime);
    }
  if (ret < 0)
    {
      runtime->ota_result = ret;
      __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_DONE,
                       __ATOMIC_RELEASE);
      return 0;
    }

  bkvoice_ota_flush_done(runtime);
  __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_APPLYING,
                   __ATOMIC_RELEASE);
  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(
              &attr, CONFIG_BK7258_OTA_RPMSG_AP_STACKSIZE);
      if (ret == 0)
        {
          ret = pthread_create(&runtime->ota_worker, &attr,
                               bkvoice_ota_worker, runtime);
        }

      (void)pthread_attr_destroy(&attr);
    }

  if (ret != 0)
    {
      runtime->ota_result = -ret;
      __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_DONE,
                       __ATOMIC_RELEASE);
    }
  else
    {
      runtime->ota_worker_joinable = true;
    }

  return 0;
}

static int bkvoice_ota_join_finished(struct bkvoice_runtime_s *runtime)
{
  int ret;

  if (!runtime->ota_worker_joinable)
    {
      return 0;
    }

  ret = pthread_join(runtime->ota_worker, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  runtime->ota_worker_joinable = false;
  return 0;
}

static void bkvoice_ota_clear(struct bkvoice_runtime_s *runtime)
{
  if (runtime->ota_direct_request != NULL &&
      !runtime->ota_worker_joinable)
    {
      mbedtls_platform_zeroize(runtime->ota_direct_request,
                               sizeof(*runtime->ota_direct_request));
      free(runtime->ota_direct_request);
      runtime->ota_direct_request = NULL;
    }

  runtime->ota_request_sequence = 0;
  memset(runtime->ota_manifest_sha256, 0,
         sizeof(runtime->ota_manifest_sha256));
  memset(runtime->ota_catalog_url, 0, sizeof(runtime->ota_catalog_url));
  runtime->ota_result = 0;
  runtime->ota_report_phase = 0;
  runtime->ota_report_progress = 0;
  runtime->ota_request_prepared = false;
  runtime->ota_target_valid = false;
  runtime->ota_target_commit_started = false;
  __atomic_store_n(&runtime->ota_target_admission_result, 0,
                   __ATOMIC_RELAXED);
  runtime->ota_trial_poll_ms = 0;
  memset(&runtime->ota_target_version, 0,
         sizeof(runtime->ota_target_version));
  runtime->ota_target_security_counter = 0;
  runtime->ota_direct = false;
  __atomic_store_n(&runtime->ota_cancel_requested, false, __ATOMIC_RELEASE);
  __atomic_store_n(&runtime->ota_target_admission,
                   BKVOICE_OTA_TARGET_IDLE, __ATOMIC_RELEASE);
  __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_EMPTY,
                   __ATOMIC_RELEASE);
}

static void bkvoice_ota_reject_pending_target(
  struct bkvoice_runtime_s *runtime, int error)
{
  if (bkvoice_ota_target_admission_claim_rejection(
        &runtime->ota_target_admission))
    {
      __atomic_store_n(&runtime->ota_target_admission_result, error,
                       __ATOMIC_RELAXED);
      bkvoice_ota_target_admission_finish(
        &runtime->ota_target_admission, false);
      (void)sem_post(&runtime->ota_target_decision);
    }
}

static bool bkvoice_ota_commit_sensitive(
  const struct bkvoice_runtime_s *runtime)
{
  return bkvoice_ota_intent_is_commit_sensitive(
           runtime->ota_intent_present,
           runtime->ota_intent_present &&
             runtime->ota_intent.state == BKVOICE_OTA_DOWNLOADING,
           runtime->ota_intent_present ?
             runtime->ota_intent.target_security_counter : 0u,
           runtime->ota_target_commit_started);
}

static void bkvoice_ota_admit_pending_target(
  struct bkvoice_runtime_s *runtime)
{
  struct bkvoice_ota_intent_s intent;
  int ret;

  if (!bkvoice_ota_target_admission_claim(
        &runtime->ota_target_admission))
    {
      return;
    }

  if (!runtime->ota_intent_present ||
      runtime->ota_intent.state != BKVOICE_OTA_DOWNLOADING ||
      __atomic_load_n(&runtime->ota_cancel_requested, __ATOMIC_ACQUIRE))
    {
      ret = __atomic_load_n(&runtime->ota_cancel_requested,
                            __ATOMIC_ACQUIRE) ? -ECANCELED : -ESTALE;
    }
  else
    {
      intent = runtime->ota_intent;
      intent.target_version = runtime->ota_target_version;
      intent.target_security_counter = runtime->ota_target_security_counter;
      runtime->ota_target_commit_started = true;
      ret = bkvoice_ota_store_commit_runtime(runtime, &intent);
      if (ret == 0)
        {
          runtime->ota_target_valid = true;
        }
    }

  __atomic_store_n(&runtime->ota_target_admission_result, ret,
                   __ATOMIC_RELAXED);
  bkvoice_ota_target_admission_finish(&runtime->ota_target_admission,
                                      ret == 0);
  (void)sem_post(&runtime->ota_target_decision);
}

static int bkvoice_ota_cancel_join(struct bkvoice_runtime_s *runtime)
{
  uint8_t state = __atomic_load_n(&runtime->ota_job_state,
                                  __ATOMIC_ACQUIRE);
  int ret;

  if (state == BKVOICE_OTA_JOB_EMPTY)
    {
      return 0;
    }

  __atomic_store_n(&runtime->ota_cancel_requested, true, __ATOMIC_RELEASE);
  bkvoice_ota_reject_pending_target(runtime, -ECANCELED);
  if (runtime->ota_worker_joinable)
    {
      unsigned int attempts = BKVOICE_OTA_JOIN_MS / 50u;

      for (;;)
        {
          struct timespec deadline;

          (void)bk7258_ota_manager_cancel();
          if (clock_gettime(CLOCK_REALTIME, &deadline) < 0)
            {
              return -errno;
            }

          deadline.tv_nsec += 50l * 1000l * 1000l;
          if (deadline.tv_nsec >= 1000l * 1000l * 1000l)
            {
              deadline.tv_sec++;
              deadline.tv_nsec -= 1000l * 1000l * 1000l;
            }

          ret = sem_timedwait(&runtime->ota_done, &deadline);
          if (ret == 0)
            {
              break;
            }
          if (errno != ETIMEDOUT && errno != EINTR)
            {
              return -errno;
            }
          if (attempts-- == 0u)
            {
              return -ETIMEDOUT;
            }
        }

      ret = bkvoice_ota_join_finished(runtime);
      if (ret < 0)
        {
          (void)sem_post(&runtime->ota_done);
          return ret;
        }
    }

  bkvoice_ota_clear(runtime);
  return 0;
}

static int bkvoice_ota_cancel_control(struct bkvoice_runtime_s *runtime,
                                      int *terminal_result)
{
  enum bkvoice_ota_cancel_disposition_e disposition;
  uint8_t state;
  int manager_result = 0;
  int ret;

  if (terminal_result == NULL)
    {
      return -EINVAL;
    }

  *terminal_result = -ECANCELED;
  state = __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE);
  disposition = bkvoice_ota_cancel_disposition(
                  state, manager_result, runtime->ota_result,
                  runtime->ota_target_valid,
                  bkvoice_ota_commit_sensitive(runtime));
  if (!bkvoice_ota_cancel_requires_join(state,
                                        runtime->ota_worker_joinable) ||
      state != BKVOICE_OTA_JOB_DONE)
    {
      if (disposition == BKVOICE_OTA_CANCEL_PRESERVE)
        {
          return -EALREADY;
        }

      if (disposition == BKVOICE_OTA_CANCEL_CLEAR)
        {
          return 0;
        }
    }

  if (state == BKVOICE_OTA_JOB_APPLYING)
    {
      __atomic_store_n(&runtime->ota_cancel_requested, true,
                       __ATOMIC_RELEASE);
      bkvoice_ota_reject_pending_target(runtime, -ECANCELED);
      manager_result = bk7258_ota_manager_cancel();
      disposition = bkvoice_ota_cancel_disposition(
                      state, manager_result, runtime->ota_result,
                      runtime->ota_target_valid,
                      bkvoice_ota_commit_sensitive(runtime));
      if (disposition == BKVOICE_OTA_CANCEL_PRESERVE)
        {
          return manager_result;
        }
    }

  if (runtime->ota_worker_joinable)
    {
      unsigned int attempts = BKVOICE_OTA_JOIN_MS / 50u;

      for (;;)
        {
          struct timespec deadline;

          if (clock_gettime(CLOCK_REALTIME, &deadline) < 0)
            {
              return -errno;
            }

          deadline.tv_nsec += 50l * 1000l * 1000l;
          if (deadline.tv_nsec >= 1000l * 1000l * 1000l)
            {
              deadline.tv_sec++;
              deadline.tv_nsec -= 1000l * 1000l * 1000l;
            }

          ret = sem_timedwait(&runtime->ota_done, &deadline);
          if (ret == 0)
            {
              break;
            }

          if (errno != ETIMEDOUT && errno != EINTR)
            {
              return -errno;
            }

          if (attempts-- == 0u)
            {
              return -ETIMEDOUT;
            }
        }

      ret = bkvoice_ota_join_finished(runtime);
      if (ret < 0)
        {
          (void)sem_post(&runtime->ota_done);
          return ret;
        }
    }

  state = __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE);
  disposition = bkvoice_ota_cancel_disposition(
                  state, manager_result, runtime->ota_result,
                  runtime->ota_target_valid,
                  bkvoice_ota_commit_sensitive(runtime));
  if (disposition == BKVOICE_OTA_CANCEL_PRESERVE)
    {
      return -EALREADY;
    }

  if (disposition == BKVOICE_OTA_CANCEL_WAIT)
    {
      return -EINPROGRESS;
    }

  if (runtime->ota_result < 0 && runtime->ota_result != -ECANCELED)
    {
      *terminal_result = runtime->ota_result;
    }
  else if (runtime->ota_result >= 0 && !runtime->ota_target_valid)
    {
      *terminal_result = -EBADMSG;
    }

  return 0;
}

static int bkvoice_ota_report(struct bkvoice_runtime_s *runtime, int result,
                              enum bkvoice_companion_ota_phase_e phase,
                              uint8_t progress)
{
  int ret;

  if (runtime->ota_direct)
    {
      runtime->ota_direct_status.state =
        phase == BKVOICE_COMPANION_OTA_CONFIRMED ||
        phase == BKVOICE_COMPANION_OTA_ROLLED_BACK ||
        phase == BKVOICE_COMPANION_OTA_FAILED ?
        BKVOICE_CONTROL_OTA_TERMINAL : BKVOICE_CONTROL_OTA_ACTIVE;

      runtime->ota_direct_status.phase = (uint32_t)phase;
      if (phase == BKVOICE_COMPANION_OTA_FAILED ||
          phase == BKVOICE_COMPANION_OTA_ROLLED_BACK)
        {
          if (!runtime->ota_direct_status_valid ||
              runtime->ota_direct_status.progress > 99u)
            {
              runtime->ota_direct_status.progress = UINT32_MAX;
              runtime->ota_direct_status.total = UINT32_MAX;
            }
        }
      else
        {
          runtime->ota_direct_status.progress = progress;
          runtime->ota_direct_status.total = 100u;
        }

      runtime->ota_direct_status.result = result;
      runtime->ota_direct_status_valid = true;
      runtime->ota_report_phase = (uint8_t)phase;
      runtime->ota_report_progress = progress;
      return 0;
    }

  if (runtime->ota_report_phase == (uint8_t)phase &&
      runtime->ota_report_progress == progress)
    {
      return 0;
    }

  ret = bkvoice_gateway_report_ota(
          &runtime->session.gateway, runtime->ota_request_sequence,
          runtime->ota_manifest_sha256, result, phase, progress);
  if (ret >= 0)
    {
      runtime->ota_report_phase = (uint8_t)phase;
      runtime->ota_report_progress = progress;
    }

  return ret;
}

static uint8_t bkvoice_ota_progress(
  const struct bk7258_ota_manager_status_s *status)
{
  uint32_t first;
  uint32_t span;
  uint32_t progress;

  if (status->state == BK7258_OTA_MANAGER_STAGING_AP)
    {
      first = 1u;
      span = 46u;
    }
  else if (status->state == BK7258_OTA_MANAGER_STAGING_CP)
    {
      first = 48u;
      span = 46u;
    }
  else
    {
      return 1u;
    }

  progress = status->total == 0u ? first :
             first + (uint32_t)((uint64_t)status->completed * span /
                                status->total);
  return progress > 94u ? 94u : (uint8_t)progress;
}

static int bkvoice_ota_report_through(
  struct bkvoice_runtime_s *runtime,
  enum bkvoice_companion_ota_phase_e last)
{
  enum bkvoice_companion_ota_phase_e phase;
  int ret;

  phase = runtime->ota_report_phase == 0 ?
          BKVOICE_COMPANION_OTA_DOWNLOADING :
          (enum bkvoice_companion_ota_phase_e)runtime->ota_report_phase;
  for (; phase <= last; phase++)
    {
      ret = bkvoice_ota_report(runtime, 0, phase, 100);
      if (ret < 0)
        {
          return ret;
        }
    }

  return 0;
}

static int bkvoice_ota_fail(struct bkvoice_runtime_s *runtime, int error,
                            bool clear_intent)
{
  int ret = 0;

  if (clear_intent)
    {
      ret = bkvoice_ota_store_clear_runtime(runtime);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = bkvoice_ota_report(runtime, error,
                           BKVOICE_COMPANION_OTA_FAILED,
                           runtime->ota_report_progress);
  if (ret >= 0)
    {
      bkvoice_ota_clear(runtime);
    }

  return ret;
}

static int bkvoice_ota_begin_new(
  struct bkvoice_runtime_s *runtime,
  const struct bk7258_ota_pair_snapshot_s *pair)
{
  struct bkvoice_ota_intent_s intent;
  int ret;

  if (!runtime->firmware_identity_valid ||
      pair->state != BK7258_OTA_PAIR_CONFIRMED ||
      !pair->security_counter_present || pair->security_counter == 0u ||
      !bk7258_mcuboot_version_equal(&pair->version,
                                    &runtime->firmware_version))
    {
      return -ESTALE;
    }

  memset(&intent, 0, sizeof(intent));
  intent.state = BKVOICE_OTA_DOWNLOADING;
  memcpy(intent.manifest_sha256, runtime->ota_manifest_sha256,
         sizeof(intent.manifest_sha256));
  intent.source_version = pair->version;
  intent.source_security_counter = pair->security_counter;
  intent.source_boot_generation = runtime->boot_generation;
  ret = bkvoice_ota_store_commit_runtime(runtime, &intent);
  if (ret == 0)
    {
      runtime->ota_request_prepared = true;
    }

  return ret;
}

static int bkvoice_ota_finish_terminal(
  struct bkvoice_runtime_s *runtime,
  enum bkvoice_companion_ota_phase_e phase)
{
  int ret = bkvoice_ota_report_through(runtime, phase);

  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_ota_store_clear_runtime(runtime);
  if (ret == 0)
    {
      bkvoice_ota_clear(runtime);
    }

  return ret;
}

static int bkvoice_ota_prepare_request(struct bkvoice_runtime_s *runtime)
{
  struct bk7258_ota_pair_snapshot_s pair;
  enum bkvoice_ota_flow_action_e action;
  bool same_manifest;
  int ret;

  if (runtime->ota_request_prepared ||
      __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
        BKVOICE_OTA_JOB_QUEUED)
    {
      return 0;
    }

  memset(&pair, 0, sizeof(pair));
  ret = bk7258_ota_rpmsg_pair_status(
          &pair, CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
  if (ret < 0)
    {
      return ret;
    }

  if (!runtime->ota_intent_present)
    {
      return bkvoice_ota_begin_new(runtime, &pair);
    }

  ret = bkvoice_ota_flow_decide(&runtime->ota_intent,
                                runtime->boot_generation, &pair, &action);
  if (ret < 0)
    {
      return ret;
    }

  same_manifest =
    memcmp(runtime->ota_intent.manifest_sha256,
           runtime->ota_manifest_sha256,
           sizeof(runtime->ota_manifest_sha256)) == 0;
  if (!same_manifest)
    {
      if (action != BKVOICE_OTA_FLOW_CONFIRMED &&
          action != BKVOICE_OTA_FLOW_ROLLED_BACK)
        {
          return -EBUSY;
        }

      ret = bkvoice_ota_store_clear_runtime(runtime);
      return ret < 0 ? ret : bkvoice_ota_begin_new(runtime, &pair);
    }

  switch (action)
    {
      case BKVOICE_OTA_FLOW_RESTAGE:
        runtime->ota_request_prepared = true;
        return 0;

      case BKVOICE_OTA_FLOW_REBOOT:
        __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_STAGED,
                         __ATOMIC_RELEASE);
        return 0;

      case BKVOICE_OTA_FLOW_TRIAL:
        if (runtime->ota_intent.state != BKVOICE_OTA_TRIAL)
          {
            struct bkvoice_ota_intent_s intent = runtime->ota_intent;
            intent.state = BKVOICE_OTA_TRIAL;
            ret = bkvoice_ota_store_commit_runtime(runtime, &intent);
            if (ret < 0)
              {
                return ret;
              }
          }

        ret = bkvoice_ota_report_through(runtime,
                                         BKVOICE_COMPANION_OTA_TRIAL);
        if (ret == 0)
          {
            runtime->ota_trial_poll_ms = 0;
            __atomic_store_n(&runtime->ota_job_state,
                             BKVOICE_OTA_JOB_TRIAL, __ATOMIC_RELEASE);
          }
        return ret;

      case BKVOICE_OTA_FLOW_CONFIRMED:
        return bkvoice_ota_finish_terminal(
                 runtime, BKVOICE_COMPANION_OTA_CONFIRMED);

      case BKVOICE_OTA_FLOW_ROLLED_BACK:
        return bkvoice_ota_finish_terminal(
                 runtime, BKVOICE_COMPANION_OTA_ROLLED_BACK);
    }

  return -EINVAL;
}

static int bkvoice_ota_reboot_step(struct bkvoice_runtime_s *runtime)
{
  struct bkvoice_ota_intent_s intent;
  int ret;

  ret = bkvoice_ota_report_through(runtime, BKVOICE_COMPANION_OTA_STAGED);
  if (ret < 0)
    {
      return ret;
    }

  if (runtime->ota_intent.state != BKVOICE_OTA_REBOOTING)
    {
      intent = runtime->ota_intent;
      intent.state = BKVOICE_OTA_REBOOTING;
      ret = bkvoice_ota_store_commit_runtime(runtime, &intent);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = bkvoice_ota_report(runtime, 0,
                           BKVOICE_COMPANION_OTA_REBOOTING, 100);
  if (ret < 0)
    {
      return ret;
    }

  __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_REBOOTING,
                   __ATOMIC_RELEASE);
  return bk7258_ota_rpmsg_reboot(
           CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
}

static int bkvoice_ota_trial_step(struct bkvoice_runtime_s *runtime)
{
  struct bk7258_ota_pair_snapshot_s pair;
  enum bkvoice_ota_flow_action_e action;
  uint64_t now = bkvoice_now();
  int ret;

  if (now < runtime->ota_trial_poll_ms)
    {
      return 0;
    }

  runtime->ota_trial_poll_ms = now + BKVOICE_OTA_TRIAL_POLL_MS;
  memset(&pair, 0, sizeof(pair));
  ret = bk7258_ota_rpmsg_pair_status(
          &pair, CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_ota_flow_decide(&runtime->ota_intent,
                                runtime->boot_generation, &pair, &action);
  if (ret < 0)
    {
      return ret;
    }

  if (action == BKVOICE_OTA_FLOW_TRIAL)
    {
      return bkvoice_ota_report_through(runtime,
                                        BKVOICE_COMPANION_OTA_TRIAL);
    }

  if (action == BKVOICE_OTA_FLOW_CONFIRMED)
    {
      return bkvoice_ota_finish_terminal(
               runtime, BKVOICE_COMPANION_OTA_CONFIRMED);
    }

  if (action == BKVOICE_OTA_FLOW_ROLLED_BACK)
    {
      return bkvoice_ota_finish_terminal(
               runtime, BKVOICE_COMPANION_OTA_ROLLED_BACK);
    }

  return -ESTALE;
}

static int bkvoice_ota_step(struct bkvoice_runtime_s *runtime)
{
  struct bk7258_ota_manager_status_s status;
  struct bkvoice_ota_intent_s intent;
  uint8_t state;
  int ret;

  state = __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE);
  if (state == BKVOICE_OTA_JOB_QUEUED && !runtime->ota_request_prepared)
    {
      ret = bkvoice_ota_prepare_request(runtime);
      if (ret < 0)
        {
          return bkvoice_ota_store_retryable(ret) ? ret :
                 bkvoice_ota_fail(runtime, ret, false);
        }
    }

  state = __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE);
  if (state == BKVOICE_OTA_JOB_STAGED)
    {
      return bkvoice_ota_reboot_step(runtime);
    }

  if (state == BKVOICE_OTA_JOB_TRIAL)
    {
      return bkvoice_ota_trial_step(runtime);
    }

  if (state == BKVOICE_OTA_JOB_REBOOTING)
    {
      return 0;
    }

  ret = bkvoice_ota_start(runtime);
  if (ret < 0)
    {
      return ret;
    }

  state = __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE);
  if (state == BKVOICE_OTA_JOB_APPLYING &&
      bkvoice_ota_target_admission_load(
        &runtime->ota_target_admission) == BKVOICE_OTA_TARGET_READY)
    {
      bkvoice_ota_admit_pending_target(runtime);
    }

  if (state == BKVOICE_OTA_JOB_APPLYING &&
      bk7258_ota_manager_get_status(&status) == 0)
    {
      if (status.state == BK7258_OTA_MANAGER_STAGING_AP ||
          status.state == BK7258_OTA_MANAGER_STAGING_CP ||
          status.state == BK7258_OTA_MANAGER_PACKAGE_VERIFIED)
        {
          uint8_t progress = bkvoice_ota_progress(&status);
          if (progress < runtime->ota_report_progress)
            {
              progress = runtime->ota_report_progress;
            }

          ret = bkvoice_ota_report(runtime, 0,
                                   BKVOICE_COMPANION_OTA_DOWNLOADING,
                                   progress);
          if (ret < 0)
            {
              return ret;
            }
        }
      else if (status.state == BK7258_OTA_MANAGER_PAIR_VERIFIED)
        {
          ret = bkvoice_ota_report(runtime, 0,
                                   BKVOICE_COMPANION_OTA_VERIFYING, 100);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  state = __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE);
  if (state != BKVOICE_OTA_JOB_DONE)
    {
      return 0;
    }

  ret = bkvoice_ota_join_finished(runtime);
  if (ret < 0)
    {
      return ret;
    }

  if (runtime->ota_result < 0)
    {
      return bkvoice_ota_fail(runtime, runtime->ota_result,
                              !bkvoice_ota_commit_sensitive(runtime));
    }

  if (!runtime->ota_target_valid)
    {
      return bkvoice_ota_fail(runtime, -EBADMSG, false);
    }

  intent = runtime->ota_intent;
  intent.target_version = runtime->ota_target_version;
  intent.target_security_counter = runtime->ota_target_security_counter;
  intent.state = BKVOICE_OTA_STAGED;
  ret = bkvoice_ota_store_commit_runtime(runtime, &intent);
  if (ret < 0)
    {
      return bkvoice_ota_fail(runtime, ret, false);
    }

  ret = bkvoice_ota_report_through(runtime, BKVOICE_COMPANION_OTA_STAGED);
  if (ret < 0)
    {
      return ret;
    }

  __atomic_store_n(&runtime->ota_job_state, BKVOICE_OTA_JOB_STAGED,
                   __ATOMIC_RELEASE);
  return 0;
}

static void bkvoice_ota_status_unknown(struct bkcontrol_ota_status_s *status)
{
  memset(status, 0xff, sizeof(*status));
  status->state = BKVOICE_CONTROL_OTA_IDLE;
  status->result = 0;
}

static void bkvoice_ota_status_from_manager(
  struct bkvoice_runtime_s *runtime,
  const struct bk7258_ota_manager_status_s *manager,
  struct bkcontrol_ota_status_s *status)
{
  uint8_t progress;

  status->state = BKVOICE_CONTROL_OTA_ACTIVE;
  status->phase = BKVOICE_COMPANION_OTA_DOWNLOADING;
  status->progress = 0u;
  status->total = 100u;
  status->result = manager->last_error;

  switch (manager->state)
    {
      case BK7258_OTA_MANAGER_STAGING_AP:
      case BK7258_OTA_MANAGER_STAGING_CP:
      case BK7258_OTA_MANAGER_PACKAGE_VERIFIED:
        progress = bkvoice_ota_progress(manager);
        if (progress < runtime->ota_report_progress)
          {
            progress = runtime->ota_report_progress;
          }

        status->progress = progress;
        break;

      case BK7258_OTA_MANAGER_PAIR_VERIFIED:
        status->phase = BKVOICE_COMPANION_OTA_VERIFYING;
        status->progress = 100u;
        break;

      case BK7258_OTA_MANAGER_READY_TO_REBOOT:
        status->phase = BKVOICE_COMPANION_OTA_STAGED;
        status->progress = 100u;
        break;

      case BK7258_OTA_MANAGER_FAILED:
      case BK7258_OTA_MANAGER_CANCELED:
        status->state = BKVOICE_CONTROL_OTA_TERMINAL;
        status->phase = BKVOICE_COMPANION_OTA_FAILED;
        if (runtime->ota_direct_status_valid &&
            runtime->ota_direct_status.progress <= 99u)
          {
            status->progress = runtime->ota_direct_status.progress;
          }
        else
          {
            status->progress = UINT32_MAX;
            status->total = UINT32_MAX;
          }

        if (manager->state == BK7258_OTA_MANAGER_CANCELED &&
            status->result == 0)
          {
            status->result = -ECANCELED;
          }
        break;

      case BK7258_OTA_MANAGER_IDLE:
      case BK7258_OTA_MANAGER_CHECKING:
      default:
        break;
    }
}

static int bkvoice_ota_control_status(struct bkvoice_runtime_s *runtime,
                                      struct bkcontrol_ota_status_s *status)
{
  struct bk7258_ota_manager_status_s manager;
  uint8_t state;

  bkvoice_ota_status_unknown(status);
  state = __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE);
  if (runtime->ota_direct &&
      (state == BKVOICE_OTA_JOB_APPLYING ||
       state == BKVOICE_OTA_JOB_DONE) &&
      bk7258_ota_manager_get_status(&manager) == 0)
    {
      bkvoice_ota_status_from_manager(runtime, &manager, status);
      runtime->ota_direct_status = *status;
      runtime->ota_direct_status_valid = true;
      return 0;
    }

  if (runtime->ota_direct_status_valid)
    {
      *status = runtime->ota_direct_status;
      return 0;
    }

  if (runtime->ota_intent_present)
    {
      struct bk7258_ota_pair_snapshot_s pair;
      enum bkvoice_ota_flow_action_e action;
      int ret;

      memset(&pair, 0, sizeof(pair));
      ret = bk7258_ota_rpmsg_pair_status(
              &pair, CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
      if (ret < 0)
        {
          return ret;
        }

      ret = bkvoice_ota_flow_decide(&runtime->ota_intent,
                                    runtime->boot_generation, &pair,
                                    &action);
      if (ret < 0)
        {
          return ret;
        }

      status->result = action == BKVOICE_OTA_FLOW_CONFIRMED ||
                       action == BKVOICE_OTA_FLOW_ROLLED_BACK ?
                       0 : -EINPROGRESS;
      switch (action)
        {
          case BKVOICE_OTA_FLOW_RESTAGE:
            status->state = BKVOICE_CONTROL_OTA_ACTIVE;
            status->phase = BKVOICE_COMPANION_OTA_DOWNLOADING;
            break;
          case BKVOICE_OTA_FLOW_REBOOT:
            status->state = BKVOICE_CONTROL_OTA_ACTIVE;
            status->phase = BKVOICE_COMPANION_OTA_STAGED;
            status->progress = status->total = 100u;
            break;
          case BKVOICE_OTA_FLOW_TRIAL:
            status->state = BKVOICE_CONTROL_OTA_ACTIVE;
            status->phase = BKVOICE_COMPANION_OTA_TRIAL;
            status->progress = status->total = 100u;
            break;
          case BKVOICE_OTA_FLOW_CONFIRMED:
            status->state = BKVOICE_CONTROL_OTA_TERMINAL;
            status->phase = BKVOICE_COMPANION_OTA_CONFIRMED;
            status->progress = status->total = 100u;
            break;
          case BKVOICE_OTA_FLOW_ROLLED_BACK:
            status->state = BKVOICE_CONTROL_OTA_TERMINAL;
            status->phase = BKVOICE_COMPANION_OTA_ROLLED_BACK;
            break;
        }
    }

  return 0;
}
#endif

static void bkvoice_upload_clear(struct bkvoice_runtime_s *runtime)
{
  if (runtime->upload != NULL)
    {
      mbedtls_platform_zeroize(runtime->upload, runtime->upload_size);
      free(runtime->upload);
    }

  runtime->upload = NULL;
  runtime->upload_size = 0;
  runtime->uploaded = 0;
  runtime->upload_deadline = 0;
}

static void bkvoice_button_invalid(struct bkvoice_runtime_s *runtime)
{
  irqstate_t flags = spin_lock_irqsave(&runtime->button_lock);
  runtime->button_link = false;
  runtime->button_sequence = 0;
  runtime->button_received_ms = 0;
  runtime->button_pressed = false;
  runtime->button_epoch++;
  spin_unlock_irqrestore(&runtime->button_lock, flags);
  (void)sem_post(runtime->wake);
}

static int bkvoice_button_receive(struct rpmsg_endpoint *endpoint,
                                  void *data, size_t size, uint32_t src,
                                  void *priv)
{
  struct bkvoice_runtime_s *runtime = priv;
  struct bkvoice_button_event_s event;
  uint64_t now = bkvoice_now();
  irqstate_t flags;

  (void)endpoint;
  (void)src;
  if (data == NULL || size != sizeof(event))
    {
      return -EINVAL;
    }

  memcpy(&event, data, sizeof(event));
  if (event.magic != BKVOICE_BUTTON_MAGIC ||
      event.version != BKVOICE_BUTTON_VERSION || event.sequence == 0 ||
      event.reserved[0] != 0 || event.reserved[1] != 0)
    {
      return -EINVAL;
    }

#ifdef CONFIG_BK7258_PRODUCT_KEYS
  if ((event.pressed & ~BKVOICE_PRODUCT_KEY_VALID) != 0)
    {
      return -EINVAL;
    }
#else
  if (event.pressed > 1)
    {
      return -EINVAL;
    }
#endif

  flags = spin_lock_irqsave(&runtime->button_lock);
  if (event.sequence <= runtime->button_sequence)
    {
      spin_unlock_irqrestore(&runtime->button_lock, flags);
      return -ESTALE;
    }

  runtime->button_link = true;
  runtime->button_sequence = event.sequence;
  runtime->button_received_ms = now;
  runtime->button_pressed = event.pressed;
  spin_unlock_irqrestore(&runtime->button_lock, flags);
  (void)sem_post(runtime->wake);
  return 0;
}

#ifdef CONFIG_BK7258_PRODUCT_KEYS
static void bkvoice_runtime_product_keys(struct bkvoice_runtime_s *runtime,
                                         bool command_link, uint64_t now)
{
  uint32_t mask;
  uint32_t epoch;
  uint32_t action;
  bool link;
  bool power_requested;
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&runtime->button_lock);
  link = command_link && runtime->button_link && runtime->button_sequence != 0 &&
         now >= runtime->button_received_ms &&
         now - runtime->button_received_ms < BKVOICE_BUTTON_LEASE_MS;
  epoch = runtime->button_epoch;
  mask = runtime->button_pressed;
  spin_unlock_irqrestore(&runtime->button_lock, flags);

  if (!link)
    {
      /* A stale lease is equivalent to a reconnect: require release before
       * any later held key can create an edge. */
      bkvoice_product_keys_reset(&runtime->product_keys, epoch);
      return;
    }

  action = bkvoice_product_keys_step(&runtime->product_keys, epoch, mask, now,
                                     &power_requested);
  if (power_requested)
    {
#ifdef BKVOICE_RUNTIME_SOFT_OFF
      bool expected = false;

      if (!__atomic_compare_exchange_n(&runtime->soft_off_pending, &expected,
                                       true, false, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE))
        {
          return;
        }
      else
        {
          ret = bkvoice_soft_off_admit(runtime);
#ifdef CONFIG_BK7258_USBMODE
          if (ret == 0)
            {
              ret = bk7258_usbmode_blockdev_acquire();
              if (ret == 0) runtime->soft_off_blockdev_held = true;
            }
#endif
        }

      if (ret == 0)
        {
          runtime->soft_off_accepted = false;
          runtime->soft_off_failure = 0;
          (void)sem_post(runtime->wake);
          syslog(LOG_NOTICE, "BKVOICE KEYS soft_off_pending=1\n");
        }
      else
        {
          __atomic_store_n(&runtime->soft_off_pending, false,
                           __ATOMIC_RELEASE);
          runtime->last_error = ret;
          syslog(LOG_WARNING, "BKVOICE KEYS soft_off_rejected=%d\n", ret);
        }
#else
      runtime->last_error = -ENOTSUP;
      syslog(LOG_WARNING, "BKVOICE KEYS power_off_unsupported=%d\n",
             -ENOTSUP);
#endif
    }

  /* One press produces one rising edge. Repeated heartbeat masks have no
   * action, and an electrically impossible simultaneous +/- press is ignored.
   */
  if ((action & (BKVOICE_PRODUCT_KEY_VOLUME_DOWN |
                 BKVOICE_PRODUCT_KEY_VOLUME_UP)) ==
      BKVOICE_PRODUCT_KEY_VOLUME_DOWN ||
      (action & (BKVOICE_PRODUCT_KEY_VOLUME_DOWN |
                 BKVOICE_PRODUCT_KEY_VOLUME_UP)) ==
      BKVOICE_PRODUCT_KEY_VOLUME_UP)
    {
#ifdef CONFIG_BK7258_PREFERENCES
      unsigned int volume;
      int delta = (action & BKVOICE_PRODUCT_KEY_VOLUME_UP) != 0 ? 5 : -5;

      ret = bk7258_preferences_playback_volume(&volume);
      if (ret == 0)
        {
          if (delta < 0)
            {
              volume = volume < 5u ? 0u : volume - 5u;
            }
          else
            {
              volume = volume > 95u ? 100u : volume + 5u;
            }

          ret = bk7258_preferences_set_volume(volume);
        }

      if (ret < 0)
        {
          runtime->last_error = ret;
          syslog(LOG_WARNING, "BKVOICE KEYS volume_fail=%d\n", ret);
        }
#else
      runtime->last_error = -ENOTSUP;
      syslog(LOG_WARNING, "BKVOICE KEYS volume_unsupported=%d\n", -ENOTSUP);
#endif
    }
}
#endif

static void bkvoice_button_unbind(struct rpmsg_endpoint *endpoint)
{
  bkvoice_button_invalid(endpoint->priv);
}

static void bkvoice_button_device_created(struct rpmsg_device *rdev,
                                          void *priv)
{
  struct bkvoice_runtime_s *runtime = priv;
  const char *cpu = rpmsg_get_cpuname(rdev);

  if (cpu == NULL || strcmp(cpu, "cp") != 0)
    {
      return;
    }

  if (nxmutex_lock(&runtime->endpoint_lock) >= 0)
    {
      if (runtime->button_endpoint.rdev == NULL)
        {
          runtime->button_endpoint.priv = runtime;
          (void)rpmsg_create_ept(&runtime->button_endpoint, rdev,
                                 BKVOICE_BUTTON_ENDPOINT, RPMSG_ADDR_ANY,
                                 RPMSG_ADDR_ANY, bkvoice_button_receive,
                                 bkvoice_button_unbind);
        }

      nxmutex_unlock(&runtime->endpoint_lock);
    }
}

static void bkvoice_button_device_destroyed(struct rpmsg_device *rdev,
                                            void *priv)
{
  struct bkvoice_runtime_s *runtime = priv;

  if (nxmutex_lock(&runtime->endpoint_lock) >= 0)
    {
      if (runtime->button_endpoint.rdev == rdev)
        {
          bkvoice_button_invalid(runtime);
          rpmsg_destroy_ept(&runtime->button_endpoint);
          memset(&runtime->button_endpoint, 0,
                 sizeof(runtime->button_endpoint));
        }

      nxmutex_unlock(&runtime->endpoint_lock);
    }
}

static void *bkvoice_receiver(void *arg)
{
  struct bkvoice_runtime_s *runtime = arg;

  while (!__atomic_load_n(&runtime->receiver_stop, __ATOMIC_ACQUIRE))
    {
      uint32_t head = __atomic_load_n(&runtime->rx_head, __ATOMIC_RELAXED);
      uint32_t tail = __atomic_load_n(&runtime->rx_tail, __ATOMIC_ACQUIRE);
      struct bkvoice_gateway_frame_s *frame;
      int ret;

      if (head - tail >= BKVOICE_RX_SLOTS)
        {
          (void)poll(NULL, 0, 10);
          continue;
        }

      frame = &runtime->rx[head % BKVOICE_RX_SLOTS];
      ret = bkvoice_gateway_receive_frame(&runtime->session.gateway, frame,
                                          bkvoice_now() + BKVOICE_RX_IDLE_MS);
      if (ret < 0 && frame->generation == 0)
        {
          /* Initialization/lock failures precede the normal envelope stamp.
           * The owner cannot replace this connection before joining RX.
           */

          frame->generation = runtime->session.gateway.connection_generation;
          frame->error = ret;
        }
      __atomic_store_n(&runtime->rx_head, head + 1u, __ATOMIC_RELEASE);
      (void)sem_post(runtime->wake);
      if (ret < 0)
        {
          break;
        }
    }

  (void)sem_post(&runtime->receiver_done);
  return NULL;
}

static int bkvoice_runtime_disconnect(struct bkvoice_runtime_s *runtime,
                                      int reason)
{
  struct timespec deadline;
  int ret;

  runtime->armed = false;
  runtime->pressed = false;
  runtime->cleanup_pending = true;
#ifdef BKVOICE_RUNTIME_OTA
  ret = bkvoice_ota_cancel_join(runtime);
  if (ret < 0)
    {
      runtime->last_error = ret;
      return ret;
    }
#endif
#ifdef CONFIG_BK7258_VOICE_HIL_TEST
  runtime->test_until = 0;
#endif
  if (!runtime->session.initialized)
    {
      runtime->cleanup_pending = false;
      return 0;
    }

  __atomic_store_n(&runtime->receiver_stop, true, __ATOMIC_RELEASE);
  (void)bkvoice_session_interrupt(&runtime->session);
  if (runtime->receiver_joinable)
    {
      if (clock_gettime(CLOCK_REALTIME, &deadline) < 0)
        {
          return -errno;
        }

      deadline.tv_sec++;
      do
        {
          ret = sem_timedwait(&runtime->receiver_done, &deadline);
        }
      while (ret < 0 && errno == EINTR);

      if (ret < 0)
        {
          return -errno;
        }

      ret = pthread_join(runtime->receiver, NULL);
      if (ret != 0)
        {
          (void)sem_post(&runtime->receiver_done);
          return -ret;
        }

      runtime->receiver_joinable = false;
      __atomic_store_n(&runtime->rx_head, 0, __ATOMIC_RELEASE);
      __atomic_store_n(&runtime->rx_tail, 0, __ATOMIC_RELEASE);
    }

  ret = bkvoice_session_disconnect(&runtime->session, reason);
  if (ret < 0)
    {
      runtime->last_error = ret;
    }

  else
    {
      runtime->cleanup_pending = false;
    }

  return ret;
}

#ifdef BKVOICE_RUNTIME_SOFT_OFF
static int bkvoice_soft_off_fail(struct bkvoice_runtime_s *runtime, int error)
{
  if (runtime->soft_off_failure == 0) runtime->soft_off_failure = error;
#ifdef CONFIG_BK7258_USBMODE
  if (runtime->soft_off_blockdev_held)
    {
      int release_ret = bk7258_usbmode_blockdev_release();
      if (release_ret < 0)
        {
          runtime->soft_off_accepted = false;
          runtime->last_error = release_ret;
          return release_ret;
        }
      runtime->soft_off_blockdev_held = false;
    }
#endif
  error = runtime->soft_off_failure;
  runtime->soft_off_failure = 0;
  runtime->soft_off_accepted = false;
  runtime->soft_off_status_ms = 0;
  __atomic_store_n(&runtime->soft_off_pending, false, __ATOMIC_RELEASE);
  runtime->last_error = error;
  syslog(LOG_WARNING, "BKVOICE KEYS soft_off_fail=%d\n", error);
  return error;
}

static int bkvoice_soft_off_progress(struct bkvoice_runtime_s *runtime)
{
  int ret;

  if (!bkvoice_soft_off_pending(runtime))
    {
      return 0;
    }

  if (runtime->soft_off_accepted)
    {
      uint64_t now = bkvoice_now();

      if (now < runtime->soft_off_status_ms)
        {
          return -EAGAIN;
        }

      runtime->soft_off_status_ms = now + BKVOICE_SOFT_OFF_STATUS_POLL_MS;
      ret = bk7258_pm_soft_off_status();
      if (ret > 0) return -EAGAIN;
      if (ret < 0)
        {
          runtime->last_error = ret;
          return -EAGAIN;
        }

      runtime->soft_off_accepted = false;
      return bkvoice_soft_off_fail(runtime,
        runtime->soft_off_failure < 0 ? runtime->soft_off_failure :
                                        -ECANCELED);
    }

  if (runtime->soft_off_failure < 0)
    {
      return bkvoice_soft_off_fail(runtime, runtime->soft_off_failure);
    }

#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
  if (runtime->wake_session != NULL)
    {
      ret = bkvoice_wake_session_step(runtime->wake_session, false,
                                      bkvoice_now());
      if (ret == -EAGAIN) return ret;
      if (ret < 0) return bkvoice_soft_off_fail(runtime, ret);
    }
#endif
  if (runtime->cloud != NULL && bkcloud_runtime_busy(runtime->cloud))
    {
      ret = bkcloud_runtime_cancel_drain(runtime->cloud);
      if (ret == -EAGAIN) return ret;
      if (ret < 0) return bkvoice_soft_off_fail(runtime, ret);
    }

  if (runtime->session.connected || runtime->receiver_joinable ||
      runtime->cleanup_pending ||
      (runtime->ptt != NULL && runtime->ptt->capture_ready))
    {
      ret = bkvoice_runtime_disconnect(runtime, -ECANCELED);
      if (ret == -EAGAIN) return ret;
      if (ret < 0) return bkvoice_soft_off_fail(runtime, ret);
    }

  sync();
  ret = bk7258_pm_soft_off_request();
  if (ret < 0)
    {
      if (ret == -ENOTSUP || ret == -EBUSY)
        {
          return bkvoice_soft_off_fail(runtime, ret);
        }

      /* The transport may have timed out after CP accepted the tuple. Keep
       * all gates and the storage lease until the read-only status RPC proves
       * CP canceled it. */
      runtime->soft_off_failure = ret;
      runtime->last_error = ret;
      runtime->soft_off_accepted = true;
      runtime->soft_off_status_ms = bkvoice_now();
      return -EAGAIN;
    }

  /* CP accepted the one-shot request. No new AP activity is admitted while
   * the whole-chip reset-to-sleep transition is pending. */
  runtime->soft_off_accepted = true;
  runtime->soft_off_failure = 0;
  runtime->soft_off_status_ms =
    bkvoice_now() + BKVOICE_SOFT_OFF_STATUS_POLL_MS;
  return 0;
}
#endif

static int bkvoice_runtime_clear(struct bkvoice_runtime_s *runtime)
{
  int ret;
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
  ret = bkvoice_wake_close(runtime);
  if (ret < 0) return ret;
#endif
  ret = bkcloud_runtime_clear(&runtime->cloud);
  if (ret < 0) return ret;
  ret = bkvoice_runtime_disconnect(runtime, -ECANCELED);

  if (ret < 0)
    {
      return ret;
    }

  if (runtime->session.initialized)
    {
      ret = bkvoice_session_uninitialize(&runtime->session);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (runtime->wss.initialized)
    {
      ret = bkvoice_wss_uninitialize(&runtime->wss);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (runtime->tls.initialized)
    {
      ret = bkvoice_tls_uninitialize(&runtime->tls);
      if (ret < 0)
        {
          return ret;
        }
    }

  bkvoice_config_clear(&runtime->config);
  bkvoice_upload_clear(runtime);
  return 0;
}

static int bkvoice_runtime_connect(struct bkvoice_runtime_s *runtime)
{
  struct bkvoice_tls_config_s tls_config;
  struct bkvoice_wss_config_s wss_config;
  const struct bkvoice_session_config_s session_config =
  {
    .gateway =
    {
      .io_timeout_ms = BKVOICE_IO_MS,
#ifdef BKVOICE_RUNTIME_OTA
      .ota_request = bkvoice_ota_request,
      .ota_context = runtime,
#endif
    },
    .initial_downlink_credit = BKVOICE_COMPANION_AUDIO_FRAME_BYTES * 4u,
  };
  pthread_attr_t attr;
  int ret;

  if (!runtime->config.initialized)
    {
      return -ENOKEY;
    }
#ifdef BKVOICE_RUNTIME_OTA
  if (!runtime->ota_store_ready)
    {
      return -EAGAIN;
    }
#endif

  if (runtime->receiver_joinable || runtime->session.connected)
    {
      return -EALREADY;
    }

  ret = bkvoice_config_trusted_time(&runtime->config);
  if (ret < 0)
    {
      return ret;
    }

  if (!runtime->session.initialized)
    {
      memset(&tls_config, 0, sizeof(tls_config));
      tls_config.peer_address = runtime->config.peer_address;
      tls_config.server_ca = &runtime->config.ca;
      tls_config.client_certificate = &runtime->config.certificate;
      tls_config.client_key = &runtime->config.private_key;
      tls_config.now_ms = bkvoice_config_now_ms;
      tls_config.trusted_time = bkvoice_config_trusted_time;
      tls_config.clock_context = &runtime->config;
      ret = bkvoice_tls_initialize(&runtime->tls, &tls_config);
      if (ret < 0)
        {
          return ret;
        }

      wss_config.host = runtime->config.host;
      wss_config.port = runtime->config.port;
      wss_config.path = "/companion/v1";
      wss_config.subprotocol = "companion-v1";
      ret = bkvoice_wss_initialize(&runtime->wss, bkvoice_tls_ops(),
                                   &runtime->tls, &wss_config);
      if (ret < 0)
        {
          (void)bkvoice_tls_uninitialize(&runtime->tls);
          return ret;
        }

      ret = bkvoice_session_initialize(&runtime->session, runtime->ptt,
                                       bkvoice_wss_transport_ops(),
                                       &runtime->wss,
                                       bkvoice_config_now_ms, NULL,
                                       &session_config,
                                       runtime->boot_generation);
      if (ret < 0)
        {
          (void)bkvoice_wss_uninitialize(&runtime->wss);
          (void)bkvoice_tls_uninitialize(&runtime->tls);
          return ret;
        }
    }

  runtime->armed = false;
  runtime->pressed = false;
  runtime->status_connection_generation = 0;
  runtime->status_health_sequence = 0;
  ret = bkvoice_session_connect(&runtime->session,
                                bkvoice_now() + BKVOICE_CONNECT_MS);
  if (ret < 0)
    {
      (void)bkvoice_runtime_disconnect(runtime, ret);
      return ret;
    }

  __atomic_store_n(&runtime->receiver_stop, false, __ATOMIC_RELEASE);
  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr, 8192);
      if (ret == 0)
        {
          ret = pthread_create(&runtime->receiver, &attr, bkvoice_receiver,
                                runtime);
        }

      (void)pthread_attr_destroy(&attr);
    }

  if (ret != 0)
    {
      (void)bkvoice_runtime_disconnect(runtime, -ret);
      return -ret;
    }

  runtime->receiver_joinable = true;
  runtime->last_error = 0;
  syslog(LOG_NOTICE, "BKVOICE COMPANION connected=1 ready=0 mtls=1\n");
  return 0;
}

static int bkvoice_decimal(const char *text, size_t limit, size_t *value)
{
  size_t result = 0;

  if (*text == '\0')
    {
      return -EINVAL;
    }

  for (; *text != '\0'; text++)
    {
      if (*text < '0' || *text > '9' || result > limit / 10u)
        {
          return -EINVAL;
        }

      result = result * 10u + (unsigned int)(*text - '0');
      if (result > limit)
        {
          return -EINVAL;
        }
    }

  *value = result;
  return 0;
}

static int bkvoice_hex(char value)
{
  if (value >= '0' && value <= '9')
    {
      return value - '0';
    }

  if (value >= 'a' && value <= 'f')
    {
      return value - 'a' + 10;
    }

  if (value >= 'A' && value <= 'F')
    {
      return value - 'A' + 10;
    }

  return -1;
}

#ifdef CONFIG_BK7258_PROVISION_GATT
static bool bkvoice_provision_available(void *context)
{
  struct bkvoice_runtime_s *runtime = context;
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return false;
#endif
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
  if (runtime->wake_session != NULL &&
      bkvoice_wake_session_suspend(runtime->wake_session) < 0)
    {
      return false;
    }
#endif
  return !runtime->config.initialized && !runtime->receiver_joinable &&
         !runtime->session.connected && !runtime->ptt->capture_ready &&
         runtime->upload == NULL && !runtime->cleanup_pending
#ifdef BKVOICE_RUNTIME_OTA
         && runtime->ota_store_ready
#endif
         ;
}
static int bkvoice_provision_load(void *context, const void *data, size_t size)
{
  struct bkvoice_runtime_s *runtime = context;
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return -ESHUTDOWN;
#endif
  return bkvoice_config_load(&runtime->config, data, size);
}
static int bkvoice_provision_load_cloud(void *context, const void *trust,
                                        size_t trust_size, const void *cloud,
                                        size_t cloud_size)
{
  struct bkvoice_runtime_s *runtime = context;
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return -ESHUTDOWN;
#endif
  int ret = bkvoice_config_load(&runtime->config, trust, trust_size);
  if (ret == 0)
    ret = bkcloud_runtime_create(&runtime->cloud, cloud, cloud_size,
                                 &runtime->config, runtime->ptt, runtime->wake);
  return ret;
}
static int bkvoice_provision_connect(void *context)
{
  struct bkvoice_runtime_s *runtime = context;
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return -ESHUTDOWN;
#endif
  return runtime->cloud ? bkcloud_runtime_connect(runtime->cloud) :
                          bkvoice_runtime_connect(runtime);
}
static int bkvoice_provision_ready(void *context)
{
  struct bkvoice_runtime_s *runtime = context;
  if (runtime->cloud) return bkcloud_runtime_ready(runtime->cloud);
  if (!runtime->session.connected)
    return runtime->last_error < 0 ? runtime->last_error : -ENOTCONN;
  return runtime->session.ready ? 1 : 0;
}
static int bkvoice_provision_clear(void *context)
{
  struct bkvoice_runtime_s *runtime = context;
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return -ESHUTDOWN;
#endif
  return bkvoice_runtime_clear(runtime);
}
static const struct bkprov_voice_ops_s g_provision_voice =
{
  bkvoice_provision_available, bkvoice_provision_load, bkvoice_provision_connect,
  bkvoice_provision_ready, bkvoice_provision_clear, bkvoice_provision_load_cloud
};

static int bkvoice_identity_bind(struct bkvoice_runtime_s *runtime)
{
  int ret = bkprov_network_bind(&runtime->identity, &g_provision_voice, runtime);
  if (ret == 0)
    ret = bkprov_owner_bind(&runtime->identity.certificate, &runtime->identity.key,
                            runtime->identity.secret, bkprov_network_ops(), NULL);
  if (ret < 0) (void)bkprov_network_unbind();
  else runtime->identity_bound = true;
  return ret;
}

static void bkvoice_identity_progress(struct bkvoice_runtime_s *runtime)
{
  uint64_t now;

#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return;
#endif
#ifdef BKVOICE_RUNTIME_OTA
  if (__atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
      BKVOICE_OTA_JOB_EMPTY)
    {
      return;
    }
#endif
  now = bkvoice_now();
  if (runtime->identity_bind_pending)
    {
      int ret;

      if (now < runtime->provision_restore_ms) return;
      ret = bkvoice_identity_bind(runtime);
      if (ret == -EBUSY)
        {
          runtime->identity_result = -EAGAIN;
          runtime->provision_restore_ms = now + 1000;
          return;
        }

      runtime->identity_bind_pending = false;
      runtime->identity_result = ret;
      if (ret < 0 && ret != -EAGAIN) runtime->last_error = ret;
      return;
    }
  if (runtime->identity_pending)
    {
      int ret = bkprov_storage_identity_install(runtime->identity.record, runtime->identity.size);
      if (ret == -EAGAIN) return;
      if (ret == 0)
        {
          ret = bkvoice_identity_bind(runtime);
          if (ret == -EBUSY)
            {
              runtime->identity_bind_pending = true;
              runtime->identity_result = -EAGAIN;
              runtime->provision_restore_ms = now + 1000;
              runtime->identity_pending = false;
              return;
            }
        }
      runtime->identity_result = ret;
      runtime->identity_pending = false;
      if (ret < 0) runtime->last_error = ret;
      return;
    }
  if (runtime->identity.record != NULL || runtime->upload != NULL ||
      now < runtime->provision_restore_ms) return;
  runtime->provision_restore_ms = now + 1000;
  uint8_t *record = malloc(BKVOICE_CONFIG_MAX_BYTES);
  if (record == NULL) return;
  size_t size;
  int ret = bkprov_storage_identity(record, BKVOICE_CONFIG_MAX_BYTES, &size);
  if (ret == 0)
    {
      ret = bkprov_identity_load(&runtime->identity, record, size);
      if (ret == 0)
        {
          ret = bkvoice_identity_bind(runtime);
          if (ret == -EBUSY)
            {
              runtime->identity_bind_pending = true;
              runtime->identity_result = -EAGAIN;
              runtime->provision_restore_ms = now + 1000;
              ret = -EAGAIN;
            }
        }
      runtime->identity_result = ret;
      if (ret < 0 && ret != -EAGAIN) runtime->last_error = ret;
    }
  else if (ret == -ENODEV || ret == -EXDEV || ret == -ENOTCONN)
    {
      (void)bkprov_storage_refresh();
    }
  mbedtls_platform_zeroize(record, BKVOICE_CONFIG_MAX_BYTES);
  free(record);
}

static void bkvoice_configuration_progress(struct bkvoice_runtime_s *runtime)
{
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return;
#endif
#ifdef BKVOICE_RUNTIME_OTA
  if (__atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
      BKVOICE_OTA_JOB_EMPTY)
    {
      return;
    }
#endif
  if (!runtime->identity_bound || runtime->identity_pending ||
      bkprov_network_busy() || bkprov_owner_busy() ||
      !bkvoice_provision_available(runtime) ||
      bkvoice_now() < runtime->configuration_restore_ms) return;
  runtime->configuration_restore_ms = bkvoice_now() + 10000;
  uint8_t *bundle = malloc(BKPROV_BUNDLE_MAX);
  if (bundle == NULL) return;
  size_t size;
  uint64_t revision;
  uint8_t transaction[16];
  int ret = bkprov_storage_snapshot(bundle, BKPROV_BUNDLE_MAX, &size, &revision, transaction);
  if (ret == 0) ret = bkprov_network_restore(bundle, size);
  if (ret < 0 && ret != -ENOENT && ret != -EAGAIN) runtime->last_error = ret;
  mbedtls_platform_zeroize(bundle, BKPROV_BUNDLE_MAX);
  free(bundle);
}

static void bkvoice_control_progress(struct bkvoice_runtime_s *runtime)
{
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return;
#endif
  if (!runtime->identity_bound || runtime->identity_pending ||
      bkprov_owner_busy() || bkprov_network_busy() ||
      bkvoice_now() < runtime->control_refresh_ms) return;
  runtime->control_refresh_ms = bkvoice_now() + 5000u;
  uint8_t *bundle = malloc(BKPROV_BUNDLE_MAX);
  if (bundle == NULL) return;
  size_t size;
  uint64_t revision;
  uint8_t transaction[16];
  struct bkprov_settings_s settings;
  memset(&settings, 0, sizeof(settings));
  int ret = bkprov_storage_snapshot(bundle, BKPROV_BUNDLE_MAX, &size,
                                    &revision, transaction);
  if (ret == 0) ret = bkprov_settings_decode(&settings, bundle, size);
  if (ret == 0 && settings.control_key != NULL)
    {
      ret = bkprov_owner_control(settings.control_key,
                                 bkvoice_runtime_control, runtime);
#ifdef BKVOICE_RUNTIME_OTA
      if (ret == 0)
        {
          ret = bkprov_owner_control_ota(bkvoice_runtime_control_ota);
        }
#endif
      if (ret < 0)
        {
          (void)bkprov_owner_control(NULL, NULL, NULL);
          runtime->last_error = ret;
        }
      if (runtime->cloud != NULL)
        (void)bkcloud_runtime_memory_owner(runtime->cloud, settings.control_key);
    }
  else
    (void)bkprov_owner_control(NULL, NULL, NULL);
  mbedtls_platform_zeroize(&settings, sizeof(settings));
  mbedtls_platform_zeroize(bundle, BKPROV_BUNDLE_MAX);
  free(bundle);
}
#endif

int bkvoice_runtime_command(const struct bkvoice_rpc_request_s *request,
                            struct bkvoice_rpc_response_s *response)
{
  struct bkvoice_runtime_s *runtime = &g_runtime;
  size_t count;
  size_t offset;
  int ret = 0;

#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return -ESHUTDOWN;
#endif

#ifdef CONFIG_BK7258_PROVISION_GATT
  if (bkprov_owner_busy() || bkprov_network_busy()) return -EBUSY;
  if (runtime->identity_pending && request->command != BKVOICE_RPC_CONFIG_COMMIT)
    return -EBUSY;
#endif

  switch (request->command)
    {
#ifdef CONFIG_BK7258_VOICE_HIL_TEST
      case BKVOICE_RPC_HIL_CAPTURE:
        ret = bkvoice_decimal(request->manifest, 5000, &count);
        if (ret < 0 || count < 200) return -EINVAL;
        if (runtime->cloud)
          {
            struct bkcloud_runtime_status_s cloud;
            bkcloud_runtime_status(runtime->cloud, &cloud);
            if (!cloud.ready) return -ENOTCONN;
            if (runtime->test_until || !cloud.armed || cloud.busy)
              return -EBUSY;
            runtime->test_until = bkvoice_now() + count;
            syslog(LOG_NOTICE, "BKVOICE HIL cloud start ms=%u physical=0\n",
                   (unsigned int)count);
            break;
          }
        if (!runtime->session.ready) return -ENOTCONN;
        if (runtime->test_until != 0 || runtime->pressed || !runtime->armed ||
            runtime->ptt->turn.state != BKVOICE_TURN_IDLE) return -EBUSY;
        runtime->test_until = bkvoice_now() + count;
        syslog(LOG_NOTICE, "BKVOICE HIL start ms=%u physical=0\n",
               (unsigned int)count);
        break;
#endif
      case BKVOICE_RPC_CONNECT:
        ret = runtime->cloud ? -EALREADY : bkvoice_runtime_connect(runtime);
        break;

      case BKVOICE_RPC_DISCONNECT:
        ret = runtime->cloud ? bkvoice_runtime_clear(runtime) :
                               bkvoice_runtime_disconnect(runtime, -ECANCELED);
        break;

      case BKVOICE_RPC_CONFIG_CLEAR:
        ret = bkvoice_runtime_clear(runtime);
        break;

      case BKVOICE_RPC_CONFIG_BEGIN:
        if (bkvoice_runtime_busy())
          {
            return -EBUSY;
          }

        ret = bkvoice_decimal(request->manifest, BKVOICE_CONFIG_MAX_BYTES,
                              &count);
        if (ret < 0 || count < 32)
          {
            return -EINVAL;
          }

        ret = bkvoice_runtime_clear(runtime);
        if (ret < 0)
          {
            return ret;
          }

        runtime->upload = calloc(1, count);
        if (runtime->upload == NULL)
          {
            return -ENOMEM;
          }

        runtime->upload_size = count;
        runtime->upload_deadline = bkvoice_now() + BKVOICE_UPLOAD_MS;
        break;

      case BKVOICE_RPC_CONFIG_DATA:
        count = strlen(request->manifest);
        ret = bkvoice_decimal(request->clip_id, BKVOICE_CONFIG_MAX_BYTES,
                              &offset);
        if (runtime->upload == NULL || ret < 0 || count == 0 ||
            count % 2 != 0 || count > BKVOICE_PROVISION_CHUNK_BYTES * 2u ||
            offset > runtime->uploaded ||
            count / 2u > runtime->upload_size - offset)
          {
            return -EINVAL;
          }

        if (offset < runtime->uploaded &&
            count / 2u > runtime->uploaded - offset)
          {
            return -EINVAL;
          }

        for (size_t i = 0; i < count; i += 2)
          {
            int high = bkvoice_hex(request->manifest[i]);
            int low = bkvoice_hex(request->manifest[i + 1]);
            if (high < 0 || low < 0)
              {
                bkvoice_upload_clear(runtime);
                return -EINVAL;
              }

            if (offset < runtime->uploaded &&
                runtime->upload[offset + i / 2u] != (high * 16 + low))
              {
                bkvoice_upload_clear(runtime);
                return -EPROTO;
              }

            runtime->upload[offset + i / 2u] = high * 16 + low;
          }

        if (offset == runtime->uploaded)
          {
            runtime->uploaded += count / 2u;
          }

        runtime->upload_deadline = bkvoice_now() + BKVOICE_UPLOAD_MS;
        response->data_bytes = runtime->uploaded;
        break;

      case BKVOICE_RPC_CONFIG_COMMIT:
        if (runtime->upload == NULL ||
            runtime->uploaded != runtime->upload_size)
          {
            return -ENODATA;
          }

#ifdef CONFIG_BK7258_PROVISION_GATT
        if (runtime->upload_size >= 4 && !memcmp(runtime->upload, "BPI1", 4))
          {
            if (runtime->identity.record == NULL)
              {
                ret = bkprov_identity_load(&runtime->identity, runtime->upload,
                                            runtime->upload_size);
                if (ret == 0)
                  {
                    runtime->identity_pending = true;
                    runtime->identity_result = -EAGAIN;
                  }
              }
            else if (runtime->identity.size != runtime->upload_size ||
                     memcmp(runtime->identity.record, runtime->upload, runtime->upload_size))
              ret = -EEXIST;
            if (ret == 0)
              {
                bkvoice_identity_progress(runtime);
                ret = runtime->identity_pending || runtime->identity_bind_pending ?
                      -EAGAIN : runtime->identity_result;
              }
            if (ret == -EAGAIN)
              {
                runtime->upload_deadline = bkvoice_now() + BKVOICE_UPLOAD_MS;
                return ret;
              }
          }
        else
#endif
          ret = bkvoice_config_load(&runtime->config, runtime->upload,
                                    runtime->upload_size);
        bkvoice_upload_clear(runtime);
        break;

      default:
        ret = -ENOSYS;
        break;
    }

  if (ret < 0)
    {
      runtime->last_error = ret;
    }

  return ret;
}

void bkvoice_runtime_step(bool command_link)
{
  struct bkvoice_runtime_s *runtime = &g_runtime;
#ifndef CONFIG_BK7258_PRODUCT_KEYS
  struct bkvoice_turn_token_s token;
#endif
  uint64_t now = bkvoice_now();
  uint32_t epoch = 0;
  bool level = false;
  bool link = false;
#ifndef CONFIG_BK7258_PRODUCT_KEYS
  irqstate_t flags;
#endif
  int ret;

#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime))
    {
      (void)bkvoice_soft_off_progress(runtime);
      return;
    }
#endif

#ifdef BKVOICE_RUNTIME_OTA
  bkvoice_ota_restore_progress(runtime);
#endif
#ifdef CONFIG_BK7258_PROVISION_GATT
  bkvoice_identity_progress(runtime);
  bkprov_network_step();
  bkvoice_configuration_progress(runtime);
  bkvoice_control_progress(runtime);
  now = bkvoice_now();
#ifndef CONFIG_BK7258_PRODUCT_KEYS
  /* Lease snapshot is taken before any early return, including offline voice.
   * Only this worker can translate physical input into a local confirmation.
   */
  flags = spin_lock_irqsave(&runtime->button_lock);
  epoch = runtime->button_epoch;
  link = command_link && runtime->button_link && runtime->button_sequence != 0 &&
         now >= runtime->button_received_ms &&
         now - runtime->button_received_ms < BKVOICE_BUTTON_LEASE_MS;
  level = runtime->button_pressed;
  spin_unlock_irqrestore(&runtime->button_lock, flags);
  bool pairing = bkprov_owner_step(now, epoch, link, level,
                     runtime->cloud ? !bkcloud_runtime_busy(runtime->cloud) :
                     !runtime->session.connected && !runtime->receiver_joinable &&
                     !runtime->ptt->capture_ready && runtime->upload == NULL &&
                     !runtime->cleanup_pending && !bkprov_network_busy());
#else
  bool pairing = bkprov_owner_step(now, 0, false, false,
                     runtime->cloud ? !bkcloud_runtime_busy(runtime->cloud) :
                     !runtime->session.connected && !runtime->receiver_joinable &&
                     !runtime->ptt->capture_ready && runtime->upload == NULL &&
                     !runtime->cleanup_pending && !bkprov_network_busy());
#endif
#endif

#ifdef CONFIG_BK7258_PRODUCT_KEYS
  /* Product keys are never a provisioning confirmation or a PTT level. */
  bkvoice_runtime_product_keys(runtime, command_link, now);
  epoch = runtime->boot_generation;
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime))
    {
      (void)bkvoice_soft_off_progress(runtime);
      return;
    }
#endif
#endif

#ifdef BKVOICE_RUNTIME_OTA
  if (runtime->ota_direct &&
      __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
        BKVOICE_OTA_JOB_EMPTY)
    {
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
      if (runtime->wake_session != NULL)
        {
          ret = bkvoice_wake_session_step(runtime->wake_session, false, now);
          if (ret < 0)
            {
              runtime->last_error = ret;
              return;
            }
        }
#endif
      ret = bkvoice_ota_step(runtime);
      if (ret < 0)
        {
          runtime->last_error = ret;
          syslog(LOG_WARNING, "BKVOICE OTA progress_fail=%d\n", ret);
        }

      return;
    }
#endif

  if (runtime->cloud)
    {
#ifdef CONFIG_BK7258_PROVISION_GATT
      /* A successful service probe is not a committed configuration.  The
       * configuration transaction remains exclusive until it commits.
       */
      if (bkprov_network_busy())
        {
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
          if (runtime->wake_session != NULL)
            {
              ret = bkvoice_wake_session_step(runtime->wake_session, false,
                                              now);
              if (ret < 0) runtime->last_error = ret;
            }
#endif
          return;
        }
      if (pairing)
        {
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
          if (runtime->wake_session != NULL)
            {
              ret = bkvoice_wake_session_step(runtime->wake_session, false,
                                              now);
              if (ret < 0)
                {
                  runtime->last_error = ret;
                  return;
                }
            }
#endif
          if (bkcloud_runtime_busy(runtime->cloud))
            {
              (void)bkcloud_runtime_cancel(runtime->cloud);
              bkcloud_runtime_step(runtime->cloud, false, false, epoch);
            }
          return;
        }
#endif
#ifdef CONFIG_BK7258_PRODUCT_KEYS
      /* Service transport follows its command link, never a GPIO lease. */
      epoch = runtime->boot_generation;
      link = command_link;
      level = false;
#else
      flags = spin_lock_irqsave(&runtime->button_lock);
      epoch = runtime->button_epoch;
      level = runtime->button_pressed;
      link = command_link && runtime->button_link && runtime->button_sequence != 0 &&
             now >= runtime->button_received_ms &&
             now - runtime->button_received_ms < BKVOICE_BUTTON_LEASE_MS;
      spin_unlock_irqrestore(&runtime->button_lock, flags);
#endif
#ifdef CONFIG_BK7258_VOICE_HIL_TEST
      if (runtime->test_until)
        {
          link = true;
          level = bkvoice_now() < runtime->test_until;
        }
#endif
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
      ret = bkvoice_wake_prepare(runtime);
      if (ret == 0)
        {
          if (level)
            {
              ret = bkvoice_wake_session_suspend(runtime->wake_session);
            }
          else
            {
              /* Continuous listening follows the authenticated service
               * channel, not the physical button lease.  Disconnect drains
               * a wake-owned turn and releases the microphone. */
              ret = bkvoice_wake_session_step(runtime->wake_session,
                                              command_link, now);
            }
          if (ret < 0 && ret != -EAGAIN && ret != runtime->wake_result)
            {
              runtime->last_error = ret;
              runtime->wake_result = ret;
              syslog(LOG_WARNING, "BKVOICE WAKE progress_fail=%d\n", ret);
            }
          else if (ret >= 0)
            {
              runtime->wake_result = 0;
            }
          /* Only an instantiated listener can hold the MIC needed by the
           * manual/HIL entry.  A missing or rejected model never blocks it. */
          if (level && ret < 0)
            {
              return;
            }
        }
#endif
      bkcloud_runtime_step(runtime->cloud, link, level, epoch);
#ifdef CONFIG_BK7258_VOICE_HIL_TEST
      if (runtime->test_until && bkvoice_now() >= runtime->test_until &&
          !bkcloud_runtime_busy(runtime->cloud)) runtime->test_until = 0;
#endif
      return;
    }

  if (runtime->cleanup_pending)
    {
      (void)bkvoice_runtime_disconnect(runtime, -ECANCELED);
      return;
    }

  if (runtime->upload != NULL &&
      (!command_link || now >= runtime->upload_deadline))
    {
      bkvoice_upload_clear(runtime);
    }

  if (!command_link)
    {
      if (runtime->session.connected || runtime->receiver_joinable)
        {
          (void)bkvoice_runtime_disconnect(runtime, -ENOTCONN);
        }

      return;
    }

  for (unsigned int i = 0; i < BKVOICE_RX_SLOTS; i++)
    {
      uint32_t tail = __atomic_load_n(&runtime->rx_tail, __ATOMIC_RELAXED);
      uint32_t head = __atomic_load_n(&runtime->rx_head, __ATOMIC_ACQUIRE);
      bool ready = runtime->session.ready;

      if (tail == head)
        {
          break;
        }

      ret = bkvoice_session_dispatch_frame(&runtime->session,
                                           &runtime->rx[tail % BKVOICE_RX_SLOTS]);
      __atomic_store_n(&runtime->rx_tail, tail + 1u, __ATOMIC_RELEASE);
      if (ret < 0 && ret != -ESTALE)
        {
          runtime->last_error = ret;
          syslog(LOG_WARNING, "BKVOICE COMPANION rx_fail=%d\n", ret);
          (void)bkvoice_runtime_disconnect(runtime, ret);
          break;
        }

      if (!ready && runtime->session.ready)
        {
          syslog(LOG_NOTICE, "BKVOICE COMPANION connected=1 ready=1 mtls=1\n");
        }
    }

#ifdef BKVOICE_RUNTIME_OTA
  ret = bkvoice_ota_step(runtime);
  if (ret < 0)
    {
      runtime->last_error = ret;
      syslog(LOG_WARNING, "BKVOICE OTA progress_fail=%d\n", ret);
      (void)bkvoice_runtime_disconnect(runtime, ret);
      return;
    }
#endif

  ret = bkvoice_session_poll(&runtime->session);
  if (ret < 0)
    {
      runtime->last_error = ret;
      syslog(LOG_WARNING, "BKVOICE playback completion failed=%d\n", ret);
      (void)bkvoice_runtime_disconnect(runtime, ret);
      return;
    }

#ifndef CONFIG_BK7258_PRODUCT_KEYS
  flags = spin_lock_irqsave(&runtime->button_lock);
  epoch = runtime->button_epoch;
  link = runtime->button_link && runtime->button_sequence != 0 &&
         now >= runtime->button_received_ms &&
         now - runtime->button_received_ms < BKVOICE_BUTTON_LEASE_MS;
  level = runtime->button_pressed;
  spin_unlock_irqrestore(&runtime->button_lock, flags);

#ifdef CONFIG_BK7258_VOICE_HIL_TEST
  bool test_active = runtime->test_until != 0;
  if (test_active)
    {
      link = true;
      level = bkvoice_now() < runtime->test_until;
    }
#endif

  if (epoch != runtime->owner_button_epoch || !link ||
#ifdef CONFIG_BK7258_PROVISION_GATT
      pairing || bkprov_network_busy() ||
#endif
#ifdef BKVOICE_RUNTIME_OTA
      __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
        BKVOICE_OTA_JOB_EMPTY ||
#endif
      !runtime->session.ready)
    {
      if (runtime->pressed && runtime->session.ready)
        {
          ret = bkvoice_session_cancel(&runtime->session, -ENOTCONN);
          runtime->last_error = ret < 0 ? ret : -ENOTCONN;
          runtime->cleanup_pending = ret < 0;
          syslog(LOG_WARNING, "BKVOICE PTT lease_lost=1 ret=%d\n", ret);
        }

      runtime->pressed = false;
      runtime->armed = false;
      runtime->owner_button_epoch = epoch;
    }
  else if (!level)
    {
      if (runtime->pressed)
        {
          ret = bkvoice_session_ptt_up(&runtime->session, now);
          runtime->last_error = ret;
          runtime->cleanup_pending = ret < 0;
          syslog(LOG_NOTICE, "BKVOICE PTT released=1 ret=%d\n", ret);
        }

      runtime->pressed = false;
      runtime->armed = true;
    }
  else if (runtime->armed && !runtime->pressed)
    {
      runtime->armed = false;
      ret = bkvoice_session_ptt_down(&runtime->session, now, &token);
      runtime->last_error = ret;
      runtime->cleanup_pending = ret < 0 && runtime->ptt->worker_joinable;
      runtime->pressed = ret >= 0;
      if (ret >= 0)
        {
#ifdef CONFIG_BK7258_VOICE_HIL_TEST
          if (!test_active)
#endif
            runtime->presses++;
        }

      syslog(LOG_NOTICE, "BKVOICE PTT pressed=1 accepted=%u ret=%d\n",
             ret >= 0 ? 1 : 0, ret);
    }

#ifdef CONFIG_BK7258_VOICE_HIL_TEST
  if (test_active && (!level || !runtime->pressed))
    {
      runtime->test_until = 0;
    }
#endif

#endif /* !CONFIG_BK7258_PRODUCT_KEYS */

  if (runtime->session.ready)
    {
      ret = bkvoice_session_timeout(&runtime->session, now);
      if (ret < 0)
        {
          runtime->last_error = ret;
          runtime->armed = false;
          runtime->pressed = false;
          runtime->cleanup_pending = true;
        }
    }

  ret = bkvoice_runtime_report_device_status(runtime);
  if (ret < 0 && ret != -EBUSY)
    {
      runtime->last_error = ret;
      syslog(LOG_WARNING, "BKVOICE COMPANION status_fail=%d\n", ret);
      (void)bkvoice_runtime_disconnect(runtime, ret);
    }
}

int bkvoice_runtime_control(void *context, enum bkcontrol_command_e command,
                            uint32_t value, struct bkcontrol_status_s *status)
{
  struct bkcloud_runtime_status_s cloud;
  int ret = 0;
  (void)context;
  if (status == NULL) return -EINVAL;
  memset(status, 0xff, sizeof(*status));
  status->flags = 0;
  status->error = 0;
  if (command == BKCONTROL_INFO)
    {
#ifdef CONFIG_BK7258_OTA_MANAGER
      struct bk7258_ota_pair_snapshot_s pair;
      int pair_ret;

      if (!g_runtime.firmware_identity_valid)
        {
          return -EAGAIN;
        }

      memset(&pair, 0, sizeof(pair));
      pair_ret = bk7258_ota_rpmsg_pair_status(
        &pair, CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
      if (pair_ret < 0 || pair.state != BK7258_OTA_PAIR_CONFIRMED ||
          !pair.security_counter_present || pair.security_counter == 0u ||
          !bk7258_mcuboot_version_equal(&pair.version,
                                        &g_runtime.firmware_version))
        {
          return -EAGAIN;
        }

      status->device_info.major = g_runtime.firmware_version.major;
      status->device_info.minor = g_runtime.firmware_version.minor;
      status->device_info.revision = g_runtime.firmware_version.revision;
      status->device_info.build = g_runtime.firmware_version.build;
      status->device_info.security_counter = pair.security_counter;
      return 0;
#else
      return -EAGAIN;
#endif
    }
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(&g_runtime) && command != BKCONTROL_STATUS)
    return -ESHUTDOWN;
#endif
  /* Identity/version and local link status remain available when the cloud
   * service has not been configured or cannot be created.  Mutations retain
   * their existing readiness gate.
   */
  if (g_runtime.cloud == NULL && command != BKCONTROL_STATUS) return -ENOTCONN;
  switch (command)
    {
      case BKCONTROL_STATUS:
        break;
      case BKCONTROL_CANCEL:
        ret = bkcloud_runtime_cancel(g_runtime.cloud);
        break;
      case BKCONTROL_CLEAR_HISTORY:
        if (bkvoice_runtime_settings_busy()) return -EBUSY;
        ret = bkcloud_runtime_clear_history(g_runtime.cloud);
        break;
      case BKCONTROL_MEMORY_SET:
      case BKCONTROL_MEMORY_DELETE:
        if (command == BKCONTROL_MEMORY_SET && value > 1u) return -EINVAL;
        if (bkvoice_runtime_settings_busy()) return -EBUSY;
        ret = bkcloud_runtime_memory_set(g_runtime.cloud, value != 0,
                                         command == BKCONTROL_MEMORY_DELETE);
        break;
      case BKCONTROL_VOLUME:
      case BKCONTROL_PERSONA:
        if (value > (command == BKCONTROL_VOLUME ? 100u : 4u)) return -EINVAL;
        if (bkvoice_runtime_settings_busy()) return -EBUSY;
#ifdef CONFIG_BK7258_PREFERENCES
        ret = command == BKCONTROL_VOLUME ? bk7258_preferences_set_volume(value) :
              bk7258_preferences_set_persona(bk7258_preferences_persona_name(value));
#else
        ret = -ENOTSUP;
#endif
        break;
      default:
        return -EINVAL;
    }
  if (ret < 0) return ret;
  bkcloud_runtime_status(g_runtime.cloud, &cloud);
  status->flags = (cloud.ready ? 1u : 0u) | (cloud.busy ? 2u : 0u) |
      (cloud.memory_known ? 32u : 0u) | (cloud.memory_enabled ? 64u : 0u) |
      (cloud.memory_pending ? 128u : 0u) | (cloud.memory_failed ? 256u : 0u) |
      (cloud.memory_supported ? 512u : 0u);
#ifdef CONFIG_BK7258_OTA_MANAGER
  status->flags |= 4096u;
#endif
#ifdef BKVOICE_RUNTIME_OTA
  status->flags |= 8192u;
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  /* A coherent local link snapshot plus the AP lease, not an Internet or
   * provider reachability claim. No credential, address or SSID is exposed.
   */
  struct bk7258_wifi_result_s wifi;
  if (bk7258_wifi_read_link(&wifi) == 0)
    {
      status->flags |= 1024u;
      if (wifi.ipaddr != 0 && bk7258_wifi_native_lease_matches(&wifi))
        status->flags |= 2048u;
    }
#endif
  if (cloud.turn_state != UINT32_MAX)
    {
      status->flags |= 4u;
      status->turn = cloud.turn_state;
      status->error = cloud.last_error;
    }
#ifdef CONFIG_BK7258_PREFERENCES
  /* Never add filesystem work to cancellation or an in-flight audio turn.
   * A failed preference read leaves the fields explicitly unknown.
   */
  if (command != BKCONTROL_CANCEL && command != BKCONTROL_CLEAR_HISTORY &&
      !cloud.busy &&
      cloud.turn_state == BKVOICE_TURN_IDLE)
    {
      struct bk7258_preferences_s preferences;
      if (bk7258_preferences_get(&preferences) == 0)
        {
          status->flags |= 8u | 16u;
          status->volume = preferences.volume_percent;
          status->persona = preferences.persona;
        }
    }
#endif
  return 0;
}

int bkvoice_runtime_control_ota(void *context,
                               enum bkcontrol_command_e command,
                               const uint8_t *record, size_t size,
                               struct bkcontrol_status_s *status)
{
#ifdef BKVOICE_RUNTIME_OTA
  struct bkvoice_runtime_s *runtime = context;
  struct bkcontrol_ota_request_s *request;
  bool direct;
  int terminal_result;
  int ret;

  if (runtime != &g_runtime || status == NULL)
    {
      return -EINVAL;
    }

  bkvoice_ota_status_unknown(&status->ota);
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime) && command != BKCONTROL_OTA_STATUS)
    return -ESHUTDOWN;
#endif
  if (command == BKCONTROL_OTA_STATUS)
    {
      if (record != NULL || size != 0)
        {
          return -EINVAL;
        }

      return bkvoice_ota_control_status(runtime, &status->ota);
    }

  if (command == BKCONTROL_OTA_CANCEL)
    {
      if (record != NULL || size != 0)
        {
          return -EINVAL;
        }

      direct = runtime->ota_direct || runtime->ota_intent_present;
      ret = bkvoice_ota_cancel_control(runtime, &terminal_result);
      if (ret == 0)
        {
          if (direct)
            {
              runtime->ota_result = terminal_result;
              runtime->ota_target_valid = false;
              __atomic_store_n(&runtime->ota_job_state,
                               BKVOICE_OTA_JOB_DONE, __ATOMIC_RELEASE);
            }

          ret = bkvoice_ota_store_clear_runtime(runtime);
          if (ret == 0)
            {
              bkvoice_ota_clear(runtime);
            }
        }

      if (ret < 0)
        {
          (void)bkvoice_ota_control_status(runtime, &status->ota);
          return ret;
        }

      bkvoice_ota_status_unknown(&runtime->ota_direct_status);
      if (direct)
        {
          runtime->ota_direct_status.state = BKVOICE_CONTROL_OTA_TERMINAL;
          runtime->ota_direct_status.phase = BKVOICE_COMPANION_OTA_FAILED;
          runtime->ota_direct_status.result = terminal_result;
          runtime->ota_direct_status_valid = true;
        }
      else
        {
          runtime->ota_direct_status_valid = false;
        }

      status->ota = runtime->ota_direct_status;
      return ret;
    }

  if (command != BKCONTROL_OTA_START || record == NULL)
    {
      return -EINVAL;
    }

  request = calloc(1, sizeof(*request));
  if (request == NULL)
    {
      return -ENOMEM;
    }

  ret = bkcontrol_ota_request_parse(record, size, request);
  if (ret < 0)
    {
      goto free_request;
    }

  if (!runtime->initialized || !runtime->firmware_identity_valid)
    {
      ret = -EAGAIN;
      goto free_request;
    }

  if (!runtime->ota_store_ready)
    {
      ret = -EAGAIN;
      goto free_request;
    }

#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
  if (runtime->wake_session != NULL)
    {
      ret = bkvoice_wake_session_step(runtime->wake_session, false,
                                      bkvoice_now());
      if (ret < 0)
        {
          goto free_request;
        }
    }
#endif

  if (runtime->ota_direct_request != NULL ||
      __atomic_load_n(&runtime->ota_job_state, __ATOMIC_ACQUIRE) !=
        BKVOICE_OTA_JOB_EMPTY ||
      (runtime->cloud != NULL && bkcloud_runtime_busy(runtime->cloud)) ||
      runtime->receiver_joinable || runtime->session.connected ||
      runtime->upload != NULL || runtime->cleanup_pending || runtime->pressed ||
      (runtime->ptt != NULL && !bkvoice_ptt_quiescent(runtime->ptt)))
    {
      ret = -EBUSY;
      goto free_request;
    }
#ifdef CONFIG_BK7258_PROVISION_GATT
  if (runtime->identity_pending || bkprov_network_busy())
    {
      ret = -EBUSY;
      goto free_request;
    }
#endif

  runtime->ota_direct_request = request;
  runtime->ota_direct = true;
  runtime->ota_direct_status_valid = true;
  bkvoice_ota_status_unknown(&runtime->ota_direct_status);
  runtime->ota_direct_status.state = BKVOICE_CONTROL_OTA_QUEUED;
  runtime->ota_direct_status.phase = BKVOICE_COMPANION_OTA_DOWNLOADING;
  runtime->ota_direct_status.progress = 0u;
  runtime->ota_direct_status.total = 100u;
  runtime->ota_direct_status.result = -EINPROGRESS;
  ret = bkvoice_ota_request(runtime, 1u, request->catalog_sha256);
  if (ret < 0)
    {
      runtime->ota_direct_request = NULL;
      runtime->ota_direct = false;
      runtime->ota_direct_status_valid = false;
      goto free_request;
    }

  status->ota = runtime->ota_direct_status;
  return 0;

free_request:
  mbedtls_platform_zeroize(request, sizeof(*request));
  free(request);
  return ret;
#else
  (void)context;
  (void)command;
  (void)record;
  (void)size;
  (void)status;
  return -ENOTSUP;
#endif
}

bool bkvoice_runtime_settings_busy(void)
{
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(&g_runtime)) return true;
#endif
  if (!g_runtime.cloud) return bkvoice_runtime_busy();
#ifdef CONFIG_BK7258_PROVISION_GATT
  if (bkprov_owner_pairing() || bkprov_network_busy() || g_runtime.identity_pending)
    return true;
#endif
  struct bkcloud_runtime_status_s cloud;
  bkcloud_runtime_status(g_runtime.cloud, &cloud);
  return !cloud.ready || cloud.busy || cloud.turn_state != BKVOICE_TURN_IDLE;
}

bool bkvoice_runtime_busy(void)
{
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(&g_runtime)) return true;
#endif
  if (bkcloud_runtime_busy(g_runtime.cloud)) return true;
#ifdef CONFIG_BK7258_PROVISION_GATT
  if (bkprov_owner_busy() || bkprov_network_busy() || g_runtime.identity_pending) return true;
#endif
#ifdef BKVOICE_RUNTIME_OTA
  if (__atomic_load_n(&g_runtime.ota_job_state, __ATOMIC_ACQUIRE) !=
      BKVOICE_OTA_JOB_EMPTY)
    {
      return true;
    }
#endif
  return g_runtime.receiver_joinable || g_runtime.session.connected ||
         (g_runtime.ptt != NULL && g_runtime.ptt->capture_ready);
}

void bkvoice_runtime_status(struct bkvoice_rpc_response_s *response)
{
  struct bkvoice_runtime_s *runtime = &g_runtime;
  struct bkvoice_turn_snapshot_s turn;
  irqstate_t flags;
  uint64_t now = bkvoice_now();

  response->flags |= BKVOICE_STATUS_TLS_AVAILABLE;
  if (runtime->config.initialized)
    {
      response->flags |= BKVOICE_STATUS_CONFIGURED;
    }

  if (runtime->session.connected)
    {
      response->flags |= BKVOICE_STATUS_CONNECTED;
      response->flags &= ~BKVOICE_STATUS_LOCAL_ONLY;
    }

  if (runtime->session.ready)
    {
      response->flags |= BKVOICE_STATUS_GATEWAY_READY;
    }

  flags = spin_lock_irqsave(&runtime->button_lock);
  if (runtime->button_link && runtime->button_sequence != 0 &&
      now >= runtime->button_received_ms &&
      now - runtime->button_received_ms < BKVOICE_BUTTON_LEASE_MS)
    {
      response->flags |= BKVOICE_STATUS_PTT_LINK;
    }

  spin_unlock_irqrestore(&runtime->button_lock, flags);
  if (runtime->cloud)
    {
      struct bkcloud_runtime_status_s cloud;
      bkcloud_runtime_status(runtime->cloud, &cloud);
      response->flags |= BKVOICE_STATUS_CLOUD_MODE;
      response->flags &= ~BKVOICE_STATUS_LOCAL_ONLY;
      if (cloud.ready) response->flags |= BKVOICE_STATUS_CLOUD_READY;
      if (cloud.busy) response->flags |= BKVOICE_STATUS_CLOUD_BUSY;
      if (cloud.pressed) response->flags |= BKVOICE_STATUS_PTT_PRESSED;
      response->result.live.turn_state = cloud.turn_state;
      response->result.live.last_error = cloud.last_error;
      return;
    }
  if (runtime->pressed)
    {
      response->flags |= BKVOICE_STATUS_PTT_PRESSED;
    }

  bkvoice_turn_snapshot(&runtime->ptt->turn, &turn);
  response->result.live.turn_state = turn.state;
  response->result.live.presses = runtime->presses;
  response->result.live.last_error = runtime->last_error;
  if (runtime->session.initialized)
    {
      struct bkvoice_gateway_snapshot_s gateway;
      bkvoice_gateway_snapshot(&runtime->session.gateway, &gateway);
      response->data_bytes = gateway.tx_frames;
      response->duration_ms = gateway.rx_frames;
    }
}

int bkvoice_runtime_initialize(struct bkvoice_ptt_s *ptt,
                               uint32_t boot_generation, sem_t *wake)
{
  struct bkvoice_runtime_s *runtime = &g_runtime;
  int ret;

  if (ptt == NULL || wake == NULL || boot_generation == 0)
    {
      return -EINVAL;
    }

  runtime->ptt = ptt;
  runtime->boot_generation = boot_generation;
  runtime->wake = wake;
#ifdef BKVOICE_RUNTIME_SOFT_OFF
  __atomic_store_n(&runtime->soft_off_pending, false, __ATOMIC_RELEASE);
  runtime->soft_off_accepted = false;
  runtime->soft_off_blockdev_held = false;
  runtime->soft_off_failure = 0;
  runtime->soft_off_status_ms = 0;
  __atomic_store_n(&runtime->soft_off_mutations, 0u, __ATOMIC_RELEASE);
#endif
#ifdef CONFIG_BK7258_VOICE_WAKE_RUNTIME
  runtime->wake_session = NULL;
  runtime->wake_result = 0;
  runtime->wake_attempted = false;
#endif
#ifdef CONFIG_BK7258_OTA_MANAGER
  memset(&runtime->firmware_version, 0, sizeof(runtime->firmware_version));
  memset(runtime->firmware_root_sha256, 0,
         sizeof(runtime->firmware_root_sha256));
  runtime->firmware_identity_valid =
    bk7258_active_ap_image_version(&runtime->firmware_version) == 0 &&
    bk7258_ota_catalog_public_fingerprint(
      runtime->firmware_root_sha256) == 0;
#endif
  ret = sem_init(&runtime->receiver_done, 0, 0);
  if (ret < 0)
    {
      return -errno;
    }

#ifdef BKVOICE_RUNTIME_OTA
  bkvoice_ota_clear(runtime);
  memset(&runtime->ota_direct_status, 0,
         sizeof(runtime->ota_direct_status));
  runtime->ota_direct_status_valid = false;
  memset(&runtime->ota_intent, 0, sizeof(runtime->ota_intent));
  runtime->ota_store_ready = false;
  runtime->ota_intent_present = false;
  runtime->ota_store_revision = 0;
  runtime->ota_restore_ms = 0;
  ret = sem_init(&runtime->ota_done, 0, 0);
  if (ret < 0)
    {
      ret = -errno;
      (void)sem_destroy(&runtime->receiver_done);
      return ret;
    }

  ret = sem_init(&runtime->ota_target_decision, 0, 0);
  if (ret < 0)
    {
      ret = -errno;
      (void)sem_destroy(&runtime->ota_done);
      (void)sem_destroy(&runtime->receiver_done);
      return ret;
    }
#endif

  ret = rpmsg_register_callback(runtime, bkvoice_button_device_created,
                                bkvoice_button_device_destroyed, NULL, NULL);
  if (ret < 0)
    {
#ifdef BKVOICE_RUNTIME_OTA
      (void)sem_destroy(&runtime->ota_target_decision);
      (void)sem_destroy(&runtime->ota_done);
#endif
      (void)sem_destroy(&runtime->receiver_done);
    }

  else
    {
      runtime->initialized = true;
    }

  return ret;
}

int bkvoice_runtime_uninitialize(void)
{
  struct bkvoice_runtime_s *runtime = &g_runtime;
  int ret;

  if (!runtime->initialized)
    {
      return 0;
    }

#ifdef BKVOICE_RUNTIME_SOFT_OFF
  if (bkvoice_soft_off_pending(runtime)) return -EBUSY;
#endif

#ifdef CONFIG_BK7258_PROVISION_GATT
  if (bkprov_owner_busy() || bkprov_network_busy() || runtime->identity_pending) return -EBUSY;
  (void)bkprov_owner_unbind();
  (void)bkprov_network_unbind();
  bkprov_identity_clear(&runtime->identity);
  runtime->identity_pending = false;
  runtime->identity_bind_pending = false;
  runtime->identity_bound = false;
#endif

  ret = bkvoice_runtime_clear(runtime);
  if (ret < 0)
    {
      return ret;
    }

  rpmsg_unregister_callback(runtime, bkvoice_button_device_created,
                             bkvoice_button_device_destroyed, NULL, NULL);
  if (nxmutex_lock(&runtime->endpoint_lock) >= 0)
    {
      if (runtime->button_endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&runtime->button_endpoint);
          memset(&runtime->button_endpoint, 0,
                 sizeof(runtime->button_endpoint));
        }

      nxmutex_unlock(&runtime->endpoint_lock);
    }

  (void)sem_destroy(&runtime->receiver_done);
#ifdef BKVOICE_RUNTIME_OTA
  (void)sem_destroy(&runtime->ota_target_decision);
  (void)sem_destroy(&runtime->ota_done);
#endif
  runtime->initialized = false;
  runtime->wake = NULL;
  runtime->ptt = NULL;
  return 0;
}
