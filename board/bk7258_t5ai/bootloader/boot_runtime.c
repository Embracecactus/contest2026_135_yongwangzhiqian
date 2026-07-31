/*
 * boot_runtime.c - BK7258 reset and application-handoff normalization.
 *
 * The cache/MPU sequence is a clean-room reconstruction of the official
 * BK7258 SMP SDK v3.1.1.9 normal_bootloader/bootloader.bin:
 *
 *   SHA-256 105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6
 *
 * Recovered functions:
 *   0x02000148 boot_early_soc_init
 *   0x02000280 boot_runtime_init
 *   0x0200180c disable_mpu
 *   0x020018c0 clear_mpu_regions
 *   0x020018e0 configure_dcache_mpu
 *
 * BK7258 uses an Armv8-M System Control Space at 0xe000ed00.  The official
 * binary addresses standard SCB/MPU registers through that base; these are
 * not a vendor-private cache block.
 *
 * Product hardening beyond the official binary:
 *   - Reset_Handler invalidates I-cache unconditionally in its first cache
 *     line, because downloader/SYSRESETREQ resets preserve stale CPU0 lines.
 *   - I-cache is invalidated again immediately before the application jump.
 *   - CPU1 and CPU2 are held in reset and powered down so a reset cannot
 *     inherit dirty private cache state from a previous AP run.
 *
 * Freestanding: no libc, mutable globals, .data or .bss.
 */

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define SYS_BASE                 0x44010000u
#define SYS_CPU1_CONTROL         (SYS_BASE + 0x14u)
#define SYS_CPU2_CONTROL         (SYS_BASE + 0x18u)
#define SYS_CPU_RESET            (1u << 0)
#define SYS_CPU_POWER_DOWN       (1u << 1)

#define FLASH_CTRL_BASE          0x4b100000u
#define FLASH_CONFIG_2C4         (FLASH_CTRL_BASE + 0x2c4u)
#define FLASH_CONFIG_2C8         (FLASH_CTRL_BASE + 0x2c8u)
#define FLASH_STATUS_7C8         (FLASH_CTRL_BASE + 0x7c8u)
#define FLASH_AUX_BASE           0x44890000u

#define SCB_BASE                 0xe000ed00u
#define SCB_VTOR                 (SCB_BASE + 0x008u)
#define SCB_CCR                  (SCB_BASE + 0x014u)
#define SCB_SHCSR                (SCB_BASE + 0x024u)
#define SCB_CLIDR                (SCB_BASE + 0x078u)
#define SCB_CCSIDR               (SCB_BASE + 0x080u)
#define SCB_CSSELR               (SCB_BASE + 0x084u)
#define SCB_ICIALLU              (SCB_BASE + 0x250u)
#define SCB_DCISW                (SCB_BASE + 0x260u)
#define SCB_DCCISW               (SCB_BASE + 0x274u)

#define SCB_CCR_DC               (1u << 16)
#define SCB_CCR_IC               (1u << 17)
#define SCB_SHCSR_MEMFAULTENA     (1u << 16)

#define MPU_TYPE                 (SCB_BASE + 0x090u)
#define MPU_CTRL                 (SCB_BASE + 0x094u)
#define MPU_RNR                  (SCB_BASE + 0x098u)
#define MPU_RLAR                 (SCB_BASE + 0x0a0u)

#define TCM_BASE                 0xe001e000u
#define ITCMCR                   (TCM_BASE + 0x010u)
#define DTCMCR                   (TCM_BASE + 0x014u)

#define BOOT_VECTOR_ADDRESS      0x02000000u

static inline void boot_dmb(void)
{
    __asm volatile ("dmb sy" ::: "memory");
}

static inline void boot_dsb(void)
{
    __asm volatile ("dsb sy" ::: "memory");
}

static inline void boot_isb(void)
{
    __asm volatile ("isb sy" ::: "memory");
}

static void boot_icache_invalidate_all(void)
{
    boot_dsb();
    boot_isb();
    REG32(SCB_ICIALLU) = 0;
    boot_dsb();
    boot_isb();
}

/*
 * Maintain every level-1 data-cache line by set/way.
 *
 * BK7258 STAR r1p0 reports 128 sets and four ways.  The operand construction
 * below intentionally mirrors the official bootloader:
 *   set  = CCSIDR[27:13] << 5
 *   way  = CCSIDR[12:3]  << 30
 */

static void boot_dcache_setway(uint32_t operation)
{
    uint32_t ccsidr;
    uint32_t set;

    REG32(SCB_CSSELR) = 0;
    boot_dsb();
    ccsidr = REG32(SCB_CCSIDR);
    set = ((ccsidr & 0x0fffffffu) >> 13) << 5;

    for (;;)
        {
            uint32_t way = (ccsidr & 0x1fffu) >> 3;

            for (;;)
                {
                    REG32(operation) = (set & 0x1fe0u) | (way << 30);
                    if (way == 0)
                        {
                            break;
                        }

                    way--;
                }

            if (set == 0)
                {
                    break;
                }

            set -= 0x20u;
        }

    boot_dsb();
    boot_isb();
}

static void boot_mpu_disable_and_clear(void)
{
    uint32_t region_count;
    uint32_t region;

    boot_dmb();
    REG32(SCB_SHCSR) &= ~SCB_SHCSR_MEMFAULTENA;
    REG32(MPU_CTRL) &= ~1u;
    boot_dsb();
    boot_isb();

    /* Official BK7258 has 16 regions and clears all 16.  Bound the value read
     * from MPU_TYPE so corrupt reset residue cannot turn this into an
     * unbounded MMIO loop. */

    region_count = (REG32(MPU_TYPE) >> 8) & 0xffu;
    if (region_count == 0 || region_count > 16)
        {
            region_count = 16;
        }

    for (region = 0; region < region_count; region++)
        {
            REG32(MPU_RNR) = region;
            REG32(MPU_RLAR) = 0;
        }

    boot_dsb();
    boot_isb();
}

static void boot_early_soc_init(void)
{
    uint32_t value;

    /* Exact functional equivalent of official 0x02000148. */

    value = REG32(SYS_BASE + 0x40u);
    if ((value & (1u << 3)) != 0)
        {
            REG32(SYS_BASE + 0x40u) = value & ~(1u << 3);
        }

    value = REG32(SYS_BASE + 0x30u);
    if ((value & (1u << 15)) == 0)
        {
            REG32(SYS_BASE + 0x30u) = value | (1u << 15);
        }

    value = REG32(FLASH_CONFIG_2C8);
    if ((value & 0x3u) != 0x3u)
        {
            REG32(FLASH_CONFIG_2C8) = value | 0x3u;
        }

    if ((REG32(FLASH_CONFIG_2C4) & 1u) == 0 &&
        (REG32(FLASH_STATUS_7C8) & 0x0fu) == 7u)
        {
            REG32(FLASH_AUX_BASE + 0x08u) = 7u;
        }
}

static void boot_secondary_cores_power_down(void)
{
    uint32_t value;

    value = REG32(SYS_CPU1_CONTROL);
    value &= ~SYS_CPU_RESET;
    value |= SYS_CPU_POWER_DOWN;
    REG32(SYS_CPU1_CONTROL) = value;

    value = REG32(SYS_CPU2_CONTROL);
    value &= ~SYS_CPU_RESET;
    value |= SYS_CPU_POWER_DOWN;
    REG32(SYS_CPU2_CONTROL) = value;

    boot_dsb();
    boot_isb();
}

void boot_reset_prepare(void)
{
    boot_early_soc_init();

    /* Official boot_runtime_init: establish vectors, enable both TCM banks,
     * invalidate+enable I-cache if it is implemented.  Reset_Handler already
     * invalidated it unconditionally, so only the enable is conditional. */

    REG32(SCB_VTOR) = BOOT_VECTOR_ADDRESS;
    REG32(ITCMCR) |= 1u;
    REG32(DTCMCR) |= 1u;

    if ((REG32(SCB_CLIDR) & 1u) != 0)
        {
            if ((REG32(SCB_CCR) & SCB_CCR_IC) == 0)
                {
                    boot_icache_invalidate_all();
                    REG32(SCB_CCR) |= SCB_CCR_IC;
                    boot_dsb();
                    boot_isb();
                }
        }

    /* Official Reset_Handler performs invalidate-by-set/way here, without
     * changing the D-cache enable bit. */

    boot_dcache_setway(SCB_DCISW);
    boot_secondary_cores_power_down();
}

void boot_prepare_app_handoff(void)
{
    /* Official configure_dcache_mpu(0). */

    REG32(SCB_CSSELR) = 0;
    boot_dsb();
    REG32(SCB_CCR) &= ~SCB_CCR_DC;
    boot_dsb();
    boot_dcache_setway(SCB_DCCISW);
    boot_mpu_disable_and_clear();

    /* Official v3.1.1.9 leaves I-cache enabled.  Invalidate it while retaining
     * that state so the app cannot observe stale XIP lines after a downloader
     * reset or same-session reflash. */

    boot_icache_invalidate_all();
}
