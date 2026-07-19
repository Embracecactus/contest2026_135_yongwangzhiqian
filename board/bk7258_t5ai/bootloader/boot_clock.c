/*
 * boot_clock.c - BK7258 cold-start DPLL enable.
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
 */

static void ana_wait(uint32_t idx)
{
    while (REG32(ANA_SPI_STATE) & (1u << idx)) { }
}

static void ana_write(uint32_t addr, uint32_t val)
{
    REG32(addr) = val;
    ana_wait((addr - ANA_REG0) >> 2);
}

static void ana_or(uint32_t addr, uint32_t bits)
{
    REG32(addr) = REG32(addr) | bits;
    ana_wait((addr - ANA_REG0) >> 2);
}

static void ana_and(uint32_t addr, uint32_t mask)
{
    REG32(addr) = REG32(addr) & mask;
    ana_wait((addr - ANA_REG0) >> 2);
}

static void ana_rmw(uint32_t addr, uint32_t clear, uint32_t set)
{
    REG32(addr) = (REG32(addr) & ~clear) | set;
    ana_wait((addr - ANA_REG0) >> 2);
}

/* ------------------------------------------------------------------ */
/* DPLL SPI recalibration (sys_hal_cali_dpll, sys_hal.c:2659-2696).   */
/* ------------------------------------------------------------------ */

static void boot_cali_dpll(void)
{
    /* Lower the SPI trigger and settle. */

    ana_and(ANA_REG0, ~(1u << 19));            /* spitrig = 0 */
    clk_delay(120);

    /* Rising edge starts recalibration; gate unlock-detect during settle. */

    ana_rmw(ANA_REG0, (1u << 4), (1u << 19));  /* spitrig=1, spideten=0 */
    clk_delay(3400);

    /* Re-enable detect (the SDK's de-facto "lock wait"). */

    ana_or(ANA_REG0, (1u << 4));               /* spideten = 1 */
    clk_delay(3400);
}

/* ------------------------------------------------------------------ */
/* Public: cold-start DPLL enable.                                    */
/*                                                                    */
/* Called from c_main() after UART init, before the FAL partition     */
/* scan.  If this hangs, recover by re-flashing the previous          */
/* bootloader (sources are in git).                                   */
/* ------------------------------------------------------------------ */

void boot_clock_cold_init(void)
{
    uint32_t v;

    /* (1) ANA_REG5: power up audpll briefly, then enable DPLL + DCO.
     *     Clear PWDAUDPLL (bit13) -> power up, set EN_DPLL (bit5) +
     *     NC_3_3 (bit3) + EN_DCO (bit2), set adc_div=1, re-set PWDAUDPLL. */

    v  = REG32(ANA_REG5);
    v &= ~(1u << 13);
    v |= (1u << 5) | (1u << 3) | (1u << 2);
    ana_write(ANA_REG5, v);
    ana_rmw(ANA_REG5, (0x3u << 10), (0x1u << 10));   /* adc_div = 1 */
    ana_or(ANA_REG5, (1u << 13));                     /* PWDAUDPLL = 1 */

    /* (2) ANA_REG0: DPLL band field, safe-mode replacement, dsptrig pulse. */

    ana_or(ANA_REG0, (0x13u << 20));                  /* band = 0x13 */
    ana_write(ANA_REG0, 0xF1305B56u);                 /* dpll_tsten=0 */
    ana_or(ANA_REG0, (1u << 26));                     /* dsptrig = 1 */
    ana_and(ANA_REG0, ~(1u << 26));                   /* dsptrig = 0 */

    /* (3) ANA_REG2/3: xtal tuning and bias. */

    ana_write(ANA_REG2, 0x7E003450u);                 /* xtal ctune=0x50 */
    ana_write(ANA_REG3, 0xC5F00B88u);

    /* (4) Latched analog block: spi_latch1v on, write REG8/9/10-13/25
     *     (default branch for chip_id MP_C), then latch off.
     *     ANA_REG3.inbufen0v9 (bit6) is set inside the latch window. */

    ana_or(ANA_REG9, (1u << 9));                      /* spi_latch1v = 1 */
    ana_write(ANA_REG8,  0x57E62F26u);
    ana_write(ANA_REG9,  0x787BC8A4u);
    ana_write(ANA_REG10, 0xC3D543A7u);
    ana_write(ANA_REG11, 0xB47E99F8u);
    ana_write(ANA_REG12, 0xB47ECF20u);
    ana_write(ANA_REG13, 0x727070EEu);
    ana_write(ANA_REG25, 0x0961FAA4u);
    ana_or(ANA_REG3, (1u << 6));                      /* inbufen0v9 = 1 */
    ana_and(ANA_REG9, ~(1u << 9));                    /* spi_latch1v = 0 */

    /* (5) DPLL SPI recalibration (the SDK runs this unconditionally). */

    clk_delay(120);
    boot_cali_dpll();

    /* (6) Evidence: confirm EN_DPLL landed. */

    clk_puts("BClk A5=");
    clk_puthex(REG32(ANA_REG5));
    clk_puts(" A9=");
    clk_puthex(REG32(ANA_REG9));
    clk_puts("\r\n");
}