/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_ota_staging_core.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable N15-B validation/staging core used by the CP adapter and host
 * fault-injection harness.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_OTA_STAGING_CORE_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_OTA_STAGING_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../include/bk7258_ota_staging.h"

#define BK7258_OTA_HASH_CONTEXT_MAX 256u

struct bk7258_ota_hash_ops_s
{
  size_t context_size;
  void (*init)(void *context);
  void (*update)(void *context, const uint8_t *data, size_t len);
  void (*final)(void *context,
                uint8_t digest[BK7258_OTA_STAGE_SHA256_SIZE]);
};

struct bk7258_ota_flash_ops_s
{
  void *arg;
  uint64_t (*now_ms)(void *arg);
  bool (*compile_write_enabled)(void *arg);
  bool (*runtime_write_enabled)(void *arg);
  int (*lock)(void *arg, uint32_t timeout_ms);
  void (*unlock)(void *arg);
  int (*erase_sector)(void *arg, uint32_t address);
  int (*write)(void *arg, uint32_t address, const uint8_t *data, size_t len);
  int (*read)(void *arg, uint32_t address, uint8_t *data, size_t len);
};

int bk7258_ota_core_validate(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result);

/* Read-only slot-neutral validation used by the symmetric boot selector.
 * physical_start must be the exact start of slot A or slot B and must match
 * the descriptor.  The legacy/public staging entry remains pinned to B.
 */

int bk7258_ota_core_validate_at(
  uint32_t physical_start,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result);

int bk7258_ota_core_stage(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_ota_flash_ops_s *flash_ops,
  uint32_t timeout_ms, uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result);

int bk7258_ota_core_stage_inactive(
  uint32_t physical_start, uint32_t active_physical_start,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_ota_flash_ops_s *flash_ops,
  uint32_t timeout_ms, uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result);

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_OTA_STAGING_CORE_H */
