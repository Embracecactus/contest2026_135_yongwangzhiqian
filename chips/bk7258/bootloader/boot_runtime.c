/* SPDX-License-Identifier: Apache-2.0 */
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

#include "boot_wdt.h"

#define SYS_BASE                 0x44010000u
#define SYS_CPU_RUN_STATUS       (SYS_BASE + 0x0cu)
#define SYS_CPU1_CONTROL         (SYS_BASE + 0x14u)
#define SYS_CPU2_CONTROL         (SYS_BASE + 0x18u)
#define SYS_CPU_RESET            (1u << 0)
#define SYS_CPU_POWER_DOWN       (1u << 1)
#define SYS_CPU_HALT             (1u << 3)
#define SYS_CPU1_RUNNING         (1u << 5)
#define SYS_CPU2_RUNNING         (1u << 6)

#if defined(BK7258_BL2_CONSOLE_UART)
#  define BOOT_CONSOLE_UART BK7258_BL2_CONSOLE_UART
#else
#  define BOOT_CONSOLE_UART BK7258_BL1_CONSOLE_UART
#endif

#if BOOT_CONSOLE_UART == 0
#  define BOOT_UART_BASE         0x44820000u
#  define SYS_BOOT_UART_CLOCK_EN (1u << 2)
#elif BOOT_CONSOLE_UART == 1
#  define BOOT_UART_BASE         0x45830000u
#  define SYS_BOOT_UART_CLOCK_EN (1u << 10)
#elif BOOT_CONSOLE_UART == 2
#  define BOOT_UART_BASE         0x45840000u
#  define SYS_BOOT_UART_CLOCK_EN (1u << 11)
#endif

#if BOOT_CONSOLE_UART < 3
#  define BOOT_UART_CONFIG       (BOOT_UART_BASE + 0x10u)
#  define BOOT_UART_FIFO_CONFIG  (BOOT_UART_BASE + 0x14u)
#  define BOOT_UART_FIFO_STATUS  (BOOT_UART_BASE + 0x18u)
#  define BOOT_UART_INT_ENABLE   (BOOT_UART_BASE + 0x20u)
#  define BOOT_UART_TX_FINISHED  (1u << 17)
#  define BOOT_UART_DRAIN_LOOPS  1000000u
#  define SYS_BOOT_UART_CLOCK    (SYS_BASE + 0x30u)
#endif

/* The official UART1 deinit additionally clears this UART1-only branch.
 * UART0/UART2 have no corresponding write in the recovered boot contract. */
#define SYS_UART1_DEVICE_CLK     (SYS_BASE + 0x80u)
#define SYS_UART1_DEVICE_CLK_EN  (1u << 15)

#define AP_BOOT_STATE_MAGIC      0x2809f000u
#define CORE_STOP_WAIT_LOOPS     10000u

#define OTP_APB_BASE             0x4b100000u
#define OTP_PUF_BUSY             (OTP_APB_BASE + 0x2c4u)
#define OTP_MEM_REPAIR_CTRL      (OTP_APB_BASE + 0x2c8u)
#define OTP_MEM_REPAIR_VALUE     (OTP_APB_BASE + 0x7c8u)
#define MEM_CHECK_BASE           0x44890000u

#define FLASH_CTRL_BASE          0x44030000u
#define FLASH_OP_CTRL            (FLASH_CTRL_BASE + 0x10u)
#define FLASH_ID                 (FLASH_CTRL_BASE + 0x20u)
#define FLASH_CONFIG             (FLASH_CTRL_BASE + 0x28u)
#define FLASH_OP_CMD             (FLASH_CTRL_BASE + 0x54u)
#define FLASH_BUSY               (1u << 31)
#define FLASH_OP_SW              (1u << 29)
#define FLASH_OP_TYPE_MASK       (0x1fu << 24)
#define FLASH_OP_TYPE_RDID       (20u << 24)
#define FLASH_WAIT_LOOPS         1000000u

#define SYS_FLASH_CLOCK          (SYS_BASE + 0x24u)
#define SYS_FLASH_DIV_MASK       (0x3u << 26)
#define SYS_FLASH_DIV_FAST       (0x1u << 26)
#define SYS_FLASH_DIV_SAFE       (0x3u << 26)

#define FLASH_ID_GD25WQ32E       0x00c86516u
#define FLASH_ID_GD25WQ64E       0x00c86517u
#define FLASH_ID_GD25Q32C        0x00c84016u
#define FLASH_ID_GD25LQ128E      0x00c86018u

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

    value = REG32(OTP_MEM_REPAIR_CTRL);
    if ((value & 0x3u) != 0x3u)
        {
            REG32(OTP_MEM_REPAIR_CTRL) = value | 0x3u;
        }

    if ((REG32(OTP_PUF_BUSY) & 1u) == 0 &&
        (REG32(OTP_MEM_REPAIR_VALUE) & 0x0fu) == 7u)
        {
            REG32(MEM_CHECK_BASE + 0x08u) = 7u;
        }
}

/* Reset-path semantics of official A/B FUN_02000358(1).  The official binary
 * consumes a private pre-runtime JEDEC-ID cache at 0x28000118, but our image
 * does not own its unseen producer.  Obtain the same input with the public
 * v3.1.1.9 flash_ll_get_id() RDID operation after controller normalization.
 *
 * The operation returns the XIP controller to two-line continuous-read mode
 * and chooses the Flash divider before the later clock helper selects DPLL.
 * No erase/program/status-register command is issued and no Flash contents
 * are changed.  The official unbounded busy wait is capped for fail-closed
 * bootloader operation.
 */

void boot_flash_reset_prepare(void)
{
    uint32_t flash_id;
    uint32_t value;
    uint32_t count;

    REG32(SYS_CPU_RUN_STATUS) &= ~(1u << 9);

    value = REG32(FLASH_CONFIG);
    REG32(FLASH_CONFIG) = (value & 0xfc00000fu) | 0x10u;
    REG32(FLASH_OP_CMD) |= 0x36000000u;
    REG32(FLASH_OP_CTRL) &= 0x40000000u;

    for (count = 0; count < FLASH_WAIT_LOOPS; count++)
        {
            if ((REG32(FLASH_OP_CTRL) & FLASH_BUSY) == 0)
                {
                    break;
                }
        }

    if (count == FLASH_WAIT_LOOPS)
        {
            boot_wdt_fail_reset();
        }

    /* flash_ll_get_id(): select RDID, assert op_sw, wait for busy_sw to
     * deassert, then consume rd_flash_id.  Keep the bounded wait used by the
     * board BL1 instead of importing the SDK's unbounded loop. */

    value = REG32(FLASH_OP_CMD);
    REG32(FLASH_OP_CMD) = (value & ~FLASH_OP_TYPE_MASK) | FLASH_OP_TYPE_RDID;
    REG32(FLASH_OP_CTRL) |= FLASH_OP_SW;

    for (count = 0; count < FLASH_WAIT_LOOPS; count++)
        {
            if ((REG32(FLASH_OP_CTRL) & FLASH_BUSY) == 0)
                {
                    break;
                }
        }

    if (count == FLASH_WAIT_LOOPS)
        {
            boot_wdt_fail_reset();
        }

    flash_id = REG32(FLASH_ID) & 0x00ffffffu;

    value = REG32(SYS_FLASH_CLOCK) & ~SYS_FLASH_DIV_MASK;
    if (flash_id == FLASH_ID_GD25WQ32E ||
        flash_id == FLASH_ID_GD25WQ64E ||
        flash_id == FLASH_ID_GD25Q32C ||
        flash_id == FLASH_ID_GD25LQ128E)
        {
            value |= SYS_FLASH_DIV_FAST;
        }
    else
        {
            value |= SYS_FLASH_DIV_SAFE;
        }

    REG32(SYS_FLASH_CLOCK) = value;
    boot_dsb();
}

static int boot_secondary_core_power_down(uint32_t control,
                                          uint32_t running_status)
{
    uint32_t value;
    uint32_t count;

    /* Match the official v3.1.1.9 control ordering instead of combining
     * reset and power-down in one write.  A power-down request issued while
     * the core clock is still running can be ignored by BK7258, including
     * the reset-bit update in the same register transaction.
     *
     * reset=0 holds the secondary core in reset; reset=1 releases it.  The
     * generated SDK field comments state the opposite, but reset_cpuN_core()
     * and the observed run-status bits make the hardware convention clear.
     */

    value = REG32(control);
    value &= ~SYS_CPU_RESET;
    REG32(control) = value;
    boot_dsb();

    /* The SDK power-off path then gates the core clock before requesting
     * power-down.  Retain its bounded settling delay in this freestanding
     * boot context, where no scheduler-backed delay is available.
     */

    value |= SYS_CPU_HALT;
    REG32(control) = value;
    for (count = 0; count < 1000u; count++)
        {
            __asm volatile ("nop");
        }

    value |= SYS_CPU_POWER_DOWN;
    REG32(control) = value;
    boot_dsb();

    /* Do not invalidate the old AP session until hardware confirms that the
     * core is no longer released.  Leaving the session valid is fail-closed:
     * CP will refuse to overlay a still-running AP generation.
     */

    for (count = 0; count < CORE_STOP_WAIT_LOOPS; count++)
        {
            if ((REG32(SYS_CPU_RUN_STATUS) & running_status) == 0)
                {
                    return 1;
                }
        }

    return 0;
}

static void boot_secondary_cores_power_down(void)
{
    int cpu1_stopped;
    int cpu2_stopped;

    /* Stop the SMP secondary before its primary. */

    cpu2_stopped = boot_secondary_core_power_down(SYS_CPU2_CONTROL,
                                                   SYS_CPU2_RUNNING);
    cpu1_stopped = boot_secondary_core_power_down(SYS_CPU1_CONTROL,
                                                   SYS_CPU1_RUNNING);

    boot_dsb();
    boot_isb();

    if (cpu1_stopped && cpu2_stopped)
        {
            /* A CPU0-only reset preserves shared SRAM.  Once both old AP
             * cores are held, invalidate the stale APBS generation so the
             * freshly booted CP may initialize a new RPTUN/BT/Wi-Fi session.
             * bk7258_ap_state_prepare() clears the remaining shared ABI
             * before releasing CPU1.
             */

            REG32(AP_BOOT_STATE_MAGIC) = 0;
            boot_dsb();
        }
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

void boot_console_prepare_app_handoff(void)
{
#if BOOT_CONSOLE_UART < 3
    uint32_t count;

    /* Exact v3.1.1.9 A/B handoff ordering calls the console deinit routine
     * before configure_dcache_mpu(0), VTOR/MSP and the final branch.  This
     * project keeps the selected console alive across BL1 -> SRAM BL2 for BL2
     * diagnostics, so quiesce it only at the final BL2 -> CP boundary.
     *
     * Official function 0x02000a7c waits indefinitely for FIFO status bit 17.
     * Bound that wait here: a damaged UART must not prevent a verified image
     * from booting forever.  The following masks and clock bits reproduce the
     * UART1 branch at 0x02000a1c in the official A/B binary. */

    for (count = 0; count < BOOT_UART_DRAIN_LOOPS; count++)
        {
            if ((REG32(BOOT_UART_FIFO_STATUS) & BOOT_UART_TX_FINISHED) != 0)
                {
                    break;
                }
        }

    REG32(BOOT_UART_CONFIG) &= ~0x1bu;
    REG32(BOOT_UART_FIFO_CONFIG) &= ~0x3040u;
    REG32(BOOT_UART_INT_ENABLE) &= ~0x42u;
#if BOOT_CONSOLE_UART == 1
    REG32(SYS_UART1_DEVICE_CLK) &= ~SYS_UART1_DEVICE_CLK_EN;
#endif
    REG32(SYS_BOOT_UART_CLOCK) &= ~SYS_BOOT_UART_CLOCK_EN;
    boot_dsb();
    boot_isb();
#endif
}
