/*
 * boot_clock.c - BK7258 cold-start DPLL enable (product-grade).
 *
 * On cold reset the BootROM leaves EN_DPLL=0.  This module mirrors the
 * Armino SDK sys_hal_early_init analog-register sequence to enable and
 * calibrate the DPLL before jumping to the app, so the app can
 * deterministically switch the core mux to 320 MHz on both cold and soft
 * reset paths.
 *
 * Board chip_id = 0x23A40910 = PM_CHIP_ID_MP_C.  The SDK has no explicit
 * MP_C branch, so the default else-branch ANA_REG values apply
 * (sys_hal.c:2865-2876).
 *
 * Product-grade safety:
 *   - Skip the full sequence if EN_DPLL is already set (soft-reset path
 *     where the loader already enabled DPLL).
 *   - Every analog-SPI write has a bounded timeout (ANA_SPI_TIMEOUT); if
 *     any write times out, the remaining sequence is aborted and control
 *     returns to the caller.  The app's 26 MHz fallback handles the case
 *     where the DPLL ends up not enabled.
 *   - No step can hang the bootloader indefinitely.
 *
 * Scope: DPLL enable + SPI recalibration only.  Does NOT switch the core
 * mux (M1) or the flash clock (M2 cksel_flash) -- switching flash to the
 * 480 MHz source stalls on this board; the core mux is left for the app.
 *
 * Freestanding: no libc, no .data/.bss globals, only stack locals.
 */

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* MMIO.                                                              */
/* ------------------------------------------------------------------ */

#define REG32(addr)        (*(volatile uint32_t *)(addr))

/* Analog register block (SYS_ANA_REG0 = 0x44010100, REGn = +4*n). */

#define ANA_SPI_STATE      0x440100E8u
#define ANA_REG0           0x44010100u
#define ANA_REG2           0x44010108u
#define ANA_REG3           0x4401010Cu
#define ANA_REG5           0x44010114u
#define ANA_REG8           0x44010120u
#define ANA_REG9           0x44010124u
#define ANA_REG10          0x44010128u
#define ANA_REG11          0x4401012Cu
#define ANA_REG12          0x44010130u
#define ANA_REG13          0x44010134u
#define ANA_REG25          0x44010164u

#define EN_DPLL_BIT        (1u << 5)

/* Analog-SPI busy-wait timeout (iterations).  Sized so that even at the
 * slowest cold-start clock (26 MHz) a genuine SPI transfer completes
 * well within the budget, but a stuck state machine aborts in < 1 ms.
 */

#define ANA_SPI_TIMEOUT    100000u

/* UART1 polled output (matches boot_main.c). */

#define UART1_FIFO         REG32(0x4583001Cu)
#define UART1_STATUS       REG32(0x45830018u)
#define UART1_TX_READY     (1u << 20)

/* ------------------------------------------------------------------ */
/* Freestanding helpers (no libc, stack-only).                        */
/* ------------------------------------------------------------------ */

static void clk_putc(char c)
{
    int i;
    for (i = 0; i < 100000; i++) {
        if (UART1_STATUS & UART1_TX_READY) break;
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
    for (s = 28; s >= 0; s -= 4)
        clk_putc(hex[(v >> s) & 0xFu]);
}

/* Coarse busy-wait (bus-independent; sized for 26 MHz cold-start). */

static void clk_delay(volatile uint32_t n)
{
    while (n-- != 0) { }
}

/*
 * Analog register write helpers.  Every write to an ANA_REG triggers an
 * internal analog-SPI transfer; the SPI busy bit in ANA_SPI_STATE[idx]
 * (idx = register number) must clear before the next write or it is lost.
 * Mirrors sys_ll_set_analog_reg_value (sys_ll.h:51-59).
 *
 * All helpers return 0 on success, -1 on SPI-busy timeout.
 */

static int ana_wait(uint32_t addr)
{
    uint32_t idx = (addr - ANA_REG0) >> 2;
    uint32_t t = ANA_SPI_TIMEOUT;

    while (REG32(ANA_SPI_STATE) & (1u << idx))
        if (--t == 0)
            return -1;
    return 0;
}

static int ana_write(uint32_t addr, uint32_t val)
{
    REG32(addr) = val;
    return ana_wait(addr);
}

static int ana_or(uint32_t addr, uint32_t bits)
{
    REG32(addr) = REG32(addr) | bits;
    return ana_wait(addr);
}

static int ana_and(uint32_t addr, uint32_t mask)
{
    REG32(addr) = REG32(addr) & mask;
    return ana_wait(addr);
}

static int ana_rmw(uint32_t addr, uint32_t clear, uint32_t set)
{
    REG32(addr) = (REG32(addr) & ~clear) | set;
    return ana_wait(addr);
}

/* ------------------------------------------------------------------ */
/* DPLL SPI recalibration (sys_hal_cali_dpll, sys_hal.c:2659-2696).   */
/* Returns 0 on success, -1 on SPI timeout.                           */
/* ------------------------------------------------------------------ */

static int boot_cali_dpll(void)
{
    /* Lower the SPI trigger and settle. */

    if (ana_and(ANA_REG0, ~(1u << 19)) < 0)   /* spitrig = 0 */
        return -1;
    clk_delay(120);

    /* Rising edge starts recalibration; gate unlock-detect during settle. */

    if (ana_rmw(ANA_REG0, (1u << 4), (1u << 19)) < 0)  /* spitrig=1, spideten=0 */
        return -1;
    clk_delay(3400);

    /* Re-enable detect (the SDK's de-facto "lock wait"). */

    if (ana_or(ANA_REG0, (1u << 4)) < 0)      /* spideten = 1 */
        return -1;
    clk_delay(3400);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Step functions — each returns 0 on success, -1 on failure.         */
/* ------------------------------------------------------------------ */

static int step1_dpll_power(void)
{
    /* ANA_REG5: power up audpll briefly, enable DPLL + DCO, set adc_div,
     * re-power-down audpll. */

    uint32_t v = REG32(ANA_REG5);
    v &= ~(1u << 13);                              /* PWDAUDPLL = 0 */
    v |= (1u << 5) | (1u << 3) | (1u << 2);       /* EN_DPLL | NC_3_3 | EN_DCO */
    if (ana_write(ANA_REG5, v) < 0) return -1;
    if (ana_rmw(ANA_REG5, (0x3u << 10), (0x1u << 10)) < 0) return -1;  /* adc_div */
    if (ana_or(ANA_REG5, (1u << 13)) < 0) return -1;  /* PWDAUDPLL = 1 */
    return 0;
}

static int step2_band_dsptrig(void)
{
    /* ANA_REG0: band field, safe-mode replacement, dsptrig pulse. */

    if (ana_or(ANA_REG0, (0x13u << 20)) < 0) return -1;   /* band = 0x13 */
    if (ana_write(ANA_REG0, 0xF1305B56u) < 0) return -1;  /* dpll_tsten=0 */
    if (ana_or(ANA_REG0, (1u << 26)) < 0) return -1;      /* dsptrig = 1 */
    if (ana_and(ANA_REG0, ~(1u << 26)) < 0) return -1;    /* dsptrig = 0 */
    return 0;
}

static int step3_xtal_bias(void)
{
    /* ANA_REG2/3: xtal tuning and bias. */

    if (ana_write(ANA_REG2, 0x7E003450u) < 0) return -1;  /* xtal ctune=0x50 */
    if (ana_write(ANA_REG3, 0xC5F00B88u) < 0) return -1;
    return 0;
}

static int step4_latched_block(void)
{
    /* spi_latch1v on -> ANA_REG8/9/10-13/25 (default branch for MP_C) ->
     * ANA_REG3.inbufen0v9 -> latch off. */

    if (ana_or(ANA_REG9, (1u << 9)) < 0) return -1;       /* spi_latch1v = 1 */
    if (ana_write(ANA_REG8,  0x57E62F26u) < 0) return -1;
    if (ana_write(ANA_REG9,  0x787BC8A4u) < 0) return -1;
    if (ana_write(ANA_REG10, 0xC3D543A7u) < 0) return -1;
    if (ana_write(ANA_REG11, 0xB47E99F8u) < 0) return -1;
    if (ana_write(ANA_REG12, 0xB47ECF20u) < 0) return -1;
    if (ana_write(ANA_REG13, 0x727070EEu) < 0) return -1;
    if (ana_write(ANA_REG25, 0x0961FAA4u) < 0) return -1;
    if (ana_or(ANA_REG3, (1u << 6)) < 0) return -1;       /* inbufen0v9 = 1 */
    if (ana_and(ANA_REG9, ~(1u << 9)) < 0) return -1;     /* spi_latch1v = 0 */
    return 0;
}

static int step5_recalibrate(void)
{
    clk_delay(120);
    return boot_cali_dpll();
}

/* ------------------------------------------------------------------ */
/* Public: cold-start DPLL enable.                                    */
/*                                                                    */
/* Called from c_main() after UART init, before the FAL partition     */
/* scan.  Bounded and fail-safe: if any analog-SPI write times out,   */
/* the remaining sequence is aborted and control returns to c_main;   */
/* the app's 26 MHz fallback handles EN_DPLL=0.                       */
/* ------------------------------------------------------------------ */

void boot_clock_cold_init(void)
{
    /* Guard: skip if DPLL already enabled (soft-reset / loader residue). */

    if (REG32(ANA_REG5) & EN_DPLL_BIT)
        return;

    /* Run the cold-init sequence.  Abort on any SPI-write timeout. */

    if (step1_dpll_power() < 0 ||
        step2_band_dsptrig() < 0 ||
        step3_xtal_bias() < 0 ||
        step4_latched_block() < 0 ||
        step5_recalibrate() < 0)
    {
        clk_puts("BClk FAIL\r\n");
        return;
    }

    /* Evidence: confirm EN_DPLL landed. */

    clk_puts("BClk A5=");
    clk_puthex(REG32(ANA_REG5));
    clk_puts(" A9=");
    clk_puthex(REG32(ANA_REG9));
    clk_puts("\r\n");
}