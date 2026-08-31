/****************************************************************************
 * chips/bk7258/common/bk7258_idle.c
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

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
#  include "bk7258_pm_coord.h"
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
#  include <arch/chip/bk7258_debug.h>
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void bk7258_idle_wfi(void)
{
  irqstate_t flags;

  /* Match the official v3.1.1.9 arch_sleep() primitive exactly at the
   * Cortex-M level.  SLEEPDEEP may have been left set by an earlier path and
   * must be cleared before ordinary WFI.  DSB completes outstanding memory
   * accesses before sleep; ISB refetches after an enabled wake interrupt.
   * No BK7258 clock or power register is changed here.
   *
   * NuttX pm_idle() calls the board handler with BASEPRI raised.  STAR does
   * not leave WFI for a SysTick whose priority is masked by that BASEPRI, so
   * the clock timebase and every delayed work item would stop after the first
   * idle pass.  Preserve the caller's exact mask, briefly admit interrupts
   * across shallow WFI, then restore the mask for pm_idle() to finish its
   * state transition.  Calls made outside pm_idle() enter and leave with the
   * original unmasked state as well.
   */

  flags = up_irq_save();
  modifyreg32(NVIC_SYSCON, NVIC_SYSCON_SLEEPDEEP, 0);
  up_irq_enable();
  __asm volatile ("dsb sy; wfi; isb sy" ::: "memory");
  up_irq_restore(flags);
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
#ifdef CONFIG_BK7258_AP_CORE
#  ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  bk7258_pm_ap_idle();
#  else
  bk7258_idle_wfi();
#  endif
#else
#  ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  if (system_state == PM_STANDBY)
    {
      if (!bk7258_pm_cp_standby())
        {
          bk7258_idle_wfi();
        }
    }
  else
#  endif
    {
      bk7258_idle_wfi();
    }
#endif
  first = pm_idle_lock(cpu);
  return first;
}
#  else
static void bk7258_pm_idle_handler(enum pm_state_e state)
{
#if defined(CONFIG_BK7258_PM_COORDINATED_STANDBY) && \
    !defined(CONFIG_BK7258_AP_CORE)
  if (state == PM_STANDBY)
    {
      if (!bk7258_pm_cp_standby())
        {
          bk7258_idle_wfi();
        }

      return;
    }
#else
  (void)state;
#endif

  bk7258_idle_wfi();
}
#  endif
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_idle(void)
{
#if defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_BK7258_PM_COORDINATED_STANDBY)
  /* AP startup runs the cross-CPU scheduler/IPI qualification and then lets
   * RPTUN finish its asynchronous Name Service exchange.  Keep both phases
   * on the previously verified raw WFI path: SMP pm_idle() itself emits
   * cross-calls while reconciling per-CPU domains, which can coalesce either
   * the qualification IPI or the one-deep mailbox edge that completes the
   * first RPMsg connection.  NuttX PM takes over once AP and RPTUN are ready.
   */

  if (!bk7258_pm_ap_runtime_ready())
    {
      bk7258_idle_wfi();

      /* The CP vote and AP0 forwarding IPI can arrive while AP1 is inside
       * this startup-safe WFI.  Consume that authorization in the same idle
       * invocation: AP1 has no local SysTick, so waiting for a later
       * up_idle() can exceed CP's bounded three-millisecond handshake.
       */

      if (!bk7258_pm_ap_runtime_ready())
        {
          return;
        }
    }
#endif

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
