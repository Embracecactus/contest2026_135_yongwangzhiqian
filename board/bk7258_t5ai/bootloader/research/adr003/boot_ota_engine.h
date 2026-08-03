/* boot_ota_engine.h - disabled-by-default BK7258 SRAM Flash primitive ABI. */

#ifndef __BOOTLOADER_BOOT_OTA_ENGINE_H
#define __BOOTLOADER_BOOT_OTA_ENGINE_H

#include <stdint.h>

#define BK7258_OTA_ENGINE_REQUEST_MAGIC   0x31524542u /* "BER1" */
#define BK7258_OTA_ENGINE_REQUEST_VERSION 1u
#define BK7258_OTA_ENGINE_ENABLE_MAGIC    0x3141544fu /* "OTA1" */

/* N15-R2 hard gate.  ADR-003 acceptance alone is not sufficient to change
 * this value: N15-C/D must also supply the parser/recovery call path. */

#define BK7258_OTA_ENGINE_WRITE_GATE      0u

enum bk7258_ota_engine_command_e {
    BK7258_OTA_ENGINE_PROBE = 0,
    BK7258_OTA_ENGINE_ERASE_JOURNAL_SECTOR = 1,
    BK7258_OTA_ENGINE_PROGRAM_JOURNAL_CHUNK = 2,
    BK7258_OTA_ENGINE_COPY_SECTOR = 3,
    BK7258_OTA_ENGINE_RESTORE_PROTECTION = 4,
};

enum bk7258_ota_engine_result_e {
    BK7258_OTA_ENGINE_OK = 0,
    BK7258_OTA_ENGINE_ERR_REQUEST = -1,
    BK7258_OTA_ENGINE_ERR_IRQ = -2,
    BK7258_OTA_ENGINE_ERR_CACHE = -3,
    BK7258_OTA_ENGINE_ERR_AP_STATE = -4,
    BK7258_OTA_ENGINE_ERR_FLASH_ID = -5,
    BK7258_OTA_ENGINE_ERR_DISABLED = -6,
    BK7258_OTA_ENGINE_ERR_RANGE = -7,
    BK7258_OTA_ENGINE_ERR_VERIFY = -8,
};

struct bk7258_ota_engine_request_v1 {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t command;
    uint32_t source;
    uint32_t destination;
    uint32_t status_before;
    int32_t result;
    uint32_t observed_flash_id;
    uint32_t observed_status;
    uint32_t detail;
    uint8_t chunk[32];
};

_Static_assert(sizeof(struct bk7258_ota_engine_request_v1) == 72u,
               "N15 SRAM engine request ABI drift");

/* This is the sole externally visible entry in the SRAM closure. */

int boot_ota_sram_entry(struct bk7258_ota_engine_request_v1 *request);

#endif /* __BOOTLOADER_BOOT_OTA_ENGINE_H */
