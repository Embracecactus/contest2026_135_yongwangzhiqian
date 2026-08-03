/* boot_ota_loader.h - install and call the BK7258 SRAM Flash closure. */

#ifndef __BOOTLOADER_BOOT_OTA_LOADER_H
#define __BOOTLOADER_BOOT_OTA_LOADER_H

#include "boot_ota_engine.h"

int boot_ota_engine_install(void);
int boot_ota_engine_call(struct bk7258_ota_engine_request_v1 *request);

#endif /* __BOOTLOADER_BOOT_OTA_LOADER_H */
