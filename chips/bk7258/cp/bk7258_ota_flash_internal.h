/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __CHIPS_BK7258_CP_BK7258_OTA_FLASH_INTERNAL_H
#define __CHIPS_BK7258_CP_BK7258_OTA_FLASH_INTERNAL_H

#include <stdint.h>

#include <arch/chip/bk7258_ota.h>

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_ota_flash_initialize(void);
int bk7258_ota_flash_verify(uint32_t address,
                            FAR const uint8_t *expected,
                            uint32_t nbytes);
uint16_t bk7258_ota_flash_crc16(FAR const uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_CP_BK7258_OTA_FLASH_INTERNAL_H */
