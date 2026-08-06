/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/bk7258_ota_staging.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX/Beken adapter for the portable N15-B candidate staging core.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#include <crypto/sha2.h>
#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mtd/mtd.h>

#include <arch/chip/bk7258_ota_staging.h>

#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
#include <arch/chip/bk7258_ota_fault.h>
#endif

#include "bk7258_flash_mtd.h"
#include "bk7258_flash_guard.h"
#include "bk7258_ota_staging_core.h"

#define BK7258_FLASH_REMAP_ENABLE 0x44030064u
#define BK7258_REG32(address) \
  (*(volatile uint32_t *)(uintptr_t)(address))

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile bool g_bk7258_ota_staging_initialized;
static volatile bool g_bk7258_ota_staging_runtime_write;
static volatile bool g_bk7258_ota_staging_active;
static volatile uintptr_t g_bk7258_ota_staging_link_closure;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_ota_sha_init(void *context)
{
  sha256init((SHA2_CTX *)context);
}

static void bk7258_ota_sha_update(void *context, const uint8_t *data,
                                  size_t len)
{
  sha256update((SHA2_CTX *)context, data, len);
}

static void bk7258_ota_sha_final(
  void *context, uint8_t digest[BK7258_OTA_STAGE_SHA256_SIZE])
{
  sha256final(digest, (SHA2_CTX *)context);
}

static const struct bk7258_ota_hash_ops_s g_bk7258_ota_hash_ops =
{
  .context_size = sizeof(SHA2_CTX),
  .init = bk7258_ota_sha_init,
  .update = bk7258_ota_sha_update,
  .final = bk7258_ota_sha_final
};

static uint64_t bk7258_ota_now_ms(void *arg)
{
  (void)arg;
  return (uint64_t)TICK2MSEC(clock_systime_ticks());
}

static bool bk7258_ota_compile_write_enabled(void *arg)
{
  (void)arg;
#ifdef CONFIG_BK7258_OTA_STAGING_WRITE
  return true;
#else
  return false;
#endif
}

static bool bk7258_ota_runtime_write_enabled(void *arg)
{
  (void)arg;
  return __atomic_load_n(&g_bk7258_ota_staging_runtime_write,
                         __ATOMIC_ACQUIRE);
}

static int bk7258_ota_flash_lock(void *arg, uint32_t timeout_ms)
{
  (void)arg;
  return bk7258_flash_guard_lock(BK7258_FLASH_GUARD_OTA_STAGING,
                                 true, timeout_ms);
}

static void bk7258_ota_flash_unlock(void *arg)
{
  (void)arg;
  bk7258_flash_guard_unlock();
}

static int bk7258_ota_flash_erase(void *arg, uint32_t address)
{
  FAR struct mtd_dev_s *mtd;
  uint32_t offset;
  int ret;

  (void)arg;
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
  ret = bk7258_ota_fault_before(BK7258_OTA_FAULT_STAGE_ERASE);
  if (ret < 0)
    {
      return ret;
    }
#else
  (void)ret;
#endif
  mtd = address < BK7258_ROLE_SLOT_B_PAIR_OFFSET ?
    bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_A) :
    bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_B);
  offset = address < BK7258_ROLE_SLOT_B_PAIR_OFFSET ?
    address - BK7258_ROLE_SLOT_A_CP_OFFSET :
    address - BK7258_ROLE_SLOT_B_PAIR_OFFSET;

  if (mtd == NULL || offset >= BK7258_ROLE_SLOT_B_PAIR_SIZE ||
      offset % BK7258_FLASH_ERASE_SIZE != 0)
    {
      return -EINVAL;
    }

  return MTD_ERASE(mtd, offset / BK7258_FLASH_ERASE_SIZE, 1);
}

static int bk7258_ota_flash_write(void *arg, uint32_t address,
                                  const uint8_t *data, size_t len)
{
  FAR struct mtd_dev_s *mtd;
  uint32_t offset;
  ssize_t written;
  int ret;

  (void)arg;
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
  ret = bk7258_ota_fault_before(BK7258_OTA_FAULT_STAGE_WRITE);
  if (ret < 0)
    {
      return ret;
    }
#else
  (void)ret;
#endif
  mtd = address < BK7258_ROLE_SLOT_B_PAIR_OFFSET ?
    bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_A) :
    bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_B);
  offset = address < BK7258_ROLE_SLOT_B_PAIR_OFFSET ?
    address - BK7258_ROLE_SLOT_A_CP_OFFSET :
    address - BK7258_ROLE_SLOT_B_PAIR_OFFSET;

  if (mtd == NULL || offset > BK7258_ROLE_SLOT_B_PAIR_SIZE ||
      len > BK7258_ROLE_SLOT_B_PAIR_SIZE - offset)
    {
      return -EINVAL;
    }

  written = MTD_WRITE(mtd, offset, len, data);
  return written == (ssize_t)len ? 0 :
         written < 0 ? (int)written : -EIO;
}

static int bk7258_ota_flash_read(void *arg, uint32_t address, uint8_t *data,
                                 size_t len)
{
  FAR struct mtd_dev_s *mtd;
  uint32_t offset;
  ssize_t read;
  int ret;

  (void)arg;
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
  ret = bk7258_ota_fault_before(BK7258_OTA_FAULT_STAGE_READ);
  if (ret < 0)
    {
      return ret;
    }
#else
  (void)ret;
#endif
  mtd = address < BK7258_ROLE_SLOT_B_PAIR_OFFSET ?
    bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_A) :
    bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_B);
  offset = address < BK7258_ROLE_SLOT_B_PAIR_OFFSET ?
    address - BK7258_ROLE_SLOT_A_CP_OFFSET :
    address - BK7258_ROLE_SLOT_B_PAIR_OFFSET;

  if (mtd == NULL || offset > BK7258_ROLE_SLOT_B_PAIR_SIZE ||
      len > BK7258_ROLE_SLOT_B_PAIR_SIZE - offset)
    {
      return -EINVAL;
    }

  read = MTD_READ(mtd, offset, len, data);
  return read == (ssize_t)len ? 0 : read < 0 ? (int)read : -EIO;
}

static const struct bk7258_ota_flash_ops_s g_bk7258_ota_flash_ops =
{
  .arg = NULL,
  .now_ms = bk7258_ota_now_ms,
  .compile_write_enabled = bk7258_ota_compile_write_enabled,
  .runtime_write_enabled = bk7258_ota_runtime_write_enabled,
  .lock = bk7258_ota_flash_lock,
  .unlock = bk7258_ota_flash_unlock,
  .erase_sector = bk7258_ota_flash_erase,
  .write = bk7258_ota_flash_write,
  .read = bk7258_ota_flash_read
};

static uint32_t bk7258_ota_slot_start(enum bk7258_ota_slot_e slot)
{
  if (slot == BK7258_OTA_SLOT_A)
    {
      return BK7258_ROLE_SLOT_A_CP_OFFSET;
    }

  if (slot == BK7258_OTA_SLOT_B)
    {
      return BK7258_ROLE_SLOT_B_PAIR_OFFSET;
    }

  return UINT32_MAX;
}

static uint32_t bk7258_ota_active_start(void)
{
  return (BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) & 1u) != 0 ?
    BK7258_ROLE_SLOT_B_PAIR_OFFSET : BK7258_ROLE_SLOT_A_CP_OFFSET;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ota_staging_initialize(void)
{
  static_assert(sizeof(SHA2_CTX) <= BK7258_OTA_HASH_CONTEXT_MAX,
                "SHA2 context exceeds N15-B scratch contract");

  __atomic_store_n(&g_bk7258_ota_staging_runtime_write, false,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_ota_staging_active, false,
                   __ATOMIC_RELEASE);

  /* Keep the validate/stage entry points and their portable core in the CP
   * ELF even while no command calls them and both write gates are zero.  The
   * volatile diagnostic value has no authority meaning; it is only an ELF
   * closure anchor verified by the N15-B host gate.
   */

  g_bk7258_ota_staging_link_closure =
    (uintptr_t)bk7258_ota_staging_validate ^
    (uintptr_t)bk7258_ota_staging_stage ^
    (uintptr_t)bk7258_ota_staging_validate_slot ^
    (uintptr_t)bk7258_ota_staging_stage_inactive ^
    (uintptr_t)bk7258_ota_staging_write_enabled;
  __atomic_store_n(&g_bk7258_ota_staging_initialized, true,
                   __ATOMIC_RELEASE);
  return 0;
}

int bk7258_ota_staging_validate(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  struct bk7258_ota_stage_result_s *result)
{
  uint8_t *scratch;
  int ret;

  if (!__atomic_load_n(&g_bk7258_ota_staging_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  scratch = kmm_malloc(BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (scratch == NULL)
    {
      return -ENOMEM;
    }

  ret = bk7258_ota_core_validate(descriptor, expected, source,
                                 &g_bk7258_ota_hash_ops, scratch,
                                 BK7258_OTA_STAGE_SCRATCH_SIZE, result);
  kmm_free(scratch);
  return ret;
}

int bk7258_ota_staging_validate_slot(
  enum bk7258_ota_slot_e target_slot,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  struct bk7258_ota_stage_result_s *result)
{
  uint32_t target_start = bk7258_ota_slot_start(target_slot);
  uint8_t *scratch;
  int ret;

  if (target_start == UINT32_MAX)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&g_bk7258_ota_staging_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  scratch = kmm_malloc(BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (scratch == NULL)
    {
      return -ENOMEM;
    }

  ret = bk7258_ota_core_validate_at(
    target_start, descriptor, expected, source, &g_bk7258_ota_hash_ops,
    scratch, BK7258_OTA_STAGE_SCRATCH_SIZE, result);
  kmm_free(scratch);
  return ret;
}

static int bk7258_ota_staging_stage_at(
  uint32_t target_start, uint32_t active_start,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source, uint32_t timeout_ms,
  struct bk7258_ota_stage_result_s *result)
{
  uint8_t *scratch;
  bool inactive = false;
  int ret;

  if (!__atomic_load_n(&g_bk7258_ota_staging_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  if (!__atomic_compare_exchange_n(&g_bk7258_ota_staging_active,
                                   &inactive, true, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  scratch = kmm_malloc(BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (scratch == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  ret = bk7258_ota_core_stage_inactive(
    target_start, active_start, descriptor, expected, source,
    &g_bk7258_ota_hash_ops, &g_bk7258_ota_flash_ops, timeout_ms, scratch,
    BK7258_OTA_STAGE_SCRATCH_SIZE, result);
  kmm_free(scratch);

out:
  __atomic_store_n(&g_bk7258_ota_staging_active, false,
                   __ATOMIC_RELEASE);
  return ret;
}

int bk7258_ota_staging_stage(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source, uint32_t timeout_ms,
  struct bk7258_ota_stage_result_s *result)
{
  return bk7258_ota_staging_stage_at(
    BK7258_ROLE_SLOT_B_PAIR_OFFSET, bk7258_ota_active_start(), descriptor,
    expected, source, timeout_ms, result);
}

int bk7258_ota_staging_stage_inactive(
  enum bk7258_ota_slot_e target_slot,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source, uint32_t timeout_ms,
  struct bk7258_ota_stage_result_s *result)
{
  uint32_t target_start = bk7258_ota_slot_start(target_slot);
  uint32_t active_start = bk7258_ota_active_start();

  if (target_start == UINT32_MAX)
    {
      return -EINVAL;
    }

  if (target_start == active_start)
    {
      return -EPERM;
    }

  return bk7258_ota_staging_stage_at(
    target_start, active_start, descriptor, expected, source, timeout_ms,
    result);
}

bool bk7258_ota_staging_write_enabled(void)
{
  return bk7258_ota_compile_write_enabled(NULL) &&
         bk7258_ota_runtime_write_enabled(NULL);
}

#ifdef CONFIG_BK7258_OTA_STAGING_WRITE
int bk7258_ota_staging_set_write_enabled(bool enabled)
{
  if (!__atomic_load_n(&g_bk7258_ota_staging_initialized,
                       __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  __atomic_store_n(&g_bk7258_ota_staging_runtime_write, enabled,
                   __ATOMIC_RELEASE);
  return 0;
}
#endif
