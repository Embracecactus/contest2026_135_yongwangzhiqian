/*
 * boot_ota_flash_program.h - SRAM-only BK7258 metadata program primitive.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_FLASH_PROGRAM_H
#define BK7258_BOOT_OTA_FLASH_PROGRAM_H

#include <stdint.h>

int boot_ota_flash_program32(uint32_t address, const uint8_t data[32]);

#endif /* BK7258_BOOT_OTA_FLASH_PROGRAM_H */
