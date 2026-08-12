/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_idle.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 board-owned NuttX idle loop.
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>

#ifdef CONFIG_PM
#  include <nuttx/power/pm.h>
#endif

#include "arm_internal.h"
#include "nvic.h"

#ifdef CONFIG_BK7258_SWD_DEBUG
#  include <arch/chip/bk7258_debug.h>
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void bk7258_idle_wfi(void)
{
  /* Match the official v3.1.1.9 arch_sleep() primitive exactly at the
   * Cortex-M level.  SLEEPDEEP may have been left set by an earlier path and
   * must be cleared before ordinary WFI.  DSB completes outstanding memory
   * accesses before sleep; ISB refetches after an enabled wake interrupt.
   * No BK7258 clock or power register is changed here.
   */

  modifyreg32(NVIC_SYSCON, NVIC_SYSCON_SLEEPDEEP, 0);
  __asm volatile ("dsb sy; wfi; isb sy" ::: "memory");
}

#if defined(CONFIG_PM) && !defined(CONFIG_BK7258_CONSOLE_RTT)
#  ifdef CONFIG_SMP
static bool bk7258_pm_idle_handler(int cpu,
                                   enum pm_state_e cpu_state,
                                   enum pm_state_e system_state)
{
  bool first;

  (void)cpu_state;
  (void)system_state;

  /* pm_idle() enters with its cross-CPU lock held.  NuttX requires the lock
   * to be dropped only around WFI and reacquired immediately after wake. */

  pm_idle_unlock();
  bk7258_idle_wfi();
  first = pm_idle_lock(cpu);
  return first;
}
#  else
static void bk7258_pm_idle_handler(enum pm_state_e state)
{
  (void)state;
  bk7258_idle_wfi();
}
#  endif
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_idle(void)
{
#ifdef CONFIG_BK7258_CONSOLE_RTT
  /* RTT is a deliberate diagnostic build profile.  Keep CPU0 awake so
   * an unattached probe can enumerate the STAR debug port, and repair the
   * route if a late SDK/AP initialization path touched the shared SYS/GPIO
   * registers.  Ordinary builds continue through pm_idle() below and retain
   * the first-phase shallow WFI behavior.
   */

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_maintain();
#endif
  __asm volatile ("nop" ::: "memory");
#elif defined(CONFIG_SUPPRESS_INTERRUPTS) || \
      defined(CONFIG_SUPPRESS_TIMER_INTS)
  nxsched_process_timer();
#elif defined(CONFIG_PM)
  pm_idle(bk7258_pm_idle_handler);
#else
  bk7258_idle_wfi();
#endif
}
