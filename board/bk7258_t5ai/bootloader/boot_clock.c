/*
 * boot_clock.c - BK7258 cold-start DPLL enable + flash clock init.
 *
 * Ported from Armino SDK sys_hal_early_init (sys_hal.c:2793-2881) +
 * sys_hal_dpll_cpu_flash_time_early_init (sys_hal.c:2697-2746) +
 * sys_hal_cali_dpll (sys_hal.c:2659-2696).
 *
 * Board chip_id = 0x23A40910 = PM_CHIP_ID_MP_C (sys_types.h:138).
 * SDK has no explicit MP_C branch -- this board falls into the default
 * else-branch for ANA_REG10/11/12/13/25 (sys_hal.c:2865-2876).
 *
 * This module only enables the DPLL + configures the flash clock (M2).
 * It does NOT set the core mux/divider (M1) -- that is the app's job
 * (bk7258_clock_bringup_320m).  Keeping the core at the BootROM baseline
 * ensures the bootloader itself runs at a safe, known frequency and does
 * not stall on unknown cold-start mux transitions.
 *
 * On success the DPLL 480 MHz source is available and flash runs at 80 MHz
 * (480/6).  On failure boot_clock_cold_init returns silently and the app
 * falls back to the 26 MHz baseline (its existing fallback path).
 *
 * Freestanding: no libc, no .data/.bss globals, only stack locals.
 */

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* MMIO helper (matches boot_main.c REG32 convention).                */
/* ------------------------------------------------------------------ */

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ------------------------------------------------------------------ */
/* Register addresses.                                                */
/* ------------------------------------------------------------------ */

#define ANA_SPI_STATE     0x440100E8u
#define ANA_REG0          0x44010100u
#define ANA_REG2          0x44010108u
#define ANA_REG3          0x4401010Cu
#define ANA_REG5          0x44010114u
#define ANA_REG8          0x44010120u
#define ANA_REG9          0x44010124u
#define ANA_REG10         0x44010128u
#define ANA_REG11         0x4401012Cu
#define ANA_REG12         0x44010130u
#define ANA_REG13         0x44010134u
#define ANA_REG25         0x44010164u
#define CPU_CLK_DIV_MODE2 0x44010024u

/* UART evidence output (freestanding polled, matches boot_main.c). */

#define UART1_FIFO   REG32(0x4583001Cu)
#define UART1_STATUS REG32(0x45830018u)
#define UART1_READY  (1u << 20)

/* ------------------------------------------------------------------ */
/* Freestanding delay and UART helpers (no libc, stack-only).         */
/* ------------------------------------------------------------------ */

static void clk_putc(char c)
{
    int i;
    for (i = 0; i < 100000; i++) {
        if (UART1_STATUS & UART1_READY) break;
    }
    UART1_FIFO = (uint32_t)(uint8_t)c;
}

static void clk_puts(const char *s)
{
    while (*s) clk_putc(*s++);
}

static void clk_puthex(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int s;
    for (s = 28; s >= 0; s -= 4) {
        clk_putc(hex[(v >> s) & 0xFu]);
    }
}

/* Coarse bus-independent delay.  Sized so that even on the slowest relevant
 * residue clock (26 MHz) the wait covers the SDK's ~150 us settle at ~3400
 * CPU cycles (each loop iteration = several real ticks).
 */

static void clk_delay(volatile uint32_t n)
{
    while (n-- != 0) { }
}

/* ------------------------------------------------------------------ */
/* Analog register helpers.  Every write busy-waits the SPI state
 * machine, mirroring sys_ll_set_analog_reg_value (sys_ll.h:51-59).
 * SPI busy bit index = register number (ANA_REG0=0 .. ANA_REG25=25).
 * ------------------------------------------------------------------ */

static void ana_wait(uint32_t idx)
{
    while (REG32(ANA_SPI_STATE) & (1u << idx)) { }
}

static void ana_set(uint32_t addr, uint32_t val, uint32_t idx)
{
    REG32(addr) = val;
    ana_wait(idx);
}

static void ana_or(uint32_t addr, uint32_t bits, uint32_t idx)
{
    REG32(addr) = REG32(addr) | bits;
    ana_wait(idx);
}

static void ana_and(uint32_t addr, uint32_t mask, uint32_t idx)
{
    REG32(addr) = REG32(addr) & mask;
    ana_wait(idx);
}

static void ana_rmw(uint32_t addr, uint32_t clear, uint32_t set, uint32_t idx)
{
    REG32(addr) = (REG32(addr) & ~clear) | set;
    ana_wait(idx);
}

/* ------------------------------------------------------------------ */
/* DPLL SPI recalibration (mirrors sys_hal_cali_dpll, sys_hal.c:2659).
 * The SDK's gating condition in sys_hal_dpll_cpu_flash_time_early_init
 * is a tautology (|| instead of &&), so this always runs unconditionally.
 * ------------------------------------------------------------------ */

static void boot_cali_dpll(void)
{
    /* Lower the SPI trigger. */

    ana_and(ANA_REG0, ~(1u << 19), 0);        /* spitrig = 0 */
    clk_delay(120);

    /* Rising edge to start recalibration; gate unlock-detect during settle. */

    ana_rmw(ANA_REG0, (1u << 4), (1u << 19), 0);  /* spitrig=1, spideten=0 */
    clk_delay(3400);

    /* Re-enable detect; this is the de-facto "lock wait". */

    ana_or(ANA_REG0, (1u << 4), 0);            /* spideten = 1 */
    clk_delay(3400);
}

/* ------------------------------------------------------------------ */
/* Public: cold-start DPLL + flash clock init.                        */
/* ------------------------------------------------------------------ */

void boot_clock_cold_init(void)
{
    uint32_t v;

    /* == (1) ANA_REG5: enable DPLL, DCO, and NC_3_3.  Clear PWDAUDPLL
     *    first (power up audpll for re-init), set the enables, set
     *    adc_div=1 (GPIO <= 3.3V), then re-set PWDAUDPLL=1.            */

    v = REG32(ANA_REG5);
    v &= ~(1u << 13);                              /* PWDAUDPLL = 0 */
    v |= (1u << 5) | (1u << 3) | (1u << 2);       /* EN_DPLL | NC_3_3 | EN_DCO */
    ana_set(ANA_REG5, v, 5);

    ana_rmw(ANA_REG5, (0x3u << 10), (0x1u << 10), 5);  /* adc_div = 1 */
    ana_or(ANA_REG5, (1u << 13), 5);               /* PWDAUDPLL = 1 */

    /* == (2) ANA_REG0: band field + full replacement + dsptrig pulse. */

    ana_or(ANA_REG0, (0x13u << 20), 0);            /* band = 0x13 */
    ana_set(ANA_REG0, 0xF1305B56u, 0);             /* dpll_tsten=0 */
    ana_or(ANA_REG0, (1u << 26), 0);               /* dsptrig = 1 */
    ana_and(ANA_REG0, ~(1u << 26), 0);             /* dsptrig = 0 */

    /* == (3) ANA_REG2/3: xtal and bias configuration. */

    ana_set(ANA_REG2, 0x7E003450u, 2);             /* xtal ctune=0x50 */
    ana_set(ANA_REG3, 0xC5F00B88u, 3);

    /* == (4) spi_latch1v on -> ANA_REG8/9/10-13/25 -> latch off.
     *    Default branch values (sys_hal.c:2865-2876, MP_C hits default). */

    ana_or(ANA_REG9, (1u << 9), 9);                /* spi_latch1v = 1 */

    ana_set(ANA_REG8,  0x57E62F26u, 8);            /* bias (non-V2_3) */
    ana_set(ANA_REG9,  0x787BC8A4u, 9);            /* unconditional */
    ana_set(ANA_REG10, 0xC3D543A7u, 10);           /* MP_C / default */
    ana_set(ANA_REG11, 0xB47E99F8u, 11);
    ana_set(ANA_REG12, 0xB47ECF20u, 12);
    ana_set(ANA_REG13, 0x727070EEu, 13);
    ana_set(ANA_REG25, 0x0961FAA4u, 25);
    ana_or(ANA_REG3, (1u << 6), 3);                /* ANA_REG3.inbufen0v9 = 1 */

    ana_and(ANA_REG9, ~(1u << 9), 9);              /* spi_latch1v = 0 */

    /* == (5) DPLL SPI recalibration (the SDK does this unconditionally). */

    clk_delay(120);
    boot_cali_dpll();

    /* == (6) Flash clock: set M2 ckdiv_flash=1, cksel_flash=480M (0x2).
     *    This matches the MP_C/default branch of sys_hal_dpll_cpu_flash
     *    _time_early_init (sys_hal.c:2714-2723).                       */

    v = REG32(CPU_CLK_DIV_MODE2);
    if (((v >> 26) & 0x3u) != 0x1u) {
        v = (v & ~(0x3u << 26)) | (0x1u << 26);   /* ckdiv_flash = 1 */
        REG32(CPU_CLK_DIV_MODE2) = v;
    }
    v = REG32(CPU_CLK_DIV_MODE2);
    if (((v >> 24) & 0x3u) != 0x2u) {
        v = (v & ~(0x3u << 24)) | (0x2u << 24);   /* cksel_flash = 480M */
        REG32(CPU_CLK_DIV_MODE2) = v;
    }

    /* == (7) Evidence line: confirm the DPLL enable landed. */

    clk_puts("BClk A5=");
    clk_puthex(REG32(ANA_REG5));
    clk_puts(" A9=");
    clk_puthex(REG32(ANA_REG9));
    clk_puts("\r\n");
}