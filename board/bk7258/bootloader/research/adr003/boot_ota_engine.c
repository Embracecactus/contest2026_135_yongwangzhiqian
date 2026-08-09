/*
 * boot_ota_engine.c - minimal BK7258 raw-Flash closure for N15-R2.
 *
 * Every function and literal in this file is forced into .ota_sram.  The
 * linker gives that section an SRAM VMA and a boot-Flash LMA.  No SDK, RTOS,
 * mailbox, libc, global mutable data, or XIP helper is reachable while a
 * Flash controller operation is active.
 *
 * The mutating commands are deliberately compiled behind a zero-valued gate.
 * N15-R2 links and audits the complete implementation but cannot execute an
 * erase/program operation.  Only the read-only PROBE command can run.
 */

#include <stdint.h>

#include "boot_ota_abi.h"
#include "boot_ota_engine.h"

#define OTA_SRAM_TEXT \
    __attribute__((section(".ota_sram.text"), noinline, used))
#define OTA_SRAM_RODATA \
    __attribute__((section(".ota_sram.rodata"), used))
#define OTA_NORETURN __attribute__((noreturn))

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define FLASH_BASE              0x44030000u
#define FLASH_OP_CTRL           (FLASH_BASE + 0x10u)
#define FLASH_DATA_SW_FLASH     (FLASH_BASE + 0x14u)
#define FLASH_DATA_FLASH_SW     (FLASH_BASE + 0x18u)
#define FLASH_CMD_CFG           (FLASH_BASE + 0x1cu)
#define FLASH_ID_REG            (FLASH_BASE + 0x20u)
#define FLASH_STATE             (FLASH_BASE + 0x24u)
#define FLASH_CONFIG            (FLASH_BASE + 0x28u)
#define FLASH_OP_CMD            (FLASH_BASE + 0x54u)

#define FLASH_OP_SW             (1u << 29)
#define FLASH_WP_VALUE          (1u << 30)
#define FLASH_BUSY_SW           (1u << 31)
#define FLASH_ADDR_MASK         0x00ffffffu
#define FLASH_CMD_SHIFT         24u
#define FLASH_CMD_MASK          (0x1fu << FLASH_CMD_SHIFT)
#define FLASH_MODE_SHIFT        4u
#define FLASH_MODE_MASK         (0x1fu << FLASH_MODE_SHIFT)
#define FLASH_MODE_DUAL         1u
#define FLASH_STATUS_DATA_MASK  0xffu
#define FLASH_WRSR_SHIFT        10u
#define FLASH_WRSR_MASK         (0xffffu << FLASH_WRSR_SHIFT)

#define FLASH_CMD_RDSR          3u
#define FLASH_CMD_RDSR2         6u
#define FLASH_CMD_WRSR2         7u
#define FLASH_CMD_READ          5u
#define FLASH_CMD_PROGRAM       12u
#define FLASH_CMD_SECTOR_ERASE  13u
#define FLASH_CMD_READ_ID       20u
#define FLASH_CMD_CLEAR_MODE    22u

/* C86517: status bytes=2, BP[4:0] at bit 2, CMP at bit 14. */

#define FLASH_PROTECT_MASK      ((0x1fu << 2) | (1u << 14))

#define SYS_BASE                0x44010000u
#define SYS_RUN_STATUS          (SYS_BASE + 0x0cu)
#define SYS_CPU1_CONTROL        (SYS_BASE + 0x14u)
#define SYS_CPU2_CONTROL        (SYS_BASE + 0x18u)
#define SYS_CPU0_SLEEP_CONFIG   (SYS_BASE + 0x44u)
#define SYS_CPU_RESET           (1u << 0)
#define SYS_CPU_POWER_DOWN      (1u << 1)
#define SYS_CPU1_POWERED_DOWN   (1u << 9)
#define SYS_CPU2_POWERED_DOWN   (1u << 10)
#define SYS_FLASH_TWO_WIRE      (1u << 7)

#define SCB_CCR                 0xe000ed14u
#define SCB_DCACHE_ENABLE       (1u << 16)
#define MPU_CTRL                0xe000ed94u

#define WDT_APB_STATUS          0x44800004u
#define WDT_APB_CTRL            0x44800010u
#define WDT_AON_CTRL            0x44000600u
#define WDT_KEY1                0x5au
#define WDT_KEY2                0xa5u
#define WDT_PERIOD              8000u

#define BOOT_STACK_LOW          0x28080000u
#define BOOT_STACK_TOP          0x2809f700u
#define OTA_SRAM_BEGIN          0x28000000u
#define OTA_SRAM_END            0x28002000u

#define FLASH_WAIT_BUDGET       0x08000000u
#define AP_WAIT_BUDGET          0x00100000u
#define WDT_FEED_INTERVAL_MASK  0x0000ffffu

/* A volatile load keeps the mutating dispatch in the audited binary while
 * the value itself remains immutable in the Flash load image. */

OTA_SRAM_RODATA
const volatile uint32_t g_bk7258_ota_write_gate =
    BK7258_OTA_ENGINE_WRITE_GATE;

static inline void ota_dsb(void)
{
    __asm volatile ("dsb sy" ::: "memory");
}

static inline void ota_isb(void)
{
    __asm volatile ("isb sy" ::: "memory");
}

OTA_SRAM_TEXT
static void boot_ota_sram_wdt_feed(void)
{
    uint32_t ctrl1 = (WDT_KEY1 << 16) | WDT_PERIOD;
    uint32_t ctrl2 = (WDT_KEY2 << 16) | WDT_PERIOD;

    REG32(WDT_APB_STATUS) = REG32(WDT_APB_STATUS) & ~0x3u;
    REG32(WDT_APB_CTRL) = ctrl1;
    REG32(WDT_APB_CTRL) = ctrl2;
    REG32(WDT_AON_CTRL) = ctrl1;
    REG32(WDT_AON_CTRL) = ctrl2;
}

OTA_SRAM_TEXT OTA_NORETURN
static void boot_ota_sram_fail_reset(
    struct bk7258_ota_engine_request_v1 *request, int32_t error,
    uint32_t detail)
{
    request->result = error;
    request->detail = detail;
    ota_dsb();

    /* Do not return to XIP while the Flash controller may still be busy.
     * Stop feeding both watchdogs and let the existing ~8 s reset contract
     * restart recovery from immutable metadata. */

    for (;;) {
        __asm volatile ("nop");
    }
}

OTA_SRAM_TEXT
static void boot_ota_sram_wait_idle(
    struct bk7258_ota_engine_request_v1 *request, uint32_t detail)
{
    uint32_t remaining = FLASH_WAIT_BUDGET;

    while ((REG32(FLASH_OP_CTRL) & FLASH_BUSY_SW) != 0u) {
        if ((remaining & WDT_FEED_INTERVAL_MASK) == 0u) {
            boot_ota_sram_wdt_feed();
        }
        if (remaining == 0u) {
            boot_ota_sram_fail_reset(
                request, BK7258_OTA_ENGINE_ERR_VERIFY, detail);
        }
        remaining--;
    }
}

OTA_SRAM_TEXT
static void boot_ota_sram_trigger(
    struct bk7258_ota_engine_request_v1 *request, uint32_t command,
    uint32_t address, uint32_t detail)
{
    uint32_t value;

    boot_ota_sram_wait_idle(request, detail);
    value = REG32(FLASH_OP_CMD);
    value &= ~(FLASH_ADDR_MASK | FLASH_CMD_MASK);
    value |= (address & FLASH_ADDR_MASK) |
             ((command << FLASH_CMD_SHIFT) & FLASH_CMD_MASK);
    REG32(FLASH_OP_CMD) = value;
    REG32(FLASH_OP_CTRL) = REG32(FLASH_OP_CTRL) | FLASH_OP_SW;
    boot_ota_sram_wait_idle(request, detail);
}

OTA_SRAM_TEXT
static void boot_ota_sram_prepare_controller(
    struct bk7258_ota_engine_request_v1 *request)
{
    uint32_t value;

    boot_ota_sram_wait_idle(request, 0x1001u);

    /* Match v3.1.1.9 flash_set_line_mode(TWO): clear continuous mode,
     * select controller dual mode, then assert sys2flsh_2wire. */

    value = REG32(FLASH_CONFIG) & ~FLASH_MODE_MASK;
    REG32(FLASH_CONFIG) = value;
    boot_ota_sram_trigger(request, FLASH_CMD_CLEAR_MODE, 0u, 0x1002u);

    REG32(SYS_CPU0_SLEEP_CONFIG) =
        REG32(SYS_CPU0_SLEEP_CONFIG) & ~SYS_FLASH_TWO_WIRE;
    value = REG32(FLASH_CONFIG) & ~FLASH_MODE_MASK;
    value |= FLASH_MODE_DUAL << FLASH_MODE_SHIFT;
    REG32(FLASH_CONFIG) = value;
    REG32(SYS_CPU0_SLEEP_CONFIG) =
        REG32(SYS_CPU0_SLEEP_CONFIG) | SYS_FLASH_TWO_WIRE;
    ota_dsb();
    ota_isb();
}

OTA_SRAM_TEXT
static uint32_t boot_ota_sram_read_id(
    struct bk7258_ota_engine_request_v1 *request)
{
    boot_ota_sram_trigger(request, FLASH_CMD_READ_ID, 0u, 0x1101u);
    return REG32(FLASH_ID_REG) & FLASH_ADDR_MASK;
}

OTA_SRAM_TEXT
static uint32_t boot_ota_sram_read_status(
    struct bk7258_ota_engine_request_v1 *request)
{
    uint32_t status;

    REG32(FLASH_CMD_CFG) = 0u;
    boot_ota_sram_trigger(request, FLASH_CMD_RDSR, 0u, 0x1201u);
    status = REG32(FLASH_STATE) & FLASH_STATUS_DATA_MASK;
    boot_ota_sram_trigger(request, FLASH_CMD_RDSR2, 0u, 0x1202u);
    status |= (REG32(FLASH_STATE) & FLASH_STATUS_DATA_MASK) << 8;
    return status;
}

OTA_SRAM_TEXT
static void boot_ota_sram_write_status(
    struct bk7258_ota_engine_request_v1 *request, uint32_t status)
{
    uint32_t value;

    boot_ota_sram_wait_idle(request, 0x1301u);
    REG32(FLASH_CMD_CFG) = 0u;
    value = REG32(FLASH_CONFIG) & ~FLASH_WRSR_MASK;
    value |= (status & 0xffffu) << FLASH_WRSR_SHIFT;
    REG32(FLASH_CONFIG) = value;
    REG32(FLASH_OP_CTRL) = REG32(FLASH_OP_CTRL) | FLASH_WP_VALUE;
    boot_ota_sram_trigger(request, FLASH_CMD_WRSR2, 0u, 0x1302u);
    REG32(FLASH_OP_CTRL) = REG32(FLASH_OP_CTRL) & ~FLASH_WP_VALUE;
}

OTA_SRAM_TEXT
static int boot_ota_sram_unprotect(
    struct bk7258_ota_engine_request_v1 *request)
{
    uint32_t status = boot_ota_sram_read_status(request);
    uint32_t desired = status & ~FLASH_PROTECT_MASK;

    request->observed_status = status;
    if (desired != status) {
        boot_ota_sram_write_status(request, desired);
        status = boot_ota_sram_read_status(request);
        request->observed_status = status;
        if ((status & FLASH_PROTECT_MASK) != 0u) {
            return BK7258_OTA_ENGINE_ERR_VERIFY;
        }
    }
    return BK7258_OTA_ENGINE_OK;
}

OTA_SRAM_TEXT
static void boot_ota_sram_read_chunk(
    struct bk7258_ota_engine_request_v1 *request, uint32_t address,
    uint32_t words[8])
{
    uint32_t index;

    boot_ota_sram_trigger(request, FLASH_CMD_READ, address, 0x1401u);
    for (index = 0; index < 8u; index++) {
        words[index] = REG32(FLASH_DATA_FLASH_SW);
    }
}

OTA_SRAM_TEXT
static void boot_ota_sram_program_chunk(
    struct bk7258_ota_engine_request_v1 *request, uint32_t address,
    const uint32_t words[8])
{
    uint32_t index;

    boot_ota_sram_wait_idle(request, 0x1501u);
    for (index = 0; index < 8u; index++) {
        REG32(FLASH_DATA_SW_FLASH) = words[index];
    }
    boot_ota_sram_trigger(request, FLASH_CMD_PROGRAM, address, 0x1502u);
}

OTA_SRAM_TEXT
static void boot_ota_sram_erase_sector(
    struct bk7258_ota_engine_request_v1 *request, uint32_t address)
{
    boot_ota_sram_trigger(
        request, FLASH_CMD_SECTOR_ERASE, address, 0x1601u);
}

OTA_SRAM_TEXT
static int boot_ota_sram_words_equal(
    const uint32_t left[8], const uint32_t right[8])
{
    uint32_t index;

    for (index = 0; index < 8u; index++) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

OTA_SRAM_TEXT
static int boot_ota_sram_words_erased(const uint32_t words[8])
{
    uint32_t index;

    for (index = 0; index < 8u; index++) {
        if (words[index] != 0xffffffffu) {
            return 0;
        }
    }
    return 1;
}

OTA_SRAM_TEXT
static int boot_ota_sram_region_contains(
    uint32_t address, uint32_t length, uint32_t start, uint32_t size)
{
    if (length > size || address < start) {
        return 0;
    }
    return address - start <= size - length;
}

OTA_SRAM_TEXT
static int boot_ota_sram_is_journal_range(
    uint32_t address, uint32_t length)
{
    return boot_ota_sram_region_contains(
               address, length, BK7258_OTA_FORWARD_LOG0_START,
               BK7258_OTA_JOURNAL_COPY_SIZE) ||
           boot_ota_sram_region_contains(
               address, length, BK7258_OTA_FORWARD_LOG1_START,
               BK7258_OTA_JOURNAL_COPY_SIZE) ||
           boot_ota_sram_region_contains(
               address, length, BK7258_OTA_REVERSE_LOG0_START,
               BK7258_OTA_JOURNAL_COPY_SIZE) ||
           boot_ota_sram_region_contains(
               address, length, BK7258_OTA_REVERSE_LOG1_START,
               BK7258_OTA_JOURNAL_COPY_SIZE);
}

OTA_SRAM_TEXT
static int boot_ota_sram_is_swap_sector(uint32_t address)
{
    if ((address & (BK7258_OTA_ERASE_SIZE - 1u)) != 0u) {
        return 0;
    }

    return boot_ota_sram_region_contains(
               address, BK7258_OTA_ERASE_SIZE,
               BK7258_OTA_CP_ACTIVE_START, BK7258_OTA_CP_SLOT_SIZE) ||
           boot_ota_sram_region_contains(
               address, BK7258_OTA_ERASE_SIZE,
               BK7258_OTA_CP_STAGING_START, BK7258_OTA_CP_SLOT_SIZE) ||
           boot_ota_sram_region_contains(
               address, BK7258_OTA_ERASE_SIZE,
               BK7258_OTA_AP_ACTIVE_START, BK7258_OTA_AP_SLOT_SIZE) ||
           boot_ota_sram_region_contains(
               address, BK7258_OTA_ERASE_SIZE,
               BK7258_OTA_AP_STAGING_START, BK7258_OTA_AP_SLOT_SIZE) ||
           address == BK7258_OTA_SCRATCH_START;
}

OTA_SRAM_TEXT
static int boot_ota_sram_copy_sector(
    struct bk7258_ota_engine_request_v1 *request, uint32_t source,
    uint32_t destination)
{
    uint32_t data[8];
    uint32_t verify[8];
    uint32_t offset;

    if (source == destination || !boot_ota_sram_is_swap_sector(source) ||
        !boot_ota_sram_is_swap_sector(destination)) {
        return BK7258_OTA_ENGINE_ERR_RANGE;
    }

    boot_ota_sram_erase_sector(request, destination);
    for (offset = 0; offset < BK7258_OTA_ERASE_SIZE;
         offset += BK7258_OTA_WRITE_CHUNK_SIZE) {
        boot_ota_sram_read_chunk(request, source + offset, data);
        boot_ota_sram_program_chunk(request, destination + offset, data);
        boot_ota_sram_read_chunk(request, destination + offset, verify);
        if (!boot_ota_sram_words_equal(data, verify)) {
            request->detail = offset;
            return BK7258_OTA_ENGINE_ERR_VERIFY;
        }
        boot_ota_sram_wdt_feed();
    }
    return BK7258_OTA_ENGINE_OK;
}

OTA_SRAM_TEXT
static int boot_ota_sram_erase_journal(
    struct bk7258_ota_engine_request_v1 *request, uint32_t address)
{
    uint32_t verify[8];
    uint32_t offset;

    if ((address & (BK7258_OTA_ERASE_SIZE - 1u)) != 0u ||
        !boot_ota_sram_is_journal_range(
            address, BK7258_OTA_ERASE_SIZE)) {
        return BK7258_OTA_ENGINE_ERR_RANGE;
    }

    boot_ota_sram_erase_sector(request, address);
    for (offset = 0; offset < BK7258_OTA_ERASE_SIZE;
         offset += BK7258_OTA_WRITE_CHUNK_SIZE) {
        boot_ota_sram_read_chunk(request, address + offset, verify);
        if (!boot_ota_sram_words_erased(verify)) {
            request->detail = offset;
            return BK7258_OTA_ENGINE_ERR_VERIFY;
        }
    }
    return BK7258_OTA_ENGINE_OK;
}

OTA_SRAM_TEXT
static int boot_ota_sram_program_journal(
    struct bk7258_ota_engine_request_v1 *request, uint32_t address)
{
    uint32_t data[8];
    uint32_t verify[8];
    uint32_t index;

    if ((address & (BK7258_OTA_WRITE_CHUNK_SIZE - 1u)) != 0u ||
        !boot_ota_sram_is_journal_range(
            address, BK7258_OTA_WRITE_CHUNK_SIZE)) {
        return BK7258_OTA_ENGINE_ERR_RANGE;
    }

    boot_ota_sram_read_chunk(request, address, verify);
    if (!boot_ota_sram_words_erased(verify)) {
        return BK7258_OTA_ENGINE_ERR_VERIFY;
    }

    for (index = 0; index < 8u; index++) {
        uint32_t base = index * 4u;
        data[index] = (uint32_t)request->chunk[base] |
                      ((uint32_t)request->chunk[base + 1u] << 8) |
                      ((uint32_t)request->chunk[base + 2u] << 16) |
                      ((uint32_t)request->chunk[base + 3u] << 24);
    }
    boot_ota_sram_program_chunk(request, address, data);
    boot_ota_sram_read_chunk(request, address, verify);
    if (!boot_ota_sram_words_equal(data, verify)) {
        return BK7258_OTA_ENGINE_ERR_VERIFY;
    }
    return BK7258_OTA_ENGINE_OK;
}

OTA_SRAM_TEXT
static int boot_ota_sram_restore_protection(
    struct bk7258_ota_engine_request_v1 *request)
{
    uint32_t status = boot_ota_sram_read_status(request);
    uint32_t desired = (status & ~FLASH_PROTECT_MASK) |
                       (request->status_before & FLASH_PROTECT_MASK);

    if (desired != status) {
        boot_ota_sram_write_status(request, desired);
        status = boot_ota_sram_read_status(request);
        if ((status & FLASH_PROTECT_MASK) !=
            (desired & FLASH_PROTECT_MASK)) {
            return BK7258_OTA_ENGINE_ERR_VERIFY;
        }
    }
    request->observed_status = status;
    return BK7258_OTA_ENGINE_OK;
}

OTA_SRAM_TEXT
static int boot_ota_sram_hold_secondaries(void)
{
    uint32_t remaining = AP_WAIT_BUDGET;
    uint32_t value;

    value = REG32(SYS_CPU1_CONTROL);
    value = (value & ~SYS_CPU_RESET) | SYS_CPU_POWER_DOWN;
    REG32(SYS_CPU1_CONTROL) = value;
    value = REG32(SYS_CPU2_CONTROL);
    value = (value & ~SYS_CPU_RESET) | SYS_CPU_POWER_DOWN;
    REG32(SYS_CPU2_CONTROL) = value;
    ota_dsb();
    ota_isb();

    while ((REG32(SYS_RUN_STATUS) &
            (SYS_CPU1_POWERED_DOWN | SYS_CPU2_POWERED_DOWN)) !=
           (SYS_CPU1_POWERED_DOWN | SYS_CPU2_POWERED_DOWN)) {
        if ((remaining & WDT_FEED_INTERVAL_MASK) == 0u) {
            boot_ota_sram_wdt_feed();
        }
        if (remaining == 0u) {
            return BK7258_OTA_ENGINE_ERR_AP_STATE;
        }
        remaining--;
    }
    return BK7258_OTA_ENGINE_OK;
}

OTA_SRAM_TEXT
static int boot_ota_sram_check_environment(
    struct bk7258_ota_engine_request_v1 *request)
{
    uint32_t pointer = (uint32_t)(uintptr_t)request;
    uint32_t primask;
    uint32_t stack;

    __asm volatile ("mrs %0, primask" : "=r" (primask));
    __asm volatile ("mrs %0, msp" : "=r" (stack));

    if (pointer < BOOT_STACK_LOW ||
        pointer > BOOT_STACK_TOP - sizeof(*request) ||
        stack < BOOT_STACK_LOW || stack > BOOT_STACK_TOP ||
        (pointer >= OTA_SRAM_BEGIN && pointer < OTA_SRAM_END)) {
        return BK7258_OTA_ENGINE_ERR_REQUEST;
    }
    if ((primask & 1u) == 0u) {
        return BK7258_OTA_ENGINE_ERR_IRQ;
    }
    if ((REG32(SCB_CCR) & SCB_DCACHE_ENABLE) != 0u ||
        (REG32(MPU_CTRL) & 1u) != 0u) {
        return BK7258_OTA_ENGINE_ERR_CACHE;
    }
    return boot_ota_sram_hold_secondaries();
}

OTA_SRAM_TEXT
int boot_ota_sram_entry(struct bk7258_ota_engine_request_v1 *request)
{
    uint32_t probe[8];
    uint32_t index;
    int result;

    result = boot_ota_sram_check_environment(request);
    if (result != BK7258_OTA_ENGINE_OK) {
        return result;
    }
    if (request->magic != BK7258_OTA_ENGINE_REQUEST_MAGIC ||
        request->version != BK7258_OTA_ENGINE_REQUEST_VERSION ||
        request->size != sizeof(*request)) {
        return BK7258_OTA_ENGINE_ERR_REQUEST;
    }

    request->result = BK7258_OTA_ENGINE_ERR_REQUEST;
    request->detail = 0u;
    boot_ota_sram_prepare_controller(request);
    request->observed_flash_id = boot_ota_sram_read_id(request);
    request->observed_status = boot_ota_sram_read_status(request);
    if (request->observed_flash_id != BK7258_OTA_FLASH_ID) {
        request->result = BK7258_OTA_ENGINE_ERR_FLASH_ID;
        return request->result;
    }

    if (request->command == BK7258_OTA_ENGINE_PROBE) {
        if ((request->source & (BK7258_OTA_WRITE_CHUNK_SIZE - 1u)) != 0u ||
            request->source >
                BK7258_OTA_FLASH_SIZE - BK7258_OTA_WRITE_CHUNK_SIZE) {
            request->result = BK7258_OTA_ENGINE_ERR_RANGE;
            return request->result;
        }
        boot_ota_sram_read_chunk(request, request->source, probe);
        for (index = 0; index < 8u; index++) {
            uint32_t base = index * 4u;
            request->chunk[base] = (uint8_t)probe[index];
            request->chunk[base + 1u] = (uint8_t)(probe[index] >> 8);
            request->chunk[base + 2u] = (uint8_t)(probe[index] >> 16);
            request->chunk[base + 3u] = (uint8_t)(probe[index] >> 24);
        }
        request->result = BK7258_OTA_ENGINE_OK;
        return request->result;
    }

    if (g_bk7258_ota_write_gate != BK7258_OTA_ENGINE_ENABLE_MAGIC) {
        request->result = BK7258_OTA_ENGINE_ERR_DISABLED;
        return request->result;
    }

    result = boot_ota_sram_unprotect(request);
    if (result != BK7258_OTA_ENGINE_OK) {
        request->result = result;
        return result;
    }

    if (request->command == BK7258_OTA_ENGINE_ERASE_JOURNAL_SECTOR) {
        result = boot_ota_sram_erase_journal(request, request->destination);
    } else if (request->command ==
               BK7258_OTA_ENGINE_PROGRAM_JOURNAL_CHUNK) {
        result = boot_ota_sram_program_journal(request, request->destination);
    } else if (request->command == BK7258_OTA_ENGINE_COPY_SECTOR) {
        result = boot_ota_sram_copy_sector(
            request, request->source, request->destination);
    } else if (request->command ==
               BK7258_OTA_ENGINE_RESTORE_PROTECTION) {
        result = boot_ota_sram_restore_protection(request);
    } else {
        result = BK7258_OTA_ENGINE_ERR_REQUEST;
    }

    request->observed_status = boot_ota_sram_read_status(request);
    request->result = result;
    return result;
}
