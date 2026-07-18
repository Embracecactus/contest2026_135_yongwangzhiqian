/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_start.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, tri-core Cortex-M33) C reset entry for NuttX
 * Stage N2.
 *
 * GOAL (Stage N2):
 *   Walk the full NuttX boot so that nx_start() brings up the kernel, the
 *   SysTick-based system clock runs, UART1 becomes the polled console, and
 *   the NSH prompt is reached.  This file replaces the N1 stub (which only
 *   printed "NUTTX N1\r\n" and hung) with the standard Cortex-M __start
 *   sequence used by nuttx/arch/arm/src/mps/mps_start.c:
 *
 *     cpsid i
 *     VTOR <- 0x02010000  (our flash-resident vector table)
 *     CPACR <- CP10/CP11 full access (FPU; cheap, avoids FP traps)
 *     .data  copy  _eronly -> _sdata.._edata
 *     .bss   zero  _sbss.._ebss
 *     arm_earlyserialinit()   (bring up the polled console early)
 *     nx_start()              (kernel: scheduler, SysTick, init/NSH)
 *
 *   A single bare "N2\r\n" marker is pushed out over UART1 BEFORE
 *   arm_earlyserialinit(), using the same freestanding MMIO write the N1
 *   stub and the verified probe use, so that board-side observation can
 *   confirm __start was reached even if the later console bring-up fails.
 *
 * Memory map (shared verbatim with docs/bk7258-t5ai/probe/probe.c):
 *   FLASH/logical app base : 0x02010000  (vector table, .text, .data LMA)
 *   RAM                     : 0x28000000 .. 0x2809FFFF (640 KiB SRAM)
 *   initial MSP (slot [0])  : 0x2809FFFC
 *
 * Freestanding note: the early marker uses a local polled putc that touches
 * only MMIO (no .data/.bss), so it is safe to call before .data/.bss init.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/init.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SCB registers. */

#define BK7258_SCB_VTOR          (*(volatile unsigned int *)0xe000ed08u)
#define BK7258_SCB_CPACR         (*(volatile unsigned int *)0xe000ed88u)

/* UART1 FIFO registers.  The Tier-1 bootloader already configured UART1
 * (pinmux, 26 MHz XTAL, clock gate, global_ctrl, config).  We MUST NOT
 * touch UART1_CFG (0x45830010) because that would clash with the
 * bootloader's divider; we only push bytes into fifo_port, exactly like
 * the Zephyr soc_reset_hook and our probe.
 */

#define BK7258_UART1_FIFO_STAT   (*(volatile unsigned int *)0x45830018u)
#define BK7258_UART1_FIFO_PORT   (*(volatile unsigned int *)0x4583001Cu)
#define BK7258_UART1_FIFO_READY  (1u << 20)   /* fifo_status.bit20 = fifo_wr_ready */

/* Our vector table lives at the very start of the app image, which the
 * bootloader maps at logical flash address 0x02010000.  Tell VTOR to
 * fetch exceptions from there.
 */

#define BK7258_VTOR_VALUE        0x02010000u

/* Heap base convention shared with mps_start.c / bk7258_allocateheap.c:
 * the IDLE thread stack sits at the top of .bss and is CONFIG_IDLETHREAD_
 * STACKSIZE bytes; the heap begins right above it.  g_idle_topstack records
 * that address for the common ARM code (up_get_idle_stack / up_allocate_heap).
 */

#define HEAP_BASE  ((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE)

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Referenced by nuttx/arch/arm/src/common/arm_initialize.c via
 * up_get_idle_stack().  Const so it lands in .rodata (flash).
 */

const uintptr_t g_idle_topstack = HEAP_BASE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Bare polled UART1 output -- freestanding, MMIO-only, identical to probe.c.
 * Used only for the very early "N2" marker before arm_earlyserialinit().
 * After the console is up, all output goes through arm_lowputc()/the
 * registered /dev/console.
 */

static void bk7258_early_putc(unsigned char c)
{
  while ((BK7258_UART1_FIFO_STAT & BK7258_UART1_FIFO_READY) == 0)
    {
    }

  BK7258_UART1_FIFO_PORT = (unsigned int)(c & 0xffu);
}

static void bk7258_early_puts(const char *s)
{
  while (*s)
    {
      bk7258_early_putc((unsigned char)*s);
      s++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __start
 *
 * Description:
 *   Cortex-M reset entry.  Reached from vector slot [1] after the
 *   bootloader validated the BK7236 magic at image offset 0x100 and jumped
 *   in.  The hardware has already loaded MSP from slot [0]; we run with
 *   interrupts whatever the bootloader left them at -- first instruction
 *   masks them.
 *
 ****************************************************************************/

void __start(void)
{
#ifndef CONFIG_BUILD_PIC
  const uint32_t *src;
  uint32_t       *dest;
#endif

  /* 1. Mask all interrupts immediately. */

  __asm volatile ("cpsid i");

  /* 2. Point VTOR at our flash-resident vector table (0x02010000).  The
   *    bootloader may or may not have set this; make it deterministic.
   *    Barrier so subsequent exception entry observes the new VTOR.
   */

  BK7258_SCB_VTOR = BK7258_VTOR_VALUE;
  __asm volatile ("dsb; isb");

  /* 3. FPU: clear FPCCR.ASPEN/LSPEN (disable lazy + automatic FP context
   *    stacking), then enable CP10/CP11.  The BootROM leaves LSPEN set; with
   *    CPACR enabled and LSPEN still set, the first exception (SysTick) hung
   *    inside the lazy-stacking protocol with no HardFault.  We cannot just
   *    call arm_fpuconfig() -- with CONFIG_ARCH_FPU off (our case) it is a
   *    no-op #define in arm_internal.h and the real impl in arm_fpuconfig.c
   *    is compiled only under CONFIG_ARCH_FPU.  So inline the FPCCR clear,
   *    ordered per the ARMv8-M rule "do not change ASPEN/LSPEN while CPACR
   *    permits CP10/CP11": deny CP first, clear the bits, re-enable CP.
   *    (FPCCR @ 0xE000EF34; ASPEN=bit31, LSPEN=bit30.)
   */

  BK7258_SCB_CPACR &= ~((3u << 20) | (3u << 22));             /* deny CP10/CP11 */
  __asm volatile ("dsb; isb");
  /* Clear ASPEN(bit31) + LSPEN(bit30, NS) + LSPENS(bit29, Secure).  We run in
   * Secure state (the bootloader never drops to NS), so Secure lazy stacking
   * (LSPENS, bit29) is the one that engages on Secure exceptions -- clearing
   * only 30/31 was not enough.  All three off -> no lazy/auto FP stacking.  */
  *(volatile uint32_t *)0xE000EF34u &= ~((1u << 31) | (1u << 30) | (1u << 29));
  BK7258_SCB_CPACR |= ((3u << 20) | (3u << 22));             /* CP10/CP11 full access */
  __asm volatile ("dsb; isb");

  /* 4. Early proof-of-life marker.  Pushed before .data/.bss init because it
   *    touches only MMIO; lets the board-side operator confirm __start was
   *    reached even if console bring-up later hangs.
   */

  bk7258_early_puts("N2\r\n");

#ifndef CONFIG_BUILD_PIC
  /* 5. Copy the .data image from flash (LMA == _eronly) to its RAM VMA
   *    (_sdata.._edata).  The BK7258 boots with a copy of NuttX kernel +
   *    NSH, so .data is no longer empty (unlike N1) and this copy is
   *    mandatory.
   */

  for (src = (const uint32_t *)_eronly,
       dest = (uint32_t *)_sdata; dest < (uint32_t *)_edata; )
    {
      *dest++ = *src++;
    }

  bk7258_early_putc('D');

  /* 6. Zero the .bss section (_sbss.._ebss). */

#ifndef CONFIG_ARCH_SKIP_ZERO_BSS
  for (dest = (uint32_t *)_sbss; dest < (uint32_t *)_ebss; )
    {
      *dest++ = 0;
    }

  bk7258_early_putc('B');
#endif
#endif /* CONFIG_BUILD_PIC */

  /* 7. Perform early serial initialisation so the console is available
   *    during the rest of boot.  arm_earlyserialinit() is only compiled
   *    when USE_EARLYSERIALINIT is derived (CONFIG_DEV_CONSOLE + a serial
   *    console), matching mps_start.c.
   */

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
  bk7258_early_putc('E');
#endif

  /* 8. Start NuttX.  nx_start() never returns; it brings up the scheduler,
   *    SysTick (via up_timer_initialize), the init task (which runs
   *    board_app_initialize and spawns the NSH builtin), and finally the
   *    IDLE task.  Caches/MPU are intentionally left untouched here to keep
   *    the N2 boot minimal and MMIO-coherent; they can be added once the
   *    NSH prompt is observed on the board.
   */

  bk7258_early_putc('S');

  nx_start();

  /* Shouldn't get here. */

  for (; ; )
    {
    }
}
