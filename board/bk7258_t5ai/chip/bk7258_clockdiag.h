/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_clockdiag.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI) Stage N4-D0 read-only clock diagnostic.
 *
 * Header-only static inline helpers that READ (never write) the clock/DPLL/
 * mux/voltage/UART/SysTick registers and push a compact raw + decode trace
 * out over UART1 using the same freestanding polled MMIO putc the N2 boot
 * trace uses (poll fifo_status.bit20, write fifo_port).  No printf, no
 * clock/DPLL/flash/voltage/UART-divisor writes -- strictly getreg32 + the
 * diagnostic putc.
 *
 * Register addresses and field bit layouts are taken verbatim from the
 * Armino BK7258 SDK headers, and inlined here as plain constants so the
 * overlay stays self-contained (no SDK macros are imported):
 *
 *   SOC_SYS_REG_BASE / SOC_AON_PMU_REG_BASE / SOC_UART1_REG_BASE
 *                                        -> cp/include/soc/bk7258/reg_base.h
 *   SYS_CPU_CLK_DIV_MODE1/2, SYS_ANA_REG0/1/5/8/9/10/11/12/13,
 *   SYS_CPU0_INT_HALT_CLK_OP,
 *   SYS_CPU0_INT_32_63_STATUS_*         -> soc/bk7258/soc/sys_reg.h
 *   AON_PMU_R7D                         -> soc/bk7258/soc/aon_pmu_reg.h
 *   UART config.clk_div bits[8:23]      -> soc/bk7258/soc/uart_struct.h
 *   SysTick CSR/RVR/CVR                 -> standard ARMv7-M/ARMv8-M
 *                                        (0xe000e010/14/18, also exposed by
 *                                         nuttx arch/arm/src/arm_m/nvic.h)
 *
 * Contract: the including translation unit MUST provide getreg32() (i.e. it
 * must include "arm_internal.h" before this header).  Both call sites
 * (bk7258_start.c, bk7258_timerisr.c) satisfy this.
 *
 * Output format (compact, CR/LF terminated):
 *   N4D0:E
 *   M1=<8> M2=<8> A0=<8> A1=<8> A5=<8> A9=<8> IS=<8> R7=<8> UC=<8> C0=<8>
 *   A8=<8> AA=<8> AB=<8> AC=<8> AD=<8>
 *   csrc=<1> cdiv=<1> bdiv=<1> usrc=<1> udiv=<1> fsrc=<1> fdiv=<1> \
 * dplle=<1> unlk=<1> vcre=<1> c0spd=<1>
 *   N4D0:T
 *   CSR=<8> RVR=<8> CVR=<8> EXP=<8>
 *
 * The two raw-hex lines are the primary evidence: line 1 covers the clock/
 * mux/voltage/UART registers; line 2 extends the analog readback with
 * ANA_REG8/10/11/12/13 (the remaining words of the vendor early_init
 * analog batch) so the boot state can be diffed against that batch.  Tags
 * on line 2 use hex index suffixes (AA=ANA_REG10, AB=11, AC=12, AD=13).
 * The short decode line extracts only the clock-source/divider/DPLL/vcore
 * fields needed to interpret line 1.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCKDIAG_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCKDIAG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Peripheral base addresses (Armino cp/include/soc/bk7258/reg_base.h). */

#define BK7258_CDIAG_SYS_BASE        0x44010000u   /* SOC_SYS_REG_BASE     */
#define BK7258_CDIAG_AON_PMU_BASE    0x44000000u   /* SOC_AON_PMU_REG_BASE */
#define BK7258_CDIAG_UART1_BASE      0x45830000u   /* SOC_UART1_REG_BASE   */

/* SYS register offsets (Armino soc/bk7258/soc/sys_reg.h, ADDR == base +
 * (index << 2)).  Raw values only; never written by this header.
 */

/* Per-core CPU0 clock-operating register (SYS_CPU0_INT_HALT_CLK_OP).  Note:
 * the BK7258 has NO per-core clock *divider* field -- the core divider is
 * the shared global CLKDIV_CORE in CPU_CLK_DIV_MODE1 (dumped as "cdiv"
 * below and applying to CPU0/CPU1/CPU2 alike).  The only per-core clock
 * field is CPU0_SPEED [4] here (1-bit speed select); CPU1/CPU2 mirror this
 * at indexes 0x05/0x06.  Read-only.
 */

#define BK7258_CDIAG_SYS_CPU0_HALT_CLK_OP \
        (BK7258_CDIAG_SYS_BASE + (0x04u << 2))            /* 0x44010010 */
#define BK7258_CDIAG_SYS_CPU_CLK_DIV_MODE1 \
        (BK7258_CDIAG_SYS_BASE + (0x08u << 2))            /* 0x44010020 */
#define BK7258_CDIAG_SYS_CPU_CLK_DIV_MODE2 \
        (BK7258_CDIAG_SYS_BASE + (0x09u << 2))            /* 0x44010024 */
#define BK7258_CDIAG_SYS_CPU0_INT_32_63_STAT \
        (BK7258_CDIAG_SYS_BASE + (0x29u << 2))            /* 0x440100A4 */
#define BK7258_CDIAG_SYS_ANA_REG0 \
        (BK7258_CDIAG_SYS_BASE + (0x40u << 2))            /* 0x44010100 */
#define BK7258_CDIAG_SYS_ANA_REG1 \
        (BK7258_CDIAG_SYS_BASE + (0x41u << 2))            /* 0x44010104 */
#define BK7258_CDIAG_SYS_ANA_REG5 \
        (BK7258_CDIAG_SYS_BASE + (0x45u << 2))            /* 0x44010114 */
#define BK7258_CDIAG_SYS_ANA_REG8 \
        (BK7258_CDIAG_SYS_BASE + (0x48u << 2))            /* 0x44010120 */
#define BK7258_CDIAG_SYS_ANA_REG9 \
        (BK7258_CDIAG_SYS_BASE + (0x49u << 2))            /* 0x44010124 */
#define BK7258_CDIAG_SYS_ANA_REG10 \
        (BK7258_CDIAG_SYS_BASE + (0x4au << 2))            /* 0x44010128 */
#define BK7258_CDIAG_SYS_ANA_REG11 \
        (BK7258_CDIAG_SYS_BASE + (0x4bu << 2))            /* 0x4401012C */
#define BK7258_CDIAG_SYS_ANA_REG12 \
        (BK7258_CDIAG_SYS_BASE + (0x4cu << 2))            /* 0x44010130 */
#define BK7258_CDIAG_SYS_ANA_REG13 \
        (BK7258_CDIAG_SYS_BASE + (0x4du << 2))            /* 0x44010134 */

/* AON_PMU register offset (Armino soc/bk7258/soc/aon_pmu_reg.h). */

#define BK7258_CDIAG_AON_PMU_R7D \
        (BK7258_CDIAG_AON_PMU_BASE + (0x7du << 2))        /* 0x440001F4 */

/* UART1 config register holding clk_div (Armino uart_struct.h: config is
 * REG_0x04 -> base + 0x10, clk_div bits[8:23]).  Read-only here -- this is
 * the register the Tier-1 bootloader programmed; the N2 console contract
 * forbids writing it.
 */

#define BK7258_CDIAG_UART1_CFG_REG    (BK7258_CDIAG_UART1_BASE + 0x10u) /* 0x45830010 */

/* UART1 FIFO status/port for the freestanding polled putc (identical MMIO
 * offsets to bk7258_start.c / bk7258_timerisr.c / bk7258_vectors.c).
 */

#define BK7258_CDIAG_UART1_FSTAT \
        (*(volatile uint32_t *)(BK7258_CDIAG_UART1_BASE + 0x18u)) /* 0x45830018 */
#define BK7258_CDIAG_UART1_FPORT \
        (*(volatile uint32_t *)(BK7258_CDIAG_UART1_BASE + 0x1cu)) /* 0x4583001C */
#define BK7258_CDIAG_UART1_READY      (1u << 20)  /* fifo_status.bit20 fifo_wr_ready */

/* SysTick CSR/RVR/CVR -- standard ARMv7-M/ARMv8-M addresses (identical to
 * nuttx NVIC_SYSTICK_CTRL/RELOAD/CURRENT); inlined so this header does not
 * require nvic.h in every including translation unit.
 */

#define BK7258_CDIAG_SYSTICK_CSR      0xe000e010u
#define BK7258_CDIAG_SYSTICK_RVR      0xe000e014u
#define BK7258_CDIAG_SYSTICK_CVR      0xe000e018u

/* Field bit definitions for the compact decode line (source: Armino
 * soc/bk7258/soc/sys_reg.h + uart_struct.h).
 *
 *   CPU_CLK_DIV_MODE1:
 *     CLKDIV_CORE  [0:3]   core divider (shared by all CPUs -- this IS the
 *                          CPU0 core divider; no per-core divider exists)
 *     CKSEL_CORE   [4:5]   core source select
 *     CLKDIV_BUS   [6]     bus divider
 *     CLKDIV_UART1 [11:12] uart1 divider field
 *     CKSEL_UART1  [13]    uart1 source select
 *   CPU0_INT_HALT_CLK_OP:
 *     CPU0_SPEED   [4]     per-core CPU0 speed select (clock-op bit)
 *   CPU_CLK_DIV_MODE2:
 *     CKSEL_FLASH  [24:25] flash source select
 *     CKDIV_FLASH  [26:27] flash divider
 *   ANA_REG5:
 *     EN_DPLL      [5]     DPLL enable
 *   CPU0_INT_32_63_STATUS:
 *     DPLL_UNLOCK_INT_ST [19]  DPLL unlock status (sticky live read)
 *   ANA_REG9:
 *     VCOREHSEL    [16:19] core voltage select
 *   UART1 config:
 *     clk_div      [8:23]  uart clock divider (uart_clk / baud)
 */

#define BK7258_CDIAG_F_CKSEL_CORE(v)   (((v) >> 4)  & 0x3u)
#define BK7258_CDIAG_F_CLKDIV_CORE(v)  (((v) >> 0)  & 0xfu)
#define BK7258_CDIAG_F_CLKDIV_BUS(v)   (((v) >> 6)  & 0x1u)
#define BK7258_CDIAG_F_CLKDIV_UART1(v) (((v) >> 11) & 0x3u)
#define BK7258_CDIAG_F_CKSEL_UART1(v)  (((v) >> 13) & 0x1u)
#define BK7258_CDIAG_F_CKSEL_FLASH(v)  (((v) >> 24) & 0x3u)
#define BK7258_CDIAG_F_CKDIV_FLASH(v)  (((v) >> 26) & 0x3u)
#define BK7258_CDIAG_F_EN_DPLL(v)      (((v) >> 5)  & 0x1u)
#define BK7258_CDIAG_F_DPLL_UNLOCK(v)  (((v) >> 19) & 0x1u)
#define BK7258_CDIAG_F_VCOREHSEL(v)    (((v) >> 16) & 0xfu)
#define BK7258_CDIAG_F_UART_CLKDIV(v)  (((v) >> 8)  & 0xffffu)
#define BK7258_CDIAG_F_CPU0_SPEED(v)   (((v) >> 4)  & 0x1u)

/****************************************************************************
 * Private Functions (static inline)
 ****************************************************************************/

/* Freestanding single-byte polled UART1 output -- MMIO only, no .data/.bss
 * dependency.  Identical behaviour to bk7258_early_putc() in bk7258_start.c,
 * bk7258_timer_diag_putc() in bk7258_timerisr.c and bk7258_fault_putc() in
 * bk7258_vectors.c; distinct name so it can live in this header without
 * colliding with the file-local copies.
 */

static inline void bk7258_clockdiag_putc(unsigned char c)
{
  while ((BK7258_CDIAG_UART1_FSTAT & BK7258_CDIAG_UART1_READY) == 0)
    {
    }

  BK7258_CDIAG_UART1_FPORT = (uint32_t)(c & 0xffu);
}

static inline void bk7258_clockdiag_puts(const char *s)
{
  while (*s)
    {
      bk7258_clockdiag_putc((unsigned char)*s);
      s++;
    }
}

/* Emit one hex nibble (0-9, a-f).  Computed rather than table-driven so the
 * path stays freestanding and needs no rodata lookup.
 */

static inline void bk7258_clockdiag_putnibble(unsigned int n)
{
  bk7258_clockdiag_putc((unsigned char)(n < 10u ? ('0' + (char)n)
                                                : ('a' + (char)(n - 10u))));
}

/* Emit `width` hex nibbles, most-significant first (width <= 8). */

static inline void bk7258_clockdiag_puthex(uint32_t v, int width)
{
  int i;

  for (i = width - 1; i >= 0; i--)
    {
      bk7258_clockdiag_putnibble((unsigned int)((v >> (i * 4)) & 0xfu));
    }
}

/* Emit "TAG=" + 8-nibble raw hex. */

static inline void bk7258_clockdiag_putreg(const char *tag, uint32_t v)
{
  bk7258_clockdiag_puts(tag);
  bk7258_clockdiag_putc('=');
  bk7258_clockdiag_puthex(v, 8);
}

/* Emit "label=<width nibbles>" for a single decoded field. */

static inline void bk7258_clockdiag_putfield(const char *label,
                                             uint32_t v, int width)
{
  bk7258_clockdiag_puts(label);
  bk7258_clockdiag_putc('=');
  bk7258_clockdiag_puthex(v, width);
}

/****************************************************************************
 * Public Functions (static inline)
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_clockdiag_early_dump
 *
 * Description:
 *   Read-only snapshot of the clock / DPLL / mux / voltage / UART1
 *   configuration as the Tier-1 bootloader left it.  Intended to be called
 *   exactly once from __start() in bk7258_start.c AFTER arm_earlyserialinit()
 *   and BEFORE nx_start(), so the boot-trace marker stream and NuttX boot
 *   order are unchanged.  No target register is written; only getreg32 +
 *   diagnostic putc.
 *
 ****************************************************************************/

static inline void bk7258_clockdiag_early_dump(void)
{
  uint32_t m1  = getreg32(BK7258_CDIAG_SYS_CPU_CLK_DIV_MODE1);
  uint32_t m2  = getreg32(BK7258_CDIAG_SYS_CPU_CLK_DIV_MODE2);
  uint32_t c0  = getreg32(BK7258_CDIAG_SYS_CPU0_HALT_CLK_OP);
  uint32_t a0  = getreg32(BK7258_CDIAG_SYS_ANA_REG0);
  uint32_t a1  = getreg32(BK7258_CDIAG_SYS_ANA_REG1);
  uint32_t a5  = getreg32(BK7258_CDIAG_SYS_ANA_REG5);
  uint32_t a8  = getreg32(BK7258_CDIAG_SYS_ANA_REG8);
  uint32_t a9  = getreg32(BK7258_CDIAG_SYS_ANA_REG9);
  uint32_t a10 = getreg32(BK7258_CDIAG_SYS_ANA_REG10);
  uint32_t a11 = getreg32(BK7258_CDIAG_SYS_ANA_REG11);
  uint32_t a12 = getreg32(BK7258_CDIAG_SYS_ANA_REG12);
  uint32_t a13 = getreg32(BK7258_CDIAG_SYS_ANA_REG13);
  uint32_t isr = getreg32(BK7258_CDIAG_SYS_CPU0_INT_32_63_STAT);
  uint32_t r7  = getreg32(BK7258_CDIAG_AON_PMU_R7D);
  uint32_t uc  = getreg32(BK7258_CDIAG_UART1_CFG_REG);

  bk7258_clockdiag_puts("N4D0:E\r\n");

  /* Raw-hex evidence line: every register the decode below references. */

  bk7258_clockdiag_putreg("M1", m1);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("M2", m2);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("A0", a0);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("A1", a1);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("A5", a5);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("A9", a9);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("IS", isr);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("R7", r7);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("UC", uc);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("C0", c0);
  bk7258_clockdiag_puts("\r\n");

  /* Extended analog batch raw line: ANA_REG8/10/11/12/13 -- the remaining
   * words the vendor early_init analog batch programs.  Emitted as raw hex
   * only (these are LDO/voltage/osc-cal registers, not clock-routing), so
   * the boot state can be diffed word-by-word against that batch.
   */

  bk7258_clockdiag_putreg("A8", a8);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("AA", a10);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("AB", a11);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("AC", a12);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("AD", a13);
  bk7258_clockdiag_puts("\r\n");

  /* Compact decode of the clock-critical fields. */

  bk7258_clockdiag_putfield("csrc",  BK7258_CDIAG_F_CKSEL_CORE(m1),   1);
  bk7258_clockdiag_putfield(" cdiv", BK7258_CDIAG_F_CLKDIV_CORE(m1),  1);
  bk7258_clockdiag_putfield(" bdiv", BK7258_CDIAG_F_CLKDIV_BUS(m1),   1);
  bk7258_clockdiag_putfield(" usrc", BK7258_CDIAG_F_CKSEL_UART1(m1),  1);
  bk7258_clockdiag_putfield(" udiv", BK7258_CDIAG_F_CLKDIV_UART1(m1), 1);
  bk7258_clockdiag_putfield(" fsrc", BK7258_CDIAG_F_CKSEL_FLASH(m2),  1);
  bk7258_clockdiag_putfield(" fdiv", BK7258_CDIAG_F_CKDIV_FLASH(m2),  1);
  bk7258_clockdiag_putfield(" dplle",BK7258_CDIAG_F_EN_DPLL(a5),      1);
  bk7258_clockdiag_putfield(" unlk", BK7258_CDIAG_F_DPLL_UNLOCK(isr), 1);
  bk7258_clockdiag_putfield(" vcre", BK7258_CDIAG_F_VCOREHSEL(a9),    1);
  bk7258_clockdiag_putfield(" c0spd",BK7258_CDIAG_F_CPU0_SPEED(c0),   1);
  bk7258_clockdiag_puts("\r\n");
}

/* Runtime CPU-frequency case identifiers used by
 * bk7258_clockdiag_last_clock_case() and bk7258_clockdiag_current_cpu_hz().
 * The case is decided from a read-only snapshot of CPU_CLK_DIV_MODE1 (M1)
 * and ANA_REG5 (A5) -- never from a write.
 */

#define BK7258_CDIAG_CASE_BASELINE   0   /* M1=0 dplle=0: 26 MHz XTAL.      */
#define BK7258_CDIAG_CASE_LOADER80   1   /* M1=0x423 csrc=2 cdiv=3 dplle=1:
                                         * loader --reboot 1 residue, ~80 MHz
                                         */
#define BK7258_CDIAG_CASE_UNKNOWN    2   /* fallback to baseline hz.        */

/****************************************************************************
 * Name: bk7258_clockdiag_last_clock_case
 *
 * Description:
 *   Classify the live core-clock configuration from a read-only snapshot of
 *   CPU_CLK_DIV_MODE1 (M1) and ANA_REG5 (A5).  No register is written.
 *   Recognised cases:
 *
 *     BK7258_CDIAG_CASE_BASELINE  Manual-reset BootROM default: M1 = 0 with
 *                                 DPLL disabled -- 26 MHz XTAL.
 *     BK7258_CDIAG_CASE_LOADER80  Tier-1 loader `--reboot 1` residue:
 *                                 M1 = 0x00000423 with csrc = 2, cdiv = 3
 *                                 and DPLL enabled -- observed f_cpu ~80.04
 *                                 MHz (J-Link DWT, 2 s = 0x098AA02F ticks).
 *     BK7258_CDIAG_CASE_UNKNOWN   Any other combination; callers fall back
 *                                 to the baseline frequency.
 *
 ****************************************************************************/

static inline int bk7258_clockdiag_last_clock_case(void)
{
  uint32_t m1    = getreg32(BK7258_CDIAG_SYS_CPU_CLK_DIV_MODE1);
  uint32_t a5    = getreg32(BK7258_CDIAG_SYS_ANA_REG5);
  uint32_t csrc  = BK7258_CDIAG_F_CKSEL_CORE(m1);
  uint32_t cdiv  = BK7258_CDIAG_F_CLKDIV_CORE(m1);
  uint32_t dplle = BK7258_CDIAG_F_EN_DPLL(a5);

  if (m1 == 0x00000000u && dplle == 0u)
    {
      return BK7258_CDIAG_CASE_BASELINE;
    }

  if (m1 == 0x00000423u && csrc == 2u && cdiv == 3u && dplle == 1u)
    {
      return BK7258_CDIAG_CASE_LOADER80;
    }

  return BK7258_CDIAG_CASE_UNKNOWN;
}

/****************************************************************************
 * Name: bk7258_clockdiag_current_cpu_hz
 *
 * Description:
 *   Return the runtime SysTick processor-clock frequency in Hz inferred
 *   from the live M1 and A5 registers.  Maps each case from
 *   bk7258_clockdiag_last_clock_case() to its measured frequency, and falls
 *   back to the 26 MHz baseline for the unknown case.  Read-only; no
 *   clock-control register is written.
 *
 *   The returned value is the SysTick processor clock (CLKSOURCE = processor
 *   clock, no /8 divisor).  BOARD_CPU_FREQ_HZ in board.h remains the
 *   build-time baseline and matches the BK7258_CDIAG_CASE_BASELINE return.
 *
 ****************************************************************************/

static inline uint32_t bk7258_clockdiag_current_cpu_hz(void)
{
  switch (bk7258_clockdiag_last_clock_case())
    {
    case BK7258_CDIAG_CASE_LOADER80:
      return 80000000u;

    case BK7258_CDIAG_CASE_BASELINE:
    case BK7258_CDIAG_CASE_UNKNOWN:
    default:
      return 26000000u;
    }
}

/****************************************************************************
 * Name: bk7258_clockdiag_systick_dump
 *
 * Description:
 *   Read-only snapshot of the Cortex-M SysTick CSR/RVR/CVR.  Intended to be
 *   called exactly once from up_timer_initialize() in bk7258_timerisr.c
 *   AFTER the reload write and systick_initialize() have programmed the
 *   SysTick, so the read-back captures the live armed state.
 *
 *   `expected_reload` is the value the caller computed at runtime (from
 *   bk7258_clockdiag_current_cpu_hz() and CLK_TCK) and wrote into RVR; the
 *   dump emits it as EXP so the read-back RVR can be compared against the
 *   design reload.  `cpu_hz` is the runtime frequency the caller used; the
 *   dump emits it as HZ plus the matched clock-case tag (CLK=x) so board-
 *   side observation can distinguish baseline / loader-residue / unknown
 *   without parsing the full early dump.  No SysTick register is written by
 *   this call -- the existing reload write in up_timer_initialize() is the
 *   only write, and is not a new clock write.
 *
 ****************************************************************************/

static inline void bk7258_clockdiag_systick_dump(uint32_t expected_reload,
                                                 uint32_t cpu_hz)
{
  uint32_t csr = getreg32(BK7258_CDIAG_SYSTICK_CSR);
  uint32_t rvr = getreg32(BK7258_CDIAG_SYSTICK_RVR);
  uint32_t cvr = getreg32(BK7258_CDIAG_SYSTICK_CVR);
  int clk_case  = bk7258_clockdiag_last_clock_case();

  bk7258_clockdiag_puts("N4D0:T\r\n");

  bk7258_clockdiag_putreg("CSR", csr);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("RVR", rvr);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("CVR", cvr);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("EXP", expected_reload);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putreg("HZ", cpu_hz);
  bk7258_clockdiag_putc(' ');
  bk7258_clockdiag_putfield("CLK", (uint32_t)clk_case, 1);
  bk7258_clockdiag_puts("\r\n");
}

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCKDIAG_H */
