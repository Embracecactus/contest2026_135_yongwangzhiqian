/****************************************************************************
 * chips/bk7258/include/bk7258_ota.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_H

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/chip/bk7258_boot_slot.h>
#include <arch/chip/bk7258_flash.h>
#include <arch/chip/bk7258_mcuboot_format.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_OTA_MANIFEST_VERSION 2u
#define BK7258_OTA_SHA256_SIZE      32u
#define BK7258_OTA_PACKAGE_ID_SIZE  32u
#define BK7258_OTA_CRC_DATA_SIZE    32u
#define BK7258_OTA_CRC_TOTAL_SIZE   34u
#define BK7258_OTA_ERASE_SIZE       BK7258_FLASH_SECTOR_SIZE

enum bk7258_ota_image_e
{
  BK7258_OTA_IMAGE_CP = 0,
  BK7258_OTA_IMAGE_AP = 1
};

struct bk7258_ota_geometry_s
{
  enum bk7258_boot_slot_e active_slot;
  enum bk7258_boot_slot_e inactive_slot;
  uint32_t cp_raw_offset;
  uint32_t cp_raw_size;
  uint32_t ap_raw_offset;
  uint32_t ap_raw_size;
};

struct bk7258_ota_image_manifest_s
{
  uint32_t physical_size;
  uint8_t sha256[BK7258_OTA_SHA256_SIZE];
};

struct bk7258_ota_manifest_s
{
  uint32_t version;
  uint8_t layout_sha256[BK7258_OTA_SHA256_SIZE];
  struct bk7258_mcuboot_version_s image_version;
  uint32_t security_counter;
  uint8_t package_id[BK7258_OTA_PACKAGE_ID_SIZE];
  struct bk7258_ota_image_manifest_s image[2];
};

enum bk7258_ota_pair_state_e
{
  BK7258_OTA_PAIR_INVALID = 0,
  BK7258_OTA_PAIR_PENDING,
  BK7258_OTA_PAIR_CONFIRMED
};

struct bk7258_ota_pair_snapshot_s
{
  enum bk7258_boot_slot_e active_slot;
  enum bk7258_ota_pair_state_e state;
  struct bk7258_mcuboot_version_s version;
  uint32_t security_counter;
  bool security_counter_present;
};

enum bk7258_ota_phase_e
{
  BK7258_OTA_PHASE_PREPARE = 0,
  BK7258_OTA_PHASE_ERASE_CP,
  BK7258_OTA_PHASE_ERASE_AP,
  BK7258_OTA_PHASE_WRITE_AP,
  BK7258_OTA_PHASE_WRITE_CP,
  BK7258_OTA_PHASE_COMMIT_CP,
  BK7258_OTA_PHASE_COMPLETE
};

struct bk7258_ota_progress_s
{
  enum bk7258_ota_phase_e phase;
  enum bk7258_ota_image_e image;
  uint32_t completed;
  uint32_t total;
};

/* The source owns transport and package ingestion.  open must return
 * metadata from an already authenticated package policy; the CP engine
 * validates the selected layout, package identity, candidate CP/AP header
 * generation,
 * protected counters, exact physical sizes and both SHA-256 values before
 * committing CP sector zero.  read_at returns exactly nbytes of one finalized
 * physical (32 data + 2 CRC) image and must not write on-chip Flash.
 * checkpoint is optional and may return a negative errno to cancel before the
 * final commit.  BL2 remains the final MCUboot signature/counter gate. */

struct bk7258_ota_source_ops_s
{
  int (*open)(void *context, struct bk7258_ota_manifest_s *manifest);
  int (*read_at)(void *context, enum bk7258_ota_image_e image,
                 uint32_t offset, uint8_t *buffer, size_t nbytes);
  int (*checkpoint)(void *context,
                    const struct bk7258_ota_progress_s *progress);
  int (*cancel)(void *context);
  void (*close)(void *context);
};

#ifdef CONFIG_BK7258_OTA
int bk7258_ota_inactive_geometry(struct bk7258_ota_geometry_s *geometry);
int bk7258_ota_get_active_pair(struct bk7258_ota_pair_snapshot_s *snapshot);
int bk7258_ota_stage_pair(const struct bk7258_ota_source_ops_s *ops,
                          void *context);
/* Call only after the product's external CP/AP health policy has accepted the
 * running pair.  The expected snapshot binds the decision to one pending
 * active slot/version/counter; this primitive validates pair state again
 * under the Flash guard, not service health. */
int bk7258_ota_confirm_pair(
  const struct bk7258_ota_pair_snapshot_s *expected);
void bk7258_ota_system_reset(void) noreturn_function;
struct bk7258_ap_supervisor_health_token_s;
/* Confirm with generation-bound Supervisor evidence.  The caller owns the
 * product freshness policy and supplies its maximum accepted sample age;
 * chip code revalidates that evidence before each trailer mutation.
 */
int bk7258_ota_confirm_pair_health(
  const struct bk7258_ota_pair_snapshot_s *expected,
  const struct bk7258_ap_supervisor_health_token_s *health,
  uint32_t health_max_age_ms);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_H */
