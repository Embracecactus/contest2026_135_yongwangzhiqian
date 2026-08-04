/*
 * boot_ota_select.h - target adapter for the N15-C boot selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_SELECT_H
#define BK7258_BOOT_OTA_SELECT_H

#include <stdint.h>

uint32_t boot_ota_select_app(uint32_t primary_app_vector);

#endif /* BK7258_BOOT_OTA_SELECT_H */
