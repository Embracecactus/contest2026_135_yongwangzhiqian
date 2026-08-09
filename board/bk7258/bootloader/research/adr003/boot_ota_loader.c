/*
 * boot_ota_loader.c - XIP-side loader for the N15 SRAM Flash closure.
 *
 * These functions are retained for source/static verification in N15-R2 but
 * are not called by the Tier-1 boot path.  A future accepted transaction
 * parser must explicitly call boot_ota_engine_call().
 */

#include <stdint.h>

#include "boot_ota_loader.h"

#define OTA_LOADER_TEXT \
    __attribute__((section(".text.boot_ota_loader"), noinline, used))

#define OTA_SRAM_EXPECTED_START 0x28000000u
#define OTA_SRAM_LIMIT          0x28002000u
#define BOOT_FLASH_START        0x02000000u
#define BOOT_FLASH_END          0x02010000u

extern uint8_t __ota_sram_load_start[];
extern uint8_t __ota_sram_start[];
extern uint8_t __ota_sram_end[];

extern void boot_prepare_ota_execution(void);

OTA_LOADER_TEXT
int boot_ota_engine_install(void)
{
    const uint8_t *source = __ota_sram_load_start;
    uint8_t *destination = __ota_sram_start;
    uintptr_t load = (uintptr_t)source;
    uintptr_t start = (uintptr_t)destination;
    uintptr_t end = (uintptr_t)__ota_sram_end;
    uintptr_t size;

    if (start != OTA_SRAM_EXPECTED_START || end <= start ||
        end > OTA_SRAM_LIMIT) {
        return -1;
    }
    size = end - start;
    if (load < BOOT_FLASH_START || load > BOOT_FLASH_END - size) {
        return -1;
    }

    while (size-- != 0u) {
        *destination++ = *source++;
    }

    /* This cleans the copied bytes out of any inherited D-cache, disables
     * D-cache/MPU, and invalidates I-cache before SRAM execution. */

    boot_prepare_ota_execution();
    __asm volatile ("dsb sy\n"
                    "isb sy\n" ::: "memory");
    return 0;
}

OTA_LOADER_TEXT
int boot_ota_engine_call(struct bk7258_ota_engine_request_v1 *request)
{
    int result = boot_ota_engine_install();

    if (result != 0) {
        return result;
    }
    return boot_ota_sram_entry(request);
}
