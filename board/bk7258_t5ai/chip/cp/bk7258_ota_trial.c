/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/bk7258_ota_trial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX/Beken adapter for the format-2 symmetric OTA metadata cores.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/clock.h>
#include <nuttx/signal.h>

#include <crypto/sha2.h>

#include <arch/chip/bk7258_ota_trial.h>
#include <arch/chip/bk7258_ota_staging.h>
#include <arch/chip/bk7258_amp.h>

#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
#include <arch/chip/bk7258_ota_fault.h>
#endif

#include <driver/flash.h>

#include "../../bootloader/boot_ota_rotation_core.h"
#include "../../bootloader/boot_ota_rotation_control_core.h"
#include "../../bootloader/boot_ota_rotation_publish_core.h"
#include "../../bootloader/boot_ota_rotation_health_core.h"
#include "bk7258_flash_guard.h"

#define BK7258_FLASH_REMAP_ENABLE 0x44030064u
#define BK7258_REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

static volatile bool g_bk7258_ota_trial_initialized;
static volatile bool g_bk7258_ota_trial_runtime_write;
static volatile uintptr_t g_bk7258_ota_trial_link_closure;

_Static_assert(sizeof(SHA2_CTX) <= BK7258_OTA_HASH_CONTEXT_MAX,
               "SHA-256 context exceeds portable OTA workspace");

static void bk7258_ota_publish_sha_init(void *context)
{
  sha256init((SHA2_CTX *)context);
}

static void bk7258_ota_publish_sha_update(void *context,
                                          const uint8_t *data,
                                          size_t len)
{
  sha256update((SHA2_CTX *)context, data, len);
}

static void bk7258_ota_publish_sha_final(
  void *context, uint8_t digest[BK7258_OTA_STAGE_SHA256_SIZE])
{
  sha256final(digest, (SHA2_CTX *)context);
}

static const struct bk7258_ota_hash_ops_s g_bk7258_ota_publish_hash_ops =
{
  .context_size = sizeof(SHA2_CTX),
  .init = bk7258_ota_publish_sha_init,
  .update = bk7258_ota_publish_sha_update,
  .final = bk7258_ota_publish_sha_final
};

static bool bk7258_ota_trial_compile_write(void *arg)
{
  (void)arg;
#ifdef CONFIG_BK7258_OTA_TRIAL_WRITE
  return true;
#else
  return false;
#endif
}

static bool bk7258_ota_trial_runtime_write(void *arg)
{
  (void)arg;
  return __atomic_load_n(&g_bk7258_ota_trial_runtime_write,
                         __ATOMIC_ACQUIRE);
}

static int bk7258_ota_trial_lock(void *arg, uint32_t timeout_ms)
{
  (void)arg;
  return bk7258_flash_guard_lock(BK7258_FLASH_GUARD_OTA_METADATA,
                                 true, timeout_ms);
}

static void bk7258_ota_trial_unlock(void *arg)
{
  (void)arg;
  bk7258_flash_guard_unlock();
}

static int bk7258_ota_trial_read(void *arg, uint32_t address, uint8_t *data,
                                 size_t len)
{
  int ret;

  (void)arg;
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
  ret = bk7258_ota_fault_before(BK7258_OTA_FAULT_TRIAL_READ);
  if (ret < 0)
    {
      return ret;
    }
#else
  (void)ret;
#endif
  return bk_flash_read_bytes(address, data, (uint32_t)len) == BK_OK ?
         0 : -EIO;
}

static int bk7258_ota_trial_write(void *arg, uint32_t address,
                                  const uint8_t *data, size_t len)
{
  int ret;

  (void)arg;
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
  ret = bk7258_ota_fault_before(BK7258_OTA_FAULT_TRIAL_WRITE);
  if (ret < 0)
    {
      return ret;
    }
#else
  (void)ret;
#endif
  return bk_flash_write_bytes(address, data, (uint32_t)len) == BK_OK ?
         0 : -EIO;
}

static int bk7258_ota_publish_raw_read(void *arg, uint32_t address,
                                       uint8_t *data, size_t len)
{
  (void)arg;
  return bk_flash_read_bytes(address, data, (uint32_t)len) == BK_OK ?
         0 : -EIO;
}

static int bk7258_ota_publish_read(void *arg, uint32_t address,
                                   uint8_t *data, size_t len)
{
  int ret;

  (void)arg;
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
  ret = bk7258_ota_fault_before(BK7258_OTA_FAULT_PUBLISH_READ);
  if (ret < 0)
    {
      return ret;
    }
#else
  (void)ret;
#endif
  return bk_flash_read_bytes(address, data, (uint32_t)len) == BK_OK ?
         0 : -EIO;
}

static uint64_t bk7258_ota_publish_now(void *arg)
{
  (void)arg;
  return (uint64_t)TICK2MSEC(clock_systime_ticks());
}

static enum bk7258_boot_ota_slot_e bk7258_ota_active_slot(void *arg)
{
  (void)arg;
  return (BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) & 1u) == 0 ?
         BK7258_BOOT_OTA_SLOT_A : BK7258_BOOT_OTA_SLOT_B;
}

static int bk7258_ota_publish_erase(void *arg, uint32_t address)
{
  int ret;

  (void)arg;
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
  ret = bk7258_ota_fault_before(BK7258_OTA_FAULT_PUBLISH_ERASE);
  if (ret < 0)
    {
      return ret;
    }
#else
  (void)ret;
#endif
  return bk_flash_erase_sector(address) == BK_OK ? 0 : -EIO;
}

static int bk7258_ota_publish_write(void *arg, uint32_t address,
                                    const uint8_t *data, size_t len)
{
  int ret;

  (void)arg;
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
  ret = bk7258_ota_fault_before(BK7258_OTA_FAULT_PUBLISH_WRITE);
  if (ret < 0)
    {
      return ret;
    }
#else
  (void)ret;
#endif
  return bk_flash_write_bytes(address, data, (uint32_t)len) == BK_OK ?
         0 : -EIO;
}

static const struct bk7258_boot_ota_trial_ops_s g_bk7258_ota_trial_ops =
{
  .arg = NULL,
  .compile_write_enabled = bk7258_ota_trial_compile_write,
  .runtime_write_enabled = bk7258_ota_trial_runtime_write,
  .lock = bk7258_ota_trial_lock,
  .unlock = bk7258_ota_trial_unlock,
  .read = bk7258_ota_trial_read,
  .write = bk7258_ota_trial_write
};

static const struct bk7258_boot_ota_raw_ops_s g_bk7258_ota_publish_raw_ops =
{
  .arg = NULL,
  .read = bk7258_ota_publish_raw_read
};

static const struct bk7258_boot_ota_rotation_publish_ops_s
  g_bk7258_ota_publish_ops =
{
  .arg = NULL,
  .now_ms = bk7258_ota_publish_now,
  .compile_write_enabled = bk7258_ota_trial_compile_write,
  .runtime_write_enabled = bk7258_ota_trial_runtime_write,
  .active_slot = bk7258_ota_active_slot,
  .lock = bk7258_ota_trial_lock,
  .unlock = bk7258_ota_trial_unlock,
  .read = bk7258_ota_publish_read,
  .erase_sector = bk7258_ota_publish_erase,
  .write = bk7258_ota_publish_write
};

static int bk7258_ota_trial_transition(
  uint64_t expected_generation,
  enum bk7258_boot_ota_rotation_state_e expected_state,
  enum bk7258_boot_ota_rotation_state_e next_state,
  uint32_t timeout_ms)
{
  struct bk7258_boot_ota_rotation_control_result_s result;
  uint8_t *workspace;
  int ret;

  if (!__atomic_load_n(&g_bk7258_ota_trial_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  workspace = kmm_malloc(BK7258_BOOT_OTA_ROTATION_CONTROL_WORKSPACE_SIZE);
  if (workspace == NULL)
    {
      return -ENOMEM;
    }

  ret = bk7258_boot_ota_rotation_control_transition(
    expected_generation, expected_state, next_state,
    &g_bk7258_ota_trial_ops, timeout_ms, workspace,
    BK7258_BOOT_OTA_ROTATION_CONTROL_WORKSPACE_SIZE, &result);
  kmm_free(workspace);
  return ret;
}

static void bk7258_ota_clear_bank_info(
  struct bk7258_boot_ota_rotation_bank_s *info)
{
  info->state = BK7258_BOOT_OTA_ROTATION_ERASED;
  info->base_slot = BK7258_BOOT_OTA_SLOT_A;
  info->target_slot = BK7258_BOOT_OTA_SLOT_B;
  info->valid_records = 0;
  info->last_record_index = 0;
  info->sequence = 0;
  info->generation = 0;
  info->erased = false;
  info->trusted = false;
}

static int bk7258_ota_trial_read_selected(
  uint8_t banks[2u * BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  struct bk7258_boot_ota_rotation_view_s *view,
  bool *metadata_degraded, uint32_t timeout_ms)
{
  struct bk7258_boot_ota_rotation_bank_s info[2];
  uint32_t addresses[2] =
  {
    BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET,
    BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET
  };
  uint32_t index;
  int ret;

  if (banks == NULL || view == NULL || metadata_degraded == NULL ||
      timeout_ms == 0)
    {
      return -EINVAL;
    }

  *metadata_degraded = false;
  ret = bk7258_flash_guard_lock(BK7258_FLASH_GUARD_OTA_METADATA,
                                false, timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  for (index = 0; index < 2; index++)
    {
      ret = bk7258_ota_trial_read(
        NULL, addresses[index],
        banks + index * BK7258_BOOT_OTA_ROTATION_BANK_SIZE,
        BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
      if (ret < 0)
        {
          break;
        }
    }

  bk7258_flash_guard_unlock();
  if (ret < 0)
    {
      return ret;
    }

  for (index = 0; index < 2; index++)
    {
      bk7258_ota_clear_bank_info(&info[index]);
      ret = bk7258_boot_ota_rotation_inspect(
        banks + index * BK7258_BOOT_OTA_ROTATION_BANK_SIZE, &info[index]);
      if (ret < 0)
        {
          bk7258_ota_clear_bank_info(&info[index]);
          *metadata_degraded = true;
        }
    }

  return bk7258_boot_ota_rotation_select(info, view);
}

int bk7258_ota_trial_initialize(void)
{
  __atomic_store_n(&g_bk7258_ota_trial_runtime_write, false,
                   __ATOMIC_RELEASE);

  /* Keep confirm, rollback and the portable transition closure in the final
   * CP ELF even though CONFIG_BK7258_OTA_TRIAL_WRITE is absent and there is no
   * runtime enable command in the closure build.
   */

  g_bk7258_ota_trial_link_closure =
    (uintptr_t)bk7258_ota_trial_confirm ^
    (uintptr_t)bk7258_ota_trial_rollback ^
    (uintptr_t)bk7258_ota_trial_get_status ^
    (uintptr_t)bk7258_ota_publish_pending ^
    (uintptr_t)bk7258_ota_trial_write_enabled ^
    (uintptr_t)bk7258_boot_ota_rotation_control_transition ^
    (uintptr_t)bk7258_boot_ota_rotation_publish_pending ^
    (uintptr_t)bk7258_boot_ota_rotation_health_update;
  __atomic_store_n(&g_bk7258_ota_trial_initialized, true,
                   __ATOMIC_RELEASE);
  return 0;
}

int bk7258_ota_trial_confirm(uint64_t expected_generation,
                             uint32_t timeout_ms)
{
  struct bk7258_boot_ota_rotation_health_tracker_s tracker = {0};
  struct bk7258_boot_ota_rotation_health_sample_s sample;
  struct bk7258_boot_ota_rotation_health_result_s health;
  struct bk7258_boot_ota_rotation_view_s view;
  struct bk7258_ap_supervisor_status_s supervisor;
  uint8_t *banks;
  const uint8_t *selected;
  uint64_t started;
  uint64_t now;
  uint32_t remaining;
  bool metadata_degraded;
  enum bk7258_boot_ota_rotation_state_e confirmed_state;
  int ret;

  if (expected_generation == 0 || timeout_ms == 0)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&g_bk7258_ota_trial_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  banks = kmm_malloc(2u * BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  if (banks == NULL)
    {
      return -ENOMEM;
    }

  started = bk7258_ota_publish_now(NULL);
  for (;;)
    {
      now = bk7258_ota_publish_now(NULL);
      if (now - started >= timeout_ms)
        {
          ret = -ETIMEDOUT;
          break;
        }

      remaining = timeout_ms - (uint32_t)(now - started);
      ret = bk7258_ota_trial_read_selected(
        banks, &view, &metadata_degraded, remaining);
      if (ret < 0 || !view.metadata_present ||
          view.selected_bank == BK7258_BOOT_OTA_ROTATION_NO_BANK)
        {
          if (ret == 0)
            {
              ret = -ENOENT;
            }

          break;
        }

      selected = banks +
        view.selected_bank * BK7258_BOOT_OTA_ROTATION_BANK_SIZE;
      ret = bk7258_ap_supervisor_get_status(&supervisor);
      if (ret < 0)
        {
          break;
        }

      sample.now_ms = now;
      sample.supervisor_generation = supervisor.generation;
      sample.supervisor_fault_count = supervisor.fault_count;
      sample.active_slot = bk7258_ota_active_slot(NULL);
      sample.supervisor_healthy =
        supervisor.state == BK7258_AP_SUPERVISOR_HEALTHY;
      sample.supervisor_fault_free =
        supervisor.reason == BK7258_AP_SUPERVISOR_REASON_NONE &&
        supervisor.last_error == 0 &&
        supervisor.injection == BK7258_AP_SUPERVISOR_INJECT_NONE &&
        (supervisor.flags &
         (BK7258_AP_SUPERVISOR_FLAG_PRIMARY |
          BK7258_AP_SUPERVISOR_FLAG_SECONDARY |
          BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK)) ==
         (BK7258_AP_SUPERVISOR_FLAG_PRIMARY |
          BK7258_AP_SUPERVISOR_FLAG_SECONDARY |
          BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK);
      ret = bk7258_boot_ota_rotation_health_update(
        &tracker, selected, expected_generation,
        CONFIG_BK7258_OTA_HEALTH_STABLE_MS, &sample, &health);
      if (ret == 0)
        {
          break;
        }

      if (ret != -EAGAIN)
        {
          break;
        }

      if (nxsig_usleep(CONFIG_BK7258_OTA_HEALTH_POLL_MS * 1000u) < 0 &&
          errno != EINTR)
        {
          ret = -errno;
          break;
        }
    }

  kmm_free(banks);
  if (ret < 0)
    {
      return ret;
    }

  now = bk7258_ota_publish_now(NULL);
  if (now - started >= timeout_ms)
    {
      return -ETIMEDOUT;
    }

  remaining = timeout_ms - (uint32_t)(now - started);
  confirmed_state = health.target_slot == BK7258_BOOT_OTA_SLOT_A ?
    BK7258_BOOT_OTA_ROTATION_CONFIRMED_A :
    BK7258_BOOT_OTA_ROTATION_CONFIRMED_B;
  return bk7258_ota_trial_transition(expected_generation, health.state,
                                     confirmed_state, remaining);
}

int bk7258_ota_trial_rollback(uint64_t expected_generation,
                              uint32_t timeout_ms)
{
  struct bk7258_boot_ota_rotation_view_s view;
  enum bk7258_boot_ota_rotation_state_e rollback_state;
  uint8_t *banks;
  uint64_t started;
  uint64_t elapsed;
  bool metadata_degraded;
  int ret;

  if (expected_generation == 0 || timeout_ms == 0)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&g_bk7258_ota_trial_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  banks = kmm_malloc(2u * BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  if (banks == NULL)
    {
      return -ENOMEM;
    }

  started = bk7258_ota_publish_now(NULL);
  ret = bk7258_ota_trial_read_selected(
    banks, &view, &metadata_degraded, timeout_ms);
  kmm_free(banks);
  if (ret < 0 || !view.metadata_present ||
      view.generation != expected_generation)
    {
      return ret < 0 ? ret :
             (!view.metadata_present ? -ENOENT : -ESTALE);
    }

  if (view.state == BK7258_BOOT_OTA_ROTATION_TRIAL_A)
    {
      rollback_state = BK7258_BOOT_OTA_ROTATION_ROLLBACK_B;
    }
  else if (view.state == BK7258_BOOT_OTA_ROTATION_TRIAL_B)
    {
      rollback_state = BK7258_BOOT_OTA_ROTATION_ROLLBACK_A;
    }
  else
    {
      return -EPERM;
    }

  elapsed = bk7258_ota_publish_now(NULL) - started;
  if (elapsed >= timeout_ms)
    {
      return -ETIMEDOUT;
    }

  return bk7258_ota_trial_transition(
    expected_generation, view.state, rollback_state,
    timeout_ms - (uint32_t)elapsed);
}

int bk7258_ota_trial_get_status(struct bk7258_ota_trial_status_s *status,
                                uint32_t timeout_ms)
{
  struct bk7258_boot_ota_rotation_view_s view;
  uint8_t *banks;
  bool metadata_degraded;
  int ret;

  if (status == NULL)
    {
      return -EINVAL;
    }

  status->status = -EINPROGRESS;
  status->format = 2;
  status->state = BK7258_BOOT_OTA_ROTATION_ERASED;
  status->valid_records = 0;
  status->selected_bank = BK7258_BOOT_OTA_ROTATION_NO_BANK;
  status->base_slot = BK7258_BOOT_OTA_SLOT_A;
  status->target_slot = BK7258_BOOT_OTA_SLOT_B;
  status->stable_slot = BK7258_BOOT_OTA_SLOT_A;
  status->active_slot = (uint32_t)bk7258_ota_active_slot(NULL);
  status->sequence = 0;
  status->generation = 0;
  status->metadata_valid = false;
  status->metadata_trusted = false;
  status->metadata_erased = false;
  status->metadata_degraded = false;
  status->secondary_mapping_active = false;
  status->staging_write_enabled = false;
  status->metadata_write_enabled = false;

  if (!__atomic_load_n(&g_bk7258_ota_trial_initialized,
                       __ATOMIC_ACQUIRE))
    {
      status->status = -EAGAIN;
      return -EAGAIN;
    }

  status->secondary_mapping_active =
    status->active_slot == BK7258_BOOT_OTA_SLOT_B;
  status->staging_write_enabled = bk7258_ota_staging_write_enabled();
  status->metadata_write_enabled = bk7258_ota_trial_write_enabled();

  banks = kmm_malloc(2u * BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  if (banks == NULL)
    {
      status->status = -ENOMEM;
      return -ENOMEM;
    }

  ret = bk7258_ota_trial_read_selected(
    banks, &view, &metadata_degraded, timeout_ms);
  kmm_free(banks);
  status->status = ret;
  if (ret == 0)
    {
      status->state = (uint32_t)view.state;
      status->valid_records = view.valid_records;
      status->selected_bank = view.selected_bank;
      status->base_slot = view.metadata_present ?
        (view.target_slot == BK7258_BOOT_OTA_SLOT_A ?
          BK7258_BOOT_OTA_SLOT_B : BK7258_BOOT_OTA_SLOT_A) :
        BK7258_BOOT_OTA_SLOT_A;
      status->target_slot = (uint32_t)view.target_slot;
      status->stable_slot = (uint32_t)view.stable_slot;
      status->sequence = view.sequence;
      status->generation = view.generation;
      status->metadata_valid = true;
      status->metadata_trusted = view.metadata_present;
      status->metadata_erased = !view.metadata_present;
      status->metadata_degraded = metadata_degraded;
    }

  return ret;
}

int bk7258_ota_publish_pending(
  const uint8_t pending_record[BK7258_OTA_PENDING_RECORD_SIZE],
  uint64_t expected_generation, uint32_t timeout_ms,
  struct bk7258_ota_publish_result_s *result)
{
  struct bk7258_boot_ota_rotation_publish_result_s core_result;
  uint8_t *workspace;
  int ret;

  if (pending_record == NULL || result == NULL)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&g_bk7258_ota_trial_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  workspace = kmm_malloc(BK7258_BOOT_OTA_ROTATION_PUBLISH_WORKSPACE_SIZE);
  if (workspace == NULL)
    {
      return -ENOMEM;
    }

  ret = bk7258_boot_ota_rotation_publish_pending(
    pending_record, expected_generation, &g_bk7258_ota_publish_raw_ops,
    &g_bk7258_ota_publish_hash_ops, &g_bk7258_ota_publish_ops, timeout_ms,
    workspace, BK7258_BOOT_OTA_ROTATION_PUBLISH_WORKSPACE_SIZE,
    &core_result);
  kmm_free(workspace);

  result->status = core_result.status;
  result->phase = (uint32_t)core_result.phase;
  result->previous_state = (uint32_t)core_result.previous_state;
  result->previous_records = core_result.previous_records;
  result->previous_bank = core_result.previous_bank;
  result->published_bank = core_result.published_bank;
  result->stable_slot = (uint32_t)core_result.stable_slot;
  result->target_slot = (uint32_t)core_result.target_slot;
  result->programmed_chunks = core_result.programmed_chunks;
  result->verified_chunks = core_result.verified_chunks;
  result->previous_generation = core_result.previous_generation;
  result->generation = core_result.generation;
  result->candidate_verified = core_result.candidate_verified;
  result->base_verified = core_result.base_verified;
  result->metadata_degraded = core_result.metadata_degraded;
  result->mutation_attempted = core_result.mutation_attempted;
  result->sector_reclaimed = core_result.bank_reclaimed;
  result->erase_verified = core_result.erase_verified;
  result->readback_verified = core_result.readback_verified;
  result->idempotent = core_result.idempotent;
  return ret;
}

bool bk7258_ota_trial_write_enabled(void)
{
  return bk7258_ota_trial_compile_write(NULL) &&
         bk7258_ota_trial_runtime_write(NULL);
}

#ifdef CONFIG_BK7258_OTA_TRIAL_WRITE
int bk7258_ota_trial_set_write_enabled(bool enabled)
{
  if (!__atomic_load_n(&g_bk7258_ota_trial_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  __atomic_store_n(&g_bk7258_ota_trial_runtime_write, enabled,
                   __ATOMIC_RELEASE);
  return 0;
}
#endif
