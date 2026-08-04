/****************************************************************************
 * arch/arm/include/bk7258/bk7258_ota_staging.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-only N15-B candidate validation and s_app staging wrapper.
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_STAGING_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_STAGING_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bk7258_partition_layout.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_OTA_STAGE_DESCRIPTOR_SIZE 384u
#define BK7258_OTA_STAGE_VERSION_SIZE    24u
#define BK7258_OTA_STAGE_SHA256_SIZE     32u
#define BK7258_OTA_STAGE_SCRATCH_SIZE    8192u

/* N15-F validation transport ABI.  The 16 MiB board PSRAM has a frozen
 * low-half allocator ABI ending at 0x60800000.  Validation artifacts use
 * fixed, non-overlapping locations in the otherwise unallocated upper half;
 * callers cannot supply alternate addresses.  This is a temporary transport
 * window, not a production allocator or persistent storage contract.
 */

#define BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS  0x60800000u
#define BK7258_OTA_TRANSFER_CANDIDATE_SIZE     \
  BK7258_ROLE_SLOT_B_PAIR_SIZE
#define BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS \
  (BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS + \
   BK7258_OTA_TRANSFER_CANDIDATE_SIZE)
#define BK7258_OTA_TRANSFER_RECORD_ADDRESS     \
  (BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS + BK7258_FLASH_ERASE_SIZE)
#define BK7258_OTA_TRANSFER_RECORD_SIZE        512u
#define BK7258_OTA_TRANSFER_END                \
  (BK7258_OTA_TRANSFER_RECORD_ADDRESS + BK7258_OTA_TRANSFER_RECORD_SIZE)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* The source is immutable for one call and must support bounded random reads.
 * Returning zero means exactly len bytes were copied; a negative errno fails
 * the operation.  A positive return is rejected as a short/ambiguous read.
 * The source must not depend on the same raw Flash guard while a sector
 * transaction is active.
 */

typedef int (*bk7258_ota_source_read_t)(void *arg, uint32_t offset,
                                        uint8_t *buffer, size_t len);

struct bk7258_ota_source_s
{
  bk7258_ota_source_read_t read;
  void *arg;
  uint32_t size;
};

/* All four identity fields are caller-pinned and mandatory.  Strings must be
 * canonical NUL-terminated ASCII and fit the N15-A 24-byte RBL fields.
 */

struct bk7258_ota_expected_s
{
  uint64_t generation;
  uint32_t timestamp;
  char version[BK7258_OTA_STAGE_VERSION_SIZE];
  char base_version[BK7258_OTA_STAGE_VERSION_SIZE];
};

enum bk7258_ota_slot_e
{
  BK7258_OTA_SLOT_A = 0,
  BK7258_OTA_SLOT_B = 1
};

enum bk7258_ota_stage_phase_e
{
  BK7258_OTA_STAGE_IDLE = 0,
  BK7258_OTA_STAGE_PREFLIGHT,
  BK7258_OTA_STAGE_ERASE,
  BK7258_OTA_STAGE_PROGRAM,
  BK7258_OTA_STAGE_READBACK,
  BK7258_OTA_STAGE_FINAL_DIGEST,
  BK7258_OTA_STAGE_DONE
};

struct bk7258_ota_stage_result_s
{
  int status;
  enum bk7258_ota_stage_phase_e phase;
  uint64_t generation;
  uint32_t sectors_erased;
  uint32_t bytes_programmed;
  uint32_t bytes_readback;
  uint8_t slot_sha256[BK7258_OTA_STAGE_SHA256_SIZE];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_OTA_STAGING
int bk7258_ota_staging_initialize(void);

int bk7258_ota_staging_validate(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  struct bk7258_ota_stage_result_s *result);

int bk7258_ota_staging_stage(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source, uint32_t timeout_ms,
  struct bk7258_ota_stage_result_s *result);

int bk7258_ota_staging_validate_slot(
  enum bk7258_ota_slot_e target_slot,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  struct bk7258_ota_stage_result_s *result);

int bk7258_ota_staging_stage_inactive(
  enum bk7258_ota_slot_e target_slot,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source, uint32_t timeout_ms,
  struct bk7258_ota_stage_result_s *result);

bool bk7258_ota_staging_write_enabled(void);

#ifdef CONFIG_BK7258_OTA_STAGING_WRITE
int bk7258_ota_staging_set_write_enabled(bool enabled);
#endif
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_STAGING_H */
