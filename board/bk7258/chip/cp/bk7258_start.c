/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_start.c
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
 *     stop the bootloader AON + APB watchdogs
 *     CPACR/FPCCR FPU setup (CP10/CP11 full access, no lazy/auto stacking)
 *     .data  copy  _eronly -> _sdata.._edata
 *     .bss   zero  _sbss.._ebss
 *     low-power hardware init (SDK leaf; NuttX still owns PM policy)
 *     arm_earlyserialinit()   (bring up the polled console early)
 *     nx_start()              (kernel: scheduler, SysTick, init/NSH)
 *
 * Memory map (shared verbatim with docs/bk7258-t5ai/probe/probe.c):
 *   FLASH/logical app base : 0x02010000  (vector table, .text, .data LMA)
 *   AP SMP spinlocks        : 0x28000000 .. 0x2800FFFF (reserved)
 *   CP RAM                  : 0x28010000 .. 0x2804FFFF (256 KiB SRAM)
 *   reset/IDLE stack        : _ebss + CONFIG_IDLETHREAD_STACKSIZE (PSP)
 *   interrupt stack top     : 0x28010800 (MSP)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/init.h>

#include <arch/chip/bk7258_amp.h>

#ifdef CONFIG_BK7258_SWD_DEBUG
#  include <arch/chip/bk7258_debug.h>
#endif
#ifdef CONFIG_BK7258_CONSOLE_RTT
#  include <SEGGER_RTT.h>
#endif

#include "arm_internal.h"

#ifdef CONFIG_BK7258_CLOCK_320M
#include "bk7258_clock.h"
#endif

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
#include "bk7258_pm_coord.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SCB registers. */

#define BK7258_SCB_VTOR          (*(volatile unsigned int *)0xe000ed08u)
#define BK7258_SCB_CPACR         (*(volatile unsigned int *)0xe000ed88u)

/* The Tier-1 bootloader arms both watchdogs while it validates flash and the
 * cold-start clocks.  The application must close both with the BK7258
 * two-key sequence before entering nx_start(): AP autostart performs bounded
 * SMP gates whose aggregate window is intentionally longer than the
 * bootloader's eight-second watchdog period.  board_app_initialize() later
 * registers the NuttX watchdog only after that bounded AP startup returns.
 */

#define BK7258_AON_WDT_CTRL      (*(volatile unsigned int *)0x44000600u)
#define BK7258_APB_WDT_GLOBAL    (*(volatile unsigned int *)0x44800008u)
#define BK7258_APB_WDT_CTRL      (*(volatile unsigned int *)0x44800010u)
#define BK7258_AON_WDT_KEY1      (0x5au << 16)
#define BK7258_AON_WDT_KEY2      (0xa5u << 16)
#define BK7258_APB_WDT_KEY1      (0x5au << 16)
#define BK7258_APB_WDT_KEY2      (0xa5u << 16)

/* The direct BL1 image starts at CP flash base.  A MCUboot payload reserves
 * its standard header before the vector table, and the linker uses the same
 * offset.  Tell VTOR to fetch exceptions from the actual vector location.
 */

#ifdef CONFIG_BK7258_BL2_IMAGE
#  define BK7258_VTOR_VALUE      BK7258_BL2_EXEC_RAM_BASE
#elif defined(CONFIG_BK7258_MCUBOOT_IMAGE)
#  define BK7258_VTOR_VALUE      (BK7258_CP_FLASH_ADDR + 0x200u)
#else
#  define BK7258_VTOR_VALUE      BK7258_CP_FLASH_ADDR
#endif

/* Heap base convention shared with mps_start.c / bk7258_allocateheap.c:
 * the IDLE thread stack sits at the top of .bss and is CONFIG_IDLETHREAD_
 * STACKSIZE bytes; the heap begins right above it.  g_idle_topstack records
 * that address for the common ARM code (up_get_idle_stack / up_allocate_heap).
 */

#define HEAP_BASE  ((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE)

/****************************************************************************
 * External Data
 ****************************************************************************/

extern uint32_t _eheap[];

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
 *   Cortex-M C entry.  Reached from the slot [1] reset wrapper after it
 *   preserves the slot [0] reset stack as PSP and installs the dedicated
 *   interrupt MSP.  The bootloader has already validated the BK7236 magic at
 *   image offset 0x100; the first C instruction masks interrupts.
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

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_begin();
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_CP_ENTRY);
#endif

  /* 2. Point VTOR at our flash-resident vector table.  The MCUboot image
   *    reserves a 0x200-byte, VTOR-alignment-sized header before that table.
   *    The bootloader may or may not have set this; make it deterministic.
   *    Barrier so subsequent exception entry observes the new VTOR.
   */

  BK7258_SCB_VTOR = BK7258_VTOR_VALUE;
  __asm volatile ("dsb; isb");

  /* 3. Stop both bootloader watchdogs immediately.  Preserve the APB WDT
   *    clock-gate bypass bit used by the official SDK close path, then apply
   *    period zero with the required two-key sequence.  A later HardFault now
   *    remains parked for inspection instead of resetting the whole SoC.
   */

  BK7258_AON_WDT_CTRL = BK7258_AON_WDT_KEY1;
  BK7258_AON_WDT_CTRL = BK7258_AON_WDT_KEY2;

  BK7258_APB_WDT_GLOBAL |= 1u << 1;
  BK7258_APB_WDT_CTRL = BK7258_APB_WDT_KEY1;
  BK7258_APB_WDT_CTRL = BK7258_APB_WDT_KEY2;
  __asm volatile ("dsb sy" ::: "memory");

  /* 4. FPU: clear FPCCR.ASPEN/LSPEN/LSPENS (disable lazy + automatic FP
   *    context stacking), then enable CP10/CP11.  The BootROM leaves lazy
   *    stacking enabled; with CPACR enabled, the first exception (SysTick)
   *    hung inside that protocol without raising HardFault.  This reset-state
   *    normalization cannot rely on arm_fpuconfig(): that helper is available
   *    only when CONFIG_ARCH_FPU is enabled.  Keep the sequence explicit and
   *    ordered per the ARMv8-M rule "do not change ASPEN/LSPEN while CPACR
   *    permits CP10/CP11": deny CP first, clear the bits, re-enable CP.
   *    (FPCCR @ 0xE000EF34; ASPEN=bit31, LSPEN=bit30, LSPENS=bit29.)
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

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_CP_CORE_READY);
#endif

#ifndef CONFIG_BUILD_PIC
  /* 5. Copy the .data image from flash (LMA == _eronly) to its RAM VMA
   *    (_sdata.._edata).  The BK7258 boots with a copy of NuttX kernel +
   *    NSH, so .data is non-empty and this copy is mandatory.
   */

  for (src = (const uint32_t *)_eronly,
       dest = (uint32_t *)_sdata; dest < (uint32_t *)_edata; )
    {
      *dest++ = *src++;
    }

  /* 6. Zero the .bss section (_sbss.._ebss). */

#ifndef CONFIG_ARCH_SKIP_ZERO_BSS
  for (dest = (uint32_t *)_sbss; dest < (uint32_t *)_ebss; )
    {
      *dest++ = 0;
    }
#endif

#ifdef CONFIG_BK7258_CONSOLE_RTT
  /* Initialize the RTT control block early, but defer the board-specific SWD
   * mux to board bring-up.  The SDK GPIO/sysctrl wrappers require initialized
   * HAL state, and the debug profile establishes the mux only through those
   * wrappers.
   */

  SEGGER_RTT_Init();
#endif

#ifdef CONFIG_BK7258_WIFI_VNET
  /* The immutable BK7258 v3.1.1.9 Wi-Fi library allocates its LMAC station
   * table with malloc() and expects the first-use heap contents to be zero.
   * The official bk7258_bsp.ld therefore includes the complete heap in its
   * startup zero table.  Reproduce that board-startup ABI before NuttX
   * initializes the allocator; _eheap is the CP-only 0x2804fffc boundary.
   */

  for (dest = (uint32_t *)HEAP_BASE; dest < _eheap; )
    {
      *dest++ = 0;
    }
#endif
#endif /* CONFIG_BUILD_PIC */

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  /* Match the mandatory CPU0 startup ordering used by both the official
   * v3.1.1.9 SDK and Tuya: initialize the low-power hardware after the C
   * runtime exists, but before the scheduler, platform services and AP cores
   * start.  The wrapper deliberately invokes only the SDK hardware leaf;
   * NuttX remains the owner of the PM state machine and policy.
   */

  bk7258_pm_coord_early_initialize();
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_CP_C_RUNTIME_READY);
#endif

  /* 7. Perform early serial initialisation so the console is available
   *    during the rest of boot.  arm_earlyserialinit() is only compiled
   *    when USE_EARLYSERIALINIT is derived (CONFIG_DEV_CONSOLE + a serial
   *    console), matching mps_start.c.
   */

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

#ifdef CONFIG_BK7258_CLOCK_320M
  /* Optional bring-up-only 320 MHz override.  The v3.1.1.9 normal startup
   * policy keeps PM_DEV_ID_DEFAULT at 120 MHz and lets modules vote upward;
   * production profiles should therefore leave this disabled.  When enabled
   * for a performance experiment, run after early serial init so any stall is
   * distinguishable on the console.  It still runs before nx_start() so the
   * initial DWT conversion observes the final live CPU mux; scheduler SysTick
   * remains on fixed 32 kHz.  UART consoles use the independent 26 MHz XTAL
   * source and RTT does not depend on the CPU clock divider.
   */

  bk7258_clock_bringup_320m();
#endif

  /* 8. Start NuttX.  nx_start() never returns; it brings up the scheduler,
   *    SysTick (via up_timer_initialize), the init task (which runs
   *    board_app_initialize and spawns the NSH builtin), and finally the
   *    IDLE task.
   */

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_CP_BEFORE_NX_START);
#endif

  nx_start();

  /* Shouldn't get here. */

  for (; ; )
    {
    }
}
