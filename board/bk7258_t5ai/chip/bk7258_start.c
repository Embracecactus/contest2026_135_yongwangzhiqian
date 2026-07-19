/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_start.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, tri-core Cortex-M33) C reset entry for NuttX.
 *
 * Standard Cortex-M __start sequence (modelled on
 * nuttx/arch/arm/src/mps/mps_start.c):
 *
 *     cpsid i
 *     VTOR <- 0x02010000  (our flash-resident vector table)
 *     CPACR/FPCCR FPU setup (CP10/CP11 full access, no lazy/auto stacking)
 *     .data  copy  _eronly -> _sdata.._edata
 *     .bss   zero  _sbss.._ebss
 *     arm_earlyserialinit()   (bring up the polled console early)
 *     nx_start()              (kernel: scheduler, SysTick, init/NSH)
 *
 * Memory map (shared verbatim with docs/bk7258-t5ai/probe/probe.c):
 *   FLASH/logical app base : 0x02010000  (vector table, .text, .data LMA)
 *   RAM                     : 0x28000000 .. 0x2809FFFF (640 KiB SRAM)
 *   initial MSP (slot [0])  : 0x2809FFFC
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

#ifndef CONFIG_BUILD_PIC
  /* 4. Copy the .data image from flash (LMA == _eronly) to its RAM VMA
   *    (_sdata.._edata).  The BK7258 boots with a copy of NuttX kernel +
   *    NSH, so .data is non-empty and this copy is mandatory.
   */

  for (src = (const uint32_t *)_eronly,
       dest = (uint32_t *)_sdata; dest < (uint32_t *)_edata; )
    {
      *dest++ = *src++;
    }

  /* 5. Zero the .bss section (_sbss.._ebss). */

#ifndef CONFIG_ARCH_SKIP_ZERO_BSS
  for (dest = (uint32_t *)_sbss; dest < (uint32_t *)_ebss; )
    {
      *dest++ = 0;
    }
#endif
#endif /* CONFIG_BUILD_PIC */

  /* 6. Perform early serial initialisation so the console is available
   *    during the rest of boot.  arm_earlyserialinit() is only compiled
   *    when USE_EARLYSERIALINIT is derived (CONFIG_DEV_CONSOLE + a serial
   *    console), matching mps_start.c.
   */

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

  /* 7. Start NuttX.  nx_start() never returns; it brings up the scheduler,
   *    SysTick (via up_timer_initialize), the init task (which runs
   *    board_app_initialize and spawns the NSH builtin), and finally the
   *    IDLE task.
   */

  nx_start();

  /* Shouldn't get here. */

  for (; ; )
    {
    }
}
