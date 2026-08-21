/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_BOOT_FLASH_H
#define BK7258_BOOT_FLASH_H

#include <stddef.h>
#include <stdint.h>

/* Access raw on-chip Flash through the BK7258 software-command window.  BL1
 * uses the reader for Manifest records.  BL2 uses program/erase only behind
 * its MCUboot flash-area range and CRC-translation guards. */

int bk7258_boot_flash_read(uint32_t address, uint8_t *buffer, size_t len);
int bk7258_boot_flash_restore_default_protection(void);
int bk7258_boot_flash_program(uint32_t address, const uint8_t *buffer,
                              size_t len);
int bk7258_boot_flash_erase(uint32_t address, size_t len);

#endif /* BK7258_BOOT_FLASH_H */
