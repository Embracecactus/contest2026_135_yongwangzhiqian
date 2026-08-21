/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/include/bk7258_ota.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARDS_ARM_BK7258_INCLUDE_BK7258_OTA_H
#define __BOARDS_ARM_BK7258_INCLUDE_BK7258_OTA_H

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/board/bk7258_boot_slot.h>

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

/* Read exactly nbytes of one finalized physical (32 data + 2 CRC) image.
 * Return zero on success.  The board calls this synchronously and owns all
 * erase/write ordering; the callback must not write on-chip Flash.  The
 * caller owns authenticated-package, layout/generation and transport policy.
 * BL2 remains the final MCUboot signature gate before execution. */

typedef int (*bk7258_ota_read_t)(void *arg, enum bk7258_ota_image_e image,
                                uint32_t offset, uint8_t *buffer,
                                size_t nbytes);

#ifdef CONFIG_BK7258_OTA
int bk7258_ota_inactive_geometry(struct bk7258_ota_geometry_s *geometry);
int bk7258_ota_stage_pair(bk7258_ota_read_t reader, void *arg);
/* Call only after the product's external CP/AP health policy has accepted the
 * running pair.  This primitive validates trailer state, not service health. */
int bk7258_ota_confirm_pair(void);
#endif

#endif /* __BOARDS_ARM_BK7258_INCLUDE_BK7258_OTA_H */
