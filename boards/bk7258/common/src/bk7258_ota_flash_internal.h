/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __BOARDS_ARM_BK7258_SRC_BK7258_OTA_FLASH_INTERNAL_H
#define __BOARDS_ARM_BK7258_SRC_BK7258_OTA_FLASH_INTERNAL_H

#include <stdint.h>

int bk7258_ota_flash_initialize(void);
int bk7258_ota_flash_verify(uint32_t address, const uint8_t *expected,
                            uint32_t nbytes);
uint16_t bk7258_ota_flash_crc16(const uint8_t *data);

#endif
