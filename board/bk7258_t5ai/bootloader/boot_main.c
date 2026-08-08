/*
 * boot_main.c - BK7258 Tier-1 bootloader C main.
 *
 *   uint32_t c_main(void);
 *
 * Responsibilities (Tier-1 features I / A, with J handled by start.S):
 *   I  UART1 logging of boot progress.
 *   A  FAL partition table parse -> locate "app", derive its logical address.
 *      App header validation (MSP / Reset Thumb / "BK7236\0\0" magic).
 * On success c_main prints "jump to:0x02010000\r\n", "JMP\r\n" and returns
 * the app logical vector address (e.g. 0x02010000). On failure it prints
 * "BAD\r\n" plus a short reason, then takes the bounded watchdog-reset
 * failure path (never returns).
 *
 * The app logical address equals FLASH_BASE + partition.offset, so the same
 * scan works if the table is later moved to a real flash partition.
 *
 * Freestanding: no libc, no mutable globals -> no .bss zeroing needed by
 * c_main (start.S calls c_main directly without a C runtime startup).
 */

#include <stdint.h>
#include <stddef.h>

#include "boot_bl1_manifest.h"
#include "boot_bl1_handoff_core.h"
#include "boot_bl1_policy.h"
#include "boot_bl2_contract.h"
#include "boot_wdt.h"
#include "../chip/include/bk7258_partition_layout.h"

#ifndef BK7258_BL1_MINIMAL
#  define BK7258_BL1_MINIMAL 0u
#endif

#if !BK7258_BL1_MINIMAL
#  include "boot_ota_select.h"
#  include "boot_n17_ecc_wrapper.h"
#endif

extern void boot_clock_cold_init(void);

/* ------------------------------------------------------------------ */
/* MMIO: BK7258 UART1 (matches the verified init in start.S).         */
/* ------------------------------------------------------------------ */
#define UART1_FIFO_ADDR   0x4583001Cu
#define UART1_STATUS_ADDR 0x45830018u
#define UART1_TX_READY    (1u << 20)   /* bit20: TX-FIFO-not-full (assumed) */

#ifndef BK7258_BOOT_N17_ECC_VECTOR_SELFTEST
#  define BK7258_BOOT_N17_ECC_VECTOR_SELFTEST 0u
#endif

#ifndef BK7258_BL1_MANIFEST_ENFORCE
#  define BK7258_BL1_MANIFEST_ENFORCE 0u
#endif

#ifndef BK7258_BL1_MANIFEST_RAW_PAGE
#  define BK7258_BL1_MANIFEST_RAW_PAGE 0u
#endif

#ifndef BK7258_BL1_TRUSTENGINE_PROBE
#  define BK7258_BL1_TRUSTENGINE_PROBE 0u
#endif

#ifndef BK7258_BL1_BOOT_CONTROL_STAGING
#  define BK7258_BL1_BOOT_CONTROL_STAGING 0u
#endif

#if BK7258_BL1_BOOT_CONTROL_STAGING
#  ifndef BK7258_BL1_BOOT_CONTROL_RAW_OFFSET
#    error "staging boot control requires a raw Flash offset"
#  endif
#  ifndef BK7258_BL1_PRIMARY_MANIFEST_XIP
#    error "staging boot control requires the Primary Manifest XIP address"
#  endif
#  ifndef BK7258_BL1_SECONDARY_MANIFEST_XIP
#    error "staging boot control requires the Secondary Manifest XIP address"
#  endif
#endif

#define REG32(addr)       (*(volatile uint32_t *)(addr))
#define UART1_FIFO        REG32(UART1_FIFO_ADDR)
#define UART1_STATUS      REG32(UART1_STATUS_ADDR)

/* ------------------------------------------------------------------ */
/* FAL partition table (mirrors struct fal_partition from the BK SDK,  */
/* fal_def.h: magic_word/name[24]/flash_name[24]/offset/len/reserved, */
/* 64 bytes per entry). XIP-readable const in .rodata.                 */
/* ------------------------------------------------------------------ */
#define FAL_DEV_NAME_MAX  24
#define FAL_PART_MAGIC    0x45503130u   /* 'E','P','1','0' (fal_partition.c) */

#define FLASH_BASE        BK7258_FLASH_XIP_BASE
#define BL2_RAM_BASE      BK7258_BL2_SRAM_BASE
#define BL2_RAM_END       BK7258_BL2_SRAM_END
#define BL2_COPY_SIZE     BK7258_BL2_COPY_SIZE
#define BL2_VECTOR_COUNT  64u
#define BL2_DATA_RAM_BASE BL2_RAM_BASE
#define BL2_DATA_RAM_END  BL2_RAM_END
#define SCB_ICIALLU       0xe000ef50u

struct fal_partition {
    uint32_t magic_word;
    char     name[FAL_DEV_NAME_MAX];
    char     flash_name[FAL_DEV_NAME_MAX];
    long     offset;        /* logical offset on flash device */
    size_t   len;
    uint32_t reserved;
};

__attribute__((used))
const struct fal_partition fal_partition_table[] = {
    { FAL_PART_MAGIC, "bootloader", "beken_onchip_crc",
      BK7258_ROLE_BOOT_LOGICAL_OFFSET, BK7258_ROLE_BOOT_LOGICAL_SIZE, 0u },
    { FAL_PART_MAGIC, "cp_app",     "beken_onchip_crc",
      BK7258_ROLE_SLOT_A_CP_LOGICAL_OFFSET,
      BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE, 0u },
    { FAL_PART_MAGIC, "ap_app",     "beken_onchip_crc",
      BK7258_ROLE_SLOT_A_AP_LOGICAL_OFFSET,
      BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE, 0u },
    { FAL_PART_MAGIC, "bl2",        "beken_onchip_crc",
      BK7258_ROLE_BL2_LOGICAL_OFFSET,
      BK7258_ROLE_BL2_LOGICAL_SIZE, 0u },
};
#define FAL_PART_COUNT  (sizeof(fal_partition_table) / sizeof(fal_partition_table[0]))

/* ------------------------------------------------------------------ */
/* UART output (self-implemented; correct nibble order, no v>>32 UB).  */
/* ------------------------------------------------------------------ */
static void uart_putc(char c)
{
    /*
     * Poll bit20 (TX-FIFO-not-full). The poll is bounded: if the bit polarity
     * is ever inverted on silicon, we degrade gracefully to the same
     * write-through behavior the verified minimal bootloader used, rather
     * than hanging the boot.
     */
    for (int i = 0; i < 100000; i++) {
        if (UART1_STATUS & UART1_TX_READY) {
            break;
        }
    }
    UART1_FIFO = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

static void print_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int s;
    /* for(s=28; s>=0; s-=4): avoids the v>>32 undefined behaviour that the
       earlier probe fell into (top nibble got shifted into oblivion). */
    for (s = 28; s >= 0; s -= 4) {
        uart_putc(hex[(v >> s) & 0xFu]);
    }
}

static void log_u32(const char *label, uint32_t value)
{
    uart_puts(label);
    uart_puts("0x");
    print_hex32(value);
    uart_puts("\r\n");
}

static int boot_bl2_policy_store(
    const struct bk7258_bl2_boot_policy_s *policy)
{
    volatile struct bk7258_bl2_boot_policy_s *handoff =
        (volatile struct bk7258_bl2_boot_policy_s *)(uintptr_t)
        BK7258_BL2_BOOT_POLICY_ADDRESS;

    if (policy == (const struct bk7258_bl2_boot_policy_s *)0 ||
        policy->magic != BK7258_BL2_BOOT_POLICY_MAGIC ||
        policy->version != BK7258_BL2_BOOT_POLICY_VERSION ||
        policy->preferred_slot > BK7258_BL2_BOOT_POLICY_SLOT_SECONDARY ||
        (policy->fallback_slot != BK7258_BL2_BOOT_POLICY_SLOT_NONE &&
         (policy->fallback_slot > BK7258_BL2_BOOT_POLICY_SLOT_SECONDARY ||
          policy->fallback_slot == policy->preferred_slot)) ||
        policy->source > BK7258_BL2_BOOT_POLICY_SOURCE_N17 ||
        policy->check != bk7258_bl2_boot_policy_check(policy))
      {
        return -1;
      }

    /* Publish magic last so a reset cannot expose a partially written policy
     * as valid to BL2. */
    handoff->magic = 0;
    __asm volatile ("dsb sy" ::: "memory");
    handoff->version = policy->version;
    handoff->preferred_slot = policy->preferred_slot;
    handoff->fallback_slot = policy->fallback_slot;
    handoff->source = policy->source;
    handoff->state = policy->state;
    handoff->generation_low = policy->generation_low;
    handoff->generation_high = policy->generation_high;
    handoff->check = policy->check;
    __asm volatile ("dsb sy" ::: "memory");
    handoff->magic = policy->magic;
    __asm volatile ("dsb sy" ::: "memory");
    return 0;
}

/* Read-only TrustEngine/Dubhe observation for the reversible bring-up path.
 * These addresses are from the v3.1.1.9 BK7258 register headers and have
 * also been read successfully through SWD on the target.  The probe never
 * changes OTP_SET (in particular DIRECT_RD), never reads key material, and
 * is disabled in the normal boot build. */
#if BK7258_BL1_TRUSTENGINE_PROBE
#  define BK7258_DUBHE_BASE              0x4b110000u
#  define BK7258_DUBHE_TOP_STAT          (BK7258_DUBHE_BASE + 0x100u)
#  define BK7258_DUBHE_OTP_MGR           (BK7258_DUBHE_BASE + 0x400u)
#  define BK7258_DUBHE_OTP_SPACE         (BK7258_DUBHE_BASE + 0x1000u)
#  define BK7258_DUBHE_OTP_UPDATE_STAT   (BK7258_DUBHE_OTP_MGR + 0x10u)
#  define BK7258_DUBHE_OTP_LCS           (BK7258_DUBHE_OTP_SPACE + 0x68u)
#  define BK7258_DUBHE_OTP_LOCK          (BK7258_DUBHE_OTP_SPACE + 0x7cu)

static void trustengine_readonly_probe(void)
{
    log_u32("DUBHE VER ", REG32(BK7258_DUBHE_TOP_STAT + 0x00u));
    log_u32("DUBHE CFG1 ", REG32(BK7258_DUBHE_TOP_STAT + 0x04u));
    log_u32("DUBHE CFG2 ", REG32(BK7258_DUBHE_TOP_STAT + 0x08u));
    log_u32("OTP SET ", REG32(BK7258_DUBHE_OTP_MGR));
    log_u32("OTP STAT ", REG32(BK7258_DUBHE_OTP_UPDATE_STAT));
    log_u32("OTP LCS ", REG32(BK7258_DUBHE_OTP_LCS));
    log_u32("OTP LOCK ", REG32(BK7258_DUBHE_OTP_LOCK));
}
#endif

/* ------------------------------------------------------------------ */
/* Partition scan + helpers.                                          */
/* ------------------------------------------------------------------ */
static int name_eq(const char *a, const char *b)
{
    int i;
    for (i = 0; i < FAL_DEV_NAME_MAX; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
    return 1;   /* equal across the full FAL_DEV_NAME_MAX width */
}

static const struct fal_partition *fal_find(const char *name)
{
    size_t i;
    for (i = 0; i < FAL_PART_COUNT; i++) {
        if (fal_partition_table[i].magic_word != FAL_PART_MAGIC) {
            continue;
        }
        if (name_eq(fal_partition_table[i].name, name)) {
            return &fal_partition_table[i];
        }
    }
    return (const struct fal_partition *)0;
}

/* ------------------------------------------------------------------ */
/* BL2 header validation (ported from bk7236_min_bl.S to C).          */
/* ------------------------------------------------------------------ */
static int validate_bl2(uint32_t image)
{
    volatile uint32_t *vec   = (volatile uint32_t *)image;
    volatile uint32_t *magic = (volatile uint32_t *)(image + 0x100u);
    uint32_t msp  = vec[0];
    uint32_t rst  = vec[1];
    size_t index;

    /* The current standalone MCUboot BL2 keeps its text, data and reset stack
     * inside the contracted 128 KiB execution window. */
    /* The vector-table MSP is required to be word aligned.  Eight-byte stack
     * alignment is an exception-entry policy (CCR.STKALIGN), not a valid
     * reason to reject an otherwise bootable BL2 image. */
    if ((msp & 3u) != 0u ||
        msp < BL2_DATA_RAM_BASE || msp > BL2_DATA_RAM_END) {
        uart_puts("BAD\r\nmsp align/OOR\r\n");
        return 0;
    }
    /* Reset_Handler must have the Thumb bit set. */
    if ((rst & 1u) == 0u) {
        uart_puts("BAD\r\nreset no-thumb\r\n");
        return 0;
    }
    if ((rst & ~1u) < BL2_RAM_BASE || (rst & ~1u) >= BL2_RAM_END) {
        uart_puts("BAD\r\nbl2 reset OOR\r\n");
        return 0;
    }

    /* BL2 is copied as a complete Cortex-M vector table.  Checking every
     * entry prevents a damaged handler word from redirecting execution to
     * XIP, erased Flash, or an unrelated SRAM window after the handoff. */
    for (index = 2; index < BL2_VECTOR_COUNT; index++) {
        uint32_t handler = vec[index];

        if ((handler & 1u) == 0u ||
            (handler & ~1u) < BL2_RAM_BASE ||
            (handler & ~1u) >= BL2_RAM_END) {
            uart_puts("BAD\r\nbl2 vector OOR\r\n");
            return 0;
        }
    }

    /* app magic @ app_vec + 0x100 == "BK7236\0\0"
       bytes  42 4B 37 32 -> word 0x32374B42
              33 36 00 00 -> word 0x00003633 */
    /* BL2 is an MCUboot ELF, not a BK7236 application image: offset 0x100
     * is executable code.  BL2 authenticates the selected application. */
    (void)magic;
    return 1;
}

/* The Beken flash controller exposes CRC-decoded executable partitions as a
 * continuous XIP byte stream.  BL1 therefore copies logical bytes, never the
 * raw 34/32 flash packets.  The requested copy length must not exceed the
 * valid logical payload encoded in the BL2 partition. */

static int load_bl2_to_ram(uint32_t source, size_t size)
{
    volatile const uint32_t *from = (volatile const uint32_t *)source;
    volatile uint32_t *to = (volatile uint32_t *)BL2_RAM_BASE;
    struct bk7258_bl1_vector_s authorized;
    struct bk7258_bl1_vector_s loaded;
    const struct bk7258_bl1_handoff_window_s window =
      {
        BL2_DATA_RAM_BASE, BL2_DATA_RAM_END,
        BL2_RAM_BASE, BL2_RAM_END
      };
    size_t index;

    if (size != BL2_COPY_SIZE || (size & 3u) != 0u) {
        uart_puts("BAD\r\nbl2 size\r\n");
        return 0;
    }

    authorized.msp = from[0];
    authorized.reset = from[1];
    for (index = 0; index < size / sizeof(uint32_t); index++) {
        to[index] = from[index];
    }

    __asm volatile ("dsb sy" ::: "memory");
    REG32(SCB_ICIALLU) = 0;
    __asm volatile ("dsb sy; isb sy" ::: "memory");

    loaded.msp = to[0];
    loaded.reset = to[1];
    if (!bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window)) {
        uart_puts("BAD\r\nbl2 copy vector\r\n");
        return 0;
    }

    return validate_bl2(BL2_RAM_BASE);
}

/* ------------------------------------------------------------------ */
/* C entry called from start.S. Returns app logical vector address.   */
/* ------------------------------------------------------------------ */
__attribute__((used))
uint32_t c_main(void)
{
    const struct fal_partition *app;
#if !BK7258_BL1_MINIMAL
    struct bk7258_boot_ota_policy_s boot_policy;
#endif
    struct bk7258_bl2_boot_policy_s bl2_policy;
    uint32_t app_vec = 0;
#if !BK7258_BL1_MANIFEST_RAW_PAGE
    uint32_t manifest_xip;
#endif
    int cold_ok = 0;
#if BK7258_BL1_MANIFEST_ENFORCE
    int manifest_status;
#if BK7258_BL1_MANIFEST_RAW_PAGE
    uint8_t manifest_record[BK7258_BL1_MANIFEST_SIZE];
#endif
#endif
    int pair_ok = 0;
    int attempt;
    int slot;
    int retry;
    uint8_t slot_order[2];
#if BK7258_BL1_BOOT_CONTROL_STAGING
    uint8_t boot_control_record[BK7258_BL1_BOOT_FLAG_RECORD_SIZE];
    const struct bk7258_bl1_boot_flag_layout_s boot_control_layout =
      {
        BK7258_BL1_PRIMARY_MANIFEST_XIP,
        BK7258_BL1_SECONDARY_MANIFEST_XIP
      };
    enum bk7258_bl1_boot_flag_status_e boot_flag_status;
#endif

    uart_puts("u_bootloader enter\r\n");

    /* --- WDT (product-grade): arm both APB + AON watchdogs (~8 s timeout).
     * Mirrors vendor bootloader sub_2000FE4: SYS_CTRL config + key unlock.
     * If boot_clock_cold_init or app validation hangs, WDT resets the chip.
     */

    boot_wdt_init();
    boot_wdt_feed();

    /* --- Cold-start DPLL enable + SPI recalibration.
     * On cold reset the BootROM leaves EN_DPLL=0; boot_clock_cold_init()
     * mirrors the Armino SDK sys_hal_early_init analog-register sequence.
     * If any analog-SPI write times out (ANA_SPI_TIMEOUT), the function
     * returns early and the app sees DPLL off.
     *
     * Retry up to 3 times: SPI timeouts can be transient (power-on noise).
     * Worst-case time: ~100 ms per attempt × 3 = 300 ms << 8 s WDT.
     */

    for (retry = 0; retry < 3; retry++) {
        boot_wdt_feed();
        boot_clock_cold_init();

        /* Check DPLL enable (ANA_REG5 bit5).  If set, cold-init succeeded. */
        if (REG32(0x44010114u) & (1u << 5)) {
            cold_ok = 1;
            break;
        }

        uart_puts("BClk RETRY\r\n");
    }

    boot_wdt_feed();

    if (!cold_ok) {
        /* All retries exhausted: DPLL not enabled.  Do NOT jump to app
         * (app would see BootROM default state, DPLL off, DVFS would try
         * to switch to cksel=2 with DPLL off — undefined behavior).
         * Use the recovered official short watchdog-reset failure path. */
        uart_puts("BClk RETRY FAIL\r\n");
        boot_wdt_fail_reset();
    }

#if BK7258_BL1_TRUSTENGINE_PROBE
    trustengine_readonly_probe();
#endif

    /* N17 probe-only target check: the vector has no release secret. */
#if !BK7258_BL1_MINIMAL && BK7258_BOOT_N17_ECC_VECTOR_SELFTEST
    if (bk7258_boot_n17_ecc_vector_selftest() < 0) {
        uart_puts("BAD\r\nn17 ecc\r\n");
        boot_wdt_fail_reset();
    }
    uart_puts("N17E1\r\n");
#endif

#if BK7258_BL1_MINIMAL
    bl2_policy.magic = BK7258_BL2_BOOT_POLICY_MAGIC;
    bl2_policy.version = BK7258_BL2_BOOT_POLICY_VERSION;
    bl2_policy.preferred_slot = BK7258_BL2_BOOT_POLICY_SLOT_PRIMARY;
    bl2_policy.fallback_slot = BK7258_BL2_BOOT_POLICY_SLOT_SECONDARY;
    bl2_policy.source = BK7258_BL2_BOOT_POLICY_SOURCE_FIXED;
    bl2_policy.state = 0u;
    bl2_policy.generation_low = 0u;
    bl2_policy.generation_high = 0u;
    bl2_policy.check = bk7258_bl2_boot_policy_check(&bl2_policy);
    uart_puts("B1FIX\r\n");
    uart_puts("B1POLA\r\n");
#else
    if (boot_ota_resolve_policy(&boot_policy) < 0)
      {
        uart_puts("BAD\r\nboot policy\r\n");
        boot_wdt_fail_reset();
      }

    uart_puts(boot_policy.source == BK7258_BOOT_OTA_POLICY_N15 ?
              "B1N15\r\n" :
              boot_policy.source == BK7258_BOOT_OTA_POLICY_N17 ?
              "B1N17\r\n" : "B1FIX\r\n");
    uart_puts(boot_policy.preferred_slot == BK7258_BOOT_OTA_SLOT_A ?
              "B1POLA\r\n" : "B1POLB\r\n");

    bl2_policy.magic = BK7258_BL2_BOOT_POLICY_MAGIC;
    bl2_policy.version = BK7258_BL2_BOOT_POLICY_VERSION;
    bl2_policy.preferred_slot = boot_policy.preferred_slot;
    bl2_policy.fallback_slot =
        boot_policy.fallback_slot == BK7258_BOOT_OTA_POLICY_SLOT_NONE ?
        BK7258_BL2_BOOT_POLICY_SLOT_NONE : boot_policy.fallback_slot;
    bl2_policy.source = boot_policy.source;
    bl2_policy.state = boot_policy.state;
    bl2_policy.generation_low = (uint32_t)boot_policy.generation;
    bl2_policy.generation_high = (uint32_t)(boot_policy.generation >> 32);
    bl2_policy.check = bk7258_bl2_boot_policy_check(&bl2_policy);
#endif

    /* --- FAL partition parse -> find the dedicated NuttX MCUboot BL2.
     * BL1 must never enter CP slot A directly once BL2 owns A/B selection.
     */

    boot_wdt_feed();
    app = fal_find("bl2");
    if (app == (const struct fal_partition *)0) {
        uart_puts("BAD\r\nno bl2 part\r\n");
        boot_wdt_fail_reset();
    }
    if ((uint32_t)app->offset != BK7258_ROLE_BL2_LOGICAL_OFFSET ||
        app->len < BL2_COPY_SIZE) {
        uart_puts("BAD\r\nbl2 layout\r\n");
        boot_wdt_fail_reset();
    }
    /* The first-stage code remains in XIP.  The CSV-owned primary BL2 keeps
     * its historical address.  A second, equally sized logical slot follows
     * it in the pre-LittleFS gap.  Each candidate has its own board-owned
     * Manifest record in the boot tail; a failed primary never reaches the
     * MCUboot handoff and is followed by a deterministic secondary attempt. */
    /* The active recoverable layout has no BL1 boot_flag provider, so its
     * default remains Primary -> Secondary.  The opt-in staging build reads
     * the second page of the documented 12 KiB control area through the raw
     * Flash path.  It never writes that page. */
#if BK7258_BL1_BOOT_CONTROL_STAGING
    if (boot_ota_raw_read((void *)0, BK7258_BL1_BOOT_CONTROL_RAW_OFFSET,
                          boot_control_record,
                          sizeof(boot_control_record)) == 0)
      {
        boot_flag_status = bk7258_bl1_boot_flag_slot_order(
          boot_control_record, sizeof(boot_control_record),
          &boot_control_layout, slot_order);
      }
    else
      {
        boot_flag_status = bk7258_bl1_boot_flag_slot_order(
          (const uint8_t *)0, 0u,
          (const struct bk7258_bl1_boot_flag_layout_s *)0, slot_order);
      }
    uart_puts(boot_flag_status == BK7258_BL1_BOOT_FLAG_VALID_SECONDARY ?
              "B1FLAGB\r\n" :
              boot_flag_status == BK7258_BL1_BOOT_FLAG_VALID_PRIMARY ?
              "B1FLAGA\r\n" : "B1FLAGDEFAULT\r\n");
#else
#if BK7258_BL1_MINIMAL
    slot_order[0] = BK7258_BL2_BOOT_POLICY_SLOT_PRIMARY;
    slot_order[1] = BK7258_BL2_BOOT_POLICY_SLOT_SECONDARY;
#else
    (void)bk7258_bl1_boot_flag_slot_order((const uint8_t *)0, 0u,
      (const struct bk7258_bl1_boot_flag_layout_s *)0, slot_order);
#endif
#endif
    for (attempt = 0; attempt < 2; attempt++) {
        slot = slot_order[attempt];
        app_vec = slot == 0 ? FLASH_BASE + (uint32_t)app->offset :
                              BK7258_BL2_SECONDARY_XIP;
#if !BK7258_BL1_MANIFEST_RAW_PAGE
        manifest_xip = slot == 0 ? BK7258_BL1_MANIFEST_PRIMARY_XIP_ADDRESS :
                                   BK7258_BL1_MANIFEST_SECONDARY_XIP_ADDRESS;
#endif
#if !BK7258_BL1_MANIFEST_ENFORCE
#if !BK7258_BL1_MANIFEST_RAW_PAGE
        (void)manifest_xip;
#endif
#endif
        uart_puts(slot == 0 ? "B1PRIMARY\r\n" : "B1SECONDARY\r\n");
        log_u32("partition bl2 @ ", app_vec);

#if BK7258_BL1_MANIFEST_ENFORCE
        /* Board-owned authorization only: it binds this exact BL2 before
         * BL1 copies it into SRAM.  It is not an assertion about Beken's
         * unpublished BootROM Manifest ABI.  A software P-256 verification
         * has no opportunity to service either watchdog while it is inside
         * the arithmetic backend, so give it the extended BL2 window. */
        boot_wdt_feed_period(BL2_WDT_PERIOD);
#if BK7258_BL1_MANIFEST_RAW_PAGE
        /* The candidate page lives in the CSV data partitions, not in the
         * CRC-decoded executable XIP tail.  Read only the first 256 bytes;
         * the candidate verifier checks that the remainder of this 4 KiB
         * record container is erased. */
        uart_puts("B1PAGE\r\n");
        manifest_status = boot_ota_raw_read(
            (void *)0,
            slot == 0 ? BK7258_ROLE_OTA_MANIFEST_A_OFFSET :
                        BK7258_ROLE_OTA_MANIFEST_B_OFFSET,
            manifest_record, sizeof(manifest_record));
        if (manifest_status == 0)
          {
            manifest_status = bk7258_beken_manifest_verify_buffer(
                manifest_record, app_vec, BL2_COPY_SIZE, BL2_RAM_BASE);
          }
        else
          {
            manifest_status = -5;
          }
#else
        manifest_status = bk7258_bl1_manifest_verify_at(
            manifest_xip, app_vec, BL2_COPY_SIZE, BL2_RAM_BASE);
#endif
        boot_wdt_feed();
        if (manifest_status < 0) {
            log_u32("bl1 manifest rc ", (uint32_t)(-manifest_status));
            uart_puts(slot == 0 ? "B1PRIMARY BAD\r\n" :
                                  "B1SECONDARY BAD\r\n");
            continue;
        }
#endif

        if (!validate_bl2(app_vec) ||
            !load_bl2_to_ram(app_vec, BL2_COPY_SIZE)) {
            uart_puts(slot == 0 ? "B1PRIMARY VECTOR BAD\r\n" :
                                  "B1SECONDARY VECTOR BAD\r\n");
            continue;
        }

        pair_ok = 1;
        break;
    }

    if (!pair_ok) {
        uart_puts("BAD\r\nno bl2 candidate\r\n");
        boot_wdt_fail_reset();
    }

    if (boot_bl2_policy_store(&bl2_policy) < 0) {
        uart_puts("BAD\r\nbl2 policy handoff\r\n");
        boot_wdt_fail_reset();
    }

    /* --- Feed WDT one last time before jumping to app.
     * The app's DVFS bring-up runs quickly (< 1 ms), but if it ever hangs
     * before the NuttX scheduler takes over WDT management, the hardware
     * WDT still fires. */

    boot_wdt_feed();
    __asm volatile ("cpsid i" ::: "memory");
    log_u32("bl2 ram @ ", BL2_RAM_BASE);
    uart_puts("BL2RAM\r\n");
    return BL2_RAM_BASE;
}
