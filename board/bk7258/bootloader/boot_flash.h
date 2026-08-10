/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_BOOT_FLASH_H
#define BK7258_BOOT_FLASH_H

#include <stddef.h>
#include <stdint.h>

/* Read raw on-chip Flash through the BK7258 software-command window.  This
 * helper is read-only and exists solely for BL1 control/Manifest records
 * that are not visible through the CRC-decoded executable XIP view. */
int bk7258_bl1_flash_read(uint32_t address, uint8_t *buffer, size_t len);

#endif /* BK7258_BOOT_FLASH_H */
