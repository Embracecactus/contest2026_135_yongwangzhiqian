/*
 * boot_n17_select.h - target adapter for the N17 read-only boot selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_N17_SELECT_H
#define BK7258_BOOT_N17_SELECT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "boot_ota_select_core.h"

/* Return zero when N17 is absent and the caller may continue with N15,
 * one when a verified N17 decision was produced, and a negative error when
 * an armed or present N17 state must fail closed.
 */

int bk7258_boot_n17_select(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  uint8_t bank0[4096], uint8_t bank1[4096],
  uint8_t *scratch, size_t scratch_size, bool enabled, uint8_t *slot);

/* Board-specific crypto integration point.  The weak implementation rejects
 * all signatures.  A future audited wrapper around the pinned SDK/ROM
 * security service must override this function before any N17 policy sector
 * is armed.
 */

int bk7258_boot_n17_verify_signature(
  uint32_t key_id, const uint8_t signed_data[448],
  const uint8_t signature[64]);

#endif /* BK7258_BOOT_N17_SELECT_H */
