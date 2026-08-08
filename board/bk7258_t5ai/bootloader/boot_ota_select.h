/*
 * boot_ota_select.h - target adapter for the N15-C boot selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_SELECT_H
#define BK7258_BOOT_OTA_SELECT_H

#include <stdint.h>
#include <stddef.h>

#include "boot_ota_rotation_core.h"

#define BK7258_BOOT_OTA_POLICY_SLOT_NONE 0xffu

enum bk7258_boot_ota_policy_source_e
{
  BK7258_BOOT_OTA_POLICY_FIXED = 0,
  BK7258_BOOT_OTA_POLICY_N15 = 1,
  BK7258_BOOT_OTA_POLICY_N17 = 2
};

struct bk7258_boot_ota_policy_s
{
  uint8_t preferred_slot;
  uint8_t fallback_slot;
  uint8_t source;
  uint8_t state;
  uint64_t generation;
};

uint32_t boot_ota_select_app(uint32_t primary_app_vector);

/* Resolve lifecycle state without leaving the application XIP remapper
 * enabled.  BL2 consumes this order and performs the authoritative MCUboot
 * validation before either CP or AP is released. */
int boot_ota_resolve_policy(struct bk7258_boot_ota_policy_s *policy);

/* Read physical Flash bytes through the same controller path used by the
 * board-owned OTA metadata reader.  This is read-only; it does not enable
 * remap and never erases or programs Flash. */
int boot_ota_raw_read(void *arg, uint32_t address, uint8_t *buffer,
                      size_t len);

#endif /* BK7258_BOOT_OTA_SELECT_H */
