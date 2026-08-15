/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_timerisr.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 Cortex-M33 SysTick system-timer init for NuttX.
 *
 * Modelled on nuttx/arch/arm/src/mps/mps_timer.c.  up_timer_initialize()
 * loads the SysTick reload value for the configured tick rate and registers
 * the common armv8-m SysTick lower half (arm_systick.c) as the system clock
 * source.
 *
 * BK7258 routes a fixed 32 kHz source to each physical core's external
 * SysTick input.  The official v3.1.1.9 AP and CP ports use this route so
 * scheduler time is independent of the shared CPU DVFS mux:
 *
 *   physical CPU0 / CP             0x44010040 bit 29
 *   physical CPU1 / AP primary     0x44010040 bit 30
 *
 * The AP secondary is physical CPU2, but NuttX owns one system timer on the
 * AP primary.  DWT performance conversion still follows the live CPU clock
 * and is refreshed after every frequency vote.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/arch_timer.h>
#include <nuttx/timers/timer.h>
#include <arch/barriers.h>
#include <arch/irq.h>

#include "arm_internal.h"
#include "systick.h"
#include "nvic.h"
#include "bk7258_clockdiag.h"
#include "bk7258_dvfs.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SYSTICK_FREQUENCY_HZ       32000u
#define BK7258_SYSTICK_TIMEOUT_US          USEC_PER_TICK
#define BK7258_SYSTICK_MIN_COUNTS          2u
#define BK7258_SYSTICK_MIN_TIMEOUT_US      \
  ((BK7258_SYSTICK_MIN_COUNTS * USEC_PER_SEC) / \
   BK7258_SYSTICK_FREQUENCY_HZ)
#define BK7258_SYS_CPU_POWER_SLEEP_WAKEUP 0x44010040u
#define BK7258_SYS_CPU0_TICKTIMER_32K     (1u << 29)
#define BK7258_SYS_CPU1_TICKTIMER_32K     (1u << 30)

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_SYSTICK_32K_ROUTE BK7258_SYS_CPU1_TICKTIMER_32K
#else
#  define BK7258_SYSTICK_32K_ROUTE BK7258_SYS_CPU0_TICKTIMER_32K
#endif

_Static_assert((BK7258_SYSTICK_FREQUENCY_HZ % CLK_TCK) == 0,
               "BK7258 32-kHz SysTick must divide evenly into CLK_TCK");

#if defined(CONFIG_BK7258_PM_COORDINATED_STANDBY) && \
    !defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SCHED_TICKLESS)
#  error BK7258 coordinated standby requires the periodic arch timer
#endif

#if defined(CONFIG_BK7258_PM_COORDINATED_STANDBY) && \
    !defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_ARM_SYSTICK_IRQ_WQUEUE)
#  error BK7258 standby compensation requires a hard-IRQ SysTick callback
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifdef CONFIG_TIMER_ARCH
struct bk7258_timer_proxy_s
{
  struct timer_lowerhalf_s lower;
  FAR struct timer_lowerhalf_s *hardware;
  CODE tccb_t callback;
  FAR void *arg;
  uint32_t phase_bias_us;
  uint32_t sleep_phase_us;
  uint32_t compensation_ticks;
  uint32_t pending_reload;
  uint32_t pending_phase_bias_us;
  bool phase_active;
  bool phase_boundary_latched;
  bool sleep_prepared;
  bool compensation_pending;
  bool compensation_in_progress;
};
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_TIMER_ARCH
static int bk7258_timer_start(FAR struct timer_lowerhalf_s *lower);
static int bk7258_timer_stop(FAR struct timer_lowerhalf_s *lower);
static int bk7258_timer_getstatus(FAR struct timer_lowerhalf_s *lower,
                                  FAR struct timer_status_s *status);
static int bk7258_timer_settimeout(FAR struct timer_lowerhalf_s *lower,
                                   uint32_t timeout);
static void bk7258_timer_setcallback(FAR struct timer_lowerhalf_s *lower,
                                     CODE tccb_t callback, FAR void *arg);
static int bk7258_timer_maxtimeout(FAR struct timer_lowerhalf_s *lower,
                                   FAR uint32_t *maxtimeout);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_TIMER_ARCH
static const struct timer_ops_s g_bk7258_timer_ops =
{
  .start       = bk7258_timer_start,
  .stop        = bk7258_timer_stop,
  .getstatus   = bk7258_timer_getstatus,
  .settimeout  = bk7258_timer_settimeout,
  .setcallback = bk7258_timer_setcallback,
  .maxtimeout  = bk7258_timer_maxtimeout,
};

static struct bk7258_timer_proxy_s g_bk7258_timer =
{
  .lower.ops = &g_bk7258_timer_ops,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifndef CONFIG_TIMER_ARCH
static int bk7258_timer_interrupt(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;
  (void)arg;

  nxsched_process_timer();
  return OK;
}
#else
static FAR struct bk7258_timer_proxy_s *
bk7258_timer_from_lower(FAR struct timer_lowerhalf_s *lower)
{
  return (FAR struct bk7258_timer_proxy_s *)lower;
}

static bool bk7258_timer_callback(FAR uint32_t *next_interval,
                                  FAR void *arg)
{
  FAR struct bk7258_timer_proxy_s *priv = arg;
  CODE tccb_t callback;
  FAR void *callback_arg;
  irqstate_t flags;
  uint32_t next_ticks;

  /* The hardware lower half reports microseconds, while arch_timer binds it
   * through TIMER_TICK_* and therefore owns callback intervals in scheduler
   * ticks.  Keep that conversion in the proxy instead of exposing either
   * private implementation to the board PM path.
   */

  flags = enter_critical_section();
  callback = priv->callback;
  callback_arg = priv->arg;
  if (callback == NULL)
    {
      leave_critical_section(flags);
      return false;
    }

  /* The PM path cannot call arch_timer's callback directly from idle task
   * context: watchdog expiry and scheduler timer processing require a real
   * interrupt context.  It therefore starts a long non-expiring interval and
   * pends SysTick.  Consume all elapsed logical ticks in this one genuine
   * exception, then atomically install the saved fractional first interval
   * before this callback returns.
   */

  if (priv->compensation_pending)
    {
      priv->compensation_pending = false;
      priv->compensation_in_progress = true;
      while (priv->compensation_ticks != 0)
        {
          /* Keep the proxy's synthetic elapsed view continuous if the arch
           * callback itself reads CLOCK_MONOTONIC: decrement the remaining
           * debt immediately before that callback advances g_timer.timebase.
           * The surrounding critical section closes the otherwise visible
           * one-tick gap between those two operations.
           */

          priv->compensation_ticks--;
          next_ticks = 1;
          if (!callback(&next_ticks, callback_arg) || next_ticks != 1)
            {
              PANIC();
            }
        }

      /* Rearm the saved short interval here, while the compensation state
       * and all higher-priority time readers are excluded.  Leaving this to
       * arm_systick after the proxy callback returns would expose a window
       * where phase_active describes the short interval but hardware still
       * runs the temporary 0x00ffffff reload.
       */

      if (priv->pending_reload == 0)
        {
          PANIC();
        }

      putreg32(priv->pending_reload, NVIC_SYSTICK_RELOAD);
      putreg32(0, NVIC_SYSTICK_CURRENT);
      UP_DSB();
      UP_ISB();
      priv->pending_reload = 0;
      priv->phase_bias_us = priv->pending_phase_bias_us;
      priv->phase_active = priv->phase_bias_us != 0;
      priv->pending_phase_bias_us = 0;
      priv->compensation_in_progress = false;
      leave_critical_section(flags);
      return true;
    }

  next_ticks = USEC2TICK(*next_interval);
  if (next_ticks == 0)
    {
      next_ticks = 1;
    }

  /* A shortened first interval represents the fractional tick that existed
   * before standby.  Once its boundary fires, arch_timer advances the full
   * logical tick and all status reads must return to the ordinary period.
   */

  if (priv->phase_active)
    {
      /* Make removal of the fractional bias and the matching arch-timer tick
       * indivisible to higher-priority time readers.  Start the canonical
       * next period before invoking the arch callback so a time read from
       * that callback observes the new base plus real handler elapsed time;
       * no later CVR clear can move that observation backwards.
       */

      putreg32((BK7258_SYSTICK_FREQUENCY_HZ / CLK_TCK) - 1,
               NVIC_SYSTICK_RELOAD);
      putreg32(0, NVIC_SYSTICK_CURRENT);
      UP_DSB();
      UP_ISB();
      priv->phase_active = false;
      priv->phase_bias_us = 0;
      priv->phase_boundary_latched = false;
      if (!callback(&next_ticks, callback_arg) || next_ticks != 1)
        {
          PANIC();
        }

      /* Keep next_interval unchanged so the underlying arm_systick ISR does
       * not perform a second, non-atomic reload/CVR update after this proxy
       * returns.
       */

      leave_critical_section(flags);
      return true;
    }

  leave_critical_section(flags);
  if (!callback(&next_ticks, callback_arg))
    {
      return false;
    }

  *next_interval = TICK2USEC(next_ticks);
  return true;
}

static int bk7258_timer_start(FAR struct timer_lowerhalf_s *lower)
{
  FAR struct bk7258_timer_proxy_s *priv = bk7258_timer_from_lower(lower);

  return TIMER_START(priv->hardware);
}

static int bk7258_timer_stop(FAR struct timer_lowerhalf_s *lower)
{
  FAR struct bk7258_timer_proxy_s *priv = bk7258_timer_from_lower(lower);

  return TIMER_STOP(priv->hardware);
}

static int bk7258_timer_getstatus(FAR struct timer_lowerhalf_s *lower,
                                  FAR struct timer_status_s *status)
{
  FAR struct bk7258_timer_proxy_s *priv = bk7258_timer_from_lower(lower);
  irqstate_t flags;
  uint64_t timeout_us;
  int ret;

  flags = enter_critical_section();
  ret = TIMER_GETSTATUS(priv->hardware, status);
  if (ret >= 0 && (priv->compensation_pending ||
                   priv->compensation_in_progress))
    {
      /* arch_timer adds (timeout - timeleft) to its private timebase.  Until
       * the pending hard-IRQ trampoline advances that timebase, expose the
       * still-unapplied whole ticks plus the restored fractional phase as a
       * synthetic elapsed interval.  compensation_ticks is decremented just
       * before each corresponding arch callback increments the timebase, so
       * the sum remains continuous throughout the ISR loop.
       */

      if (priv->pending_phase_bias_us >= BK7258_SYSTICK_TIMEOUT_US)
        {
          PANIC();
        }

      timeout_us = ((uint64_t)priv->compensation_ticks + 1u) *
                   BK7258_SYSTICK_TIMEOUT_US;
      if (timeout_us > UINT32_MAX)
        {
          PANIC();
        }

      status->flags |= TCFLAGS_ACTIVE | TCFLAGS_HANDLER;
      status->timeout = (uint32_t)timeout_us;
      status->timeleft = BK7258_SYSTICK_TIMEOUT_US -
                         priv->pending_phase_bias_us;
    }
  else if (ret >= 0 && priv->phase_active)
    {
      /* Once the shortened first period expires, PendST is only a saturating
       * one-bit witness.  arm_systick would otherwise expose progress into a
       * newly reloaded short period that this proxy intentionally discards
       * when it installs the canonical interval.  Clamp that pending/active
       * boundary to exactly one logical tick.  Once any reader observes that
       * boundary, latch it until the matching arch callback advances the
       * private timebase.  SYSTICKACT is also asserted near the end of the
       * compensation trampoline; treating that as an early boundary may move
       * time forward by less than one tick, but the latch prevents the value
       * from falling back to the shorter fractional interval after exception
       * return.
       */

      if (priv->phase_boundary_latched ||
          (getreg32(NVIC_INTCTRL) & NVIC_INTCTRL_PENDSTSET) != 0 ||
          (getreg32(NVIC_SYSHCON) & NVIC_SYSHCON_SYSTICKACT) != 0)
        {
          priv->phase_boundary_latched = true;
          status->flags |= TCFLAGS_ACTIVE | TCFLAGS_HANDLER;
          status->timeout = 2u * BK7258_SYSTICK_TIMEOUT_US;
          status->timeleft = BK7258_SYSTICK_TIMEOUT_US;
        }
      else if (status->timeout > UINT32_MAX - priv->phase_bias_us)
        {
          PANIC();
        }
      else
        {
          status->timeout += priv->phase_bias_us;
        }
    }

  leave_critical_section(flags);
  return ret;
}

static int bk7258_timer_settimeout(FAR struct timer_lowerhalf_s *lower,
                                   uint32_t timeout)
{
  FAR struct bk7258_timer_proxy_s *priv = bk7258_timer_from_lower(lower);

  return TIMER_SETTIMEOUT(priv->hardware, timeout);
}

static void bk7258_timer_setcallback(FAR struct timer_lowerhalf_s *lower,
                                     CODE tccb_t callback, FAR void *arg)
{
  FAR struct bk7258_timer_proxy_s *priv = bk7258_timer_from_lower(lower);
  irqstate_t flags;

  flags = enter_critical_section();
  priv->callback = callback;
  priv->arg = arg;
  TIMER_SETCALLBACK(priv->hardware,
                    callback == NULL ? NULL : bk7258_timer_callback,
                    priv);
  leave_critical_section(flags);
}

static int bk7258_timer_maxtimeout(FAR struct timer_lowerhalf_s *lower,
                                   FAR uint32_t *maxtimeout)
{
  FAR struct bk7258_timer_proxy_s *priv = bk7258_timer_from_lower(lower);

  return TIMER_MAXTIMEOUT(priv->hardware, maxtimeout);
}
#endif

static void bk7258_systick_verify(uint32_t expected_reload)
{
  uint32_t ctrl;
  uint32_t route;

  ctrl = getreg32(NVIC_SYSTICK_CTRL);
  route = getreg32(BK7258_SYS_CPU_POWER_SLEEP_WAKEUP);
  if ((route & BK7258_SYSTICK_32K_ROUTE) == 0 ||
      getreg32(NVIC_SYSTICK_RELOAD) != expected_reload ||
      (ctrl & (NVIC_SYSTICK_CTRL_CLKSOURCE |
               NVIC_SYSTICK_CTRL_TICKINT |
               NVIC_SYSTICK_CTRL_ENABLE)) !=
              (NVIC_SYSTICK_CTRL_TICKINT |
               NVIC_SYSTICK_CTRL_ENABLE))
    {
      PANIC();
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   Called during start-up (up_initialize) to initialise the timer
 *   hardware that drives the NuttX system clock.  Registers the common
 *   Cortex-M SysTick lower half on the fixed 32-kHz external clock.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
#ifdef CONFIG_ARCH_PERF_EVENTS
  uint32_t cpu_hz;
#endif
#ifdef CONFIG_TIMER_ARCH
  FAR struct timer_lowerhalf_s *hardware;
#endif
  uint32_t reload;

  /* Route the same fixed 32-kHz source used by the official BK7258 AP/CP
   * ports before selecting SysTick's external clock input.  Keep DWT tied to
   * the independently decoded live CPU frequency.
   */

  reload = (BK7258_SYSTICK_FREQUENCY_HZ / CLK_TCK) - 1;

  modifyreg32(BK7258_SYS_CPU_POWER_SLEEP_WAKEUP, 0,
              BK7258_SYSTICK_32K_ROUTE);
  DEBUGASSERT((getreg32(BK7258_SYS_CPU_POWER_SLEEP_WAKEUP) &
               BK7258_SYSTICK_32K_ROUTE) != 0);

#ifdef CONFIG_ARCH_PERF_EVENTS
  /* Armv8-M perf_gettime() reads CPU-clocked DWT_CYCCNT.  Initialize its
   * conversion separately from the fixed scheduler clock.
   */

  cpu_hz = bk7258_clockdiag_current_cpu_hz();
  up_perf_init((void *)(uintptr_t)cpu_hz);
#endif

  putreg32(reload, NVIC_SYSTICK_RELOAD);

#ifdef CONFIG_TIMER_ARCH
  /* The architecture-timer framework owns SysTick through the common timer
   * lower half.  The board proxy preserves that private callback so the CP
   * standby path can advance both arch_timer's timebase and scheduler timers
   * without modifying NuttX.  coreclk=false selects the routed 32-kHz input;
   * minor=-1 keeps it dedicated to the scheduler instead of publishing
   * /dev/timerN.
   */

  hardware = systick_initialize(false, BK7258_SYSTICK_FREQUENCY_HZ, -1);
  if (hardware == NULL)
    {
      PANIC();
    }

  g_bk7258_timer.hardware = hardware;
  up_timer_set_lowerhalf(&g_bk7258_timer.lower);
#else
  /* up_timer_set_lowerhalf() is an empty macro in a periodic-tick build.
   * Calling only systick_initialize() would therefore leave CTRL.ENABLE
   * clear and its lower-half callback unset: nxsig_usleep(), watchdogs and
   * every timeout-driven CP worker would stop permanently on their first
   * wait.  Install the normal periodic clock ISR and start SysTick here.
   */

  irq_attach(NVIC_IRQ_SYSTICK, bk7258_timer_interrupt, NULL);
  putreg32(0, NVIC_SYSTICK_CURRENT);
  putreg32(NVIC_SYSTICK_CTRL_TICKINT |
           NVIC_SYSTICK_CTRL_ENABLE,
           NVIC_SYSTICK_CTRL);
  up_enable_irq(NVIC_IRQ_SYSTICK);
#endif

  bk7258_systick_verify(reload);
}

/****************************************************************************
 * Name: bk7258_systick_recalc
 *
 * Description:
 *   Refresh the CPU-clocked DWT conversion after a runtime DVFS switch.
 *   Scheduler SysTick stays on the fixed 32-kHz source, so its route, reload,
 *   current phase and NuttX lower-half frequency must not change here.
 *
 ****************************************************************************/

void bk7258_systick_recalc(void)
{
#ifdef CONFIG_ARCH_PERF_EVENTS
  uint32_t cpu_hz;

  cpu_hz = bk7258_clockdiag_current_cpu_hz();

  /* Keep cycle-to-time conversion correct after changing the CPU clock. */

  up_perf_init((void *)(uintptr_t)cpu_hz);
#endif
}

/****************************************************************************
 * Name: bk7258_systick_prepare_sleep
 *
 * Description:
 *   Snapshot the elapsed part of the current scheduler tick immediately
 *   before the immutable CP low-voltage leaf takes ownership of SysTick.
 *   The caller already has interrupts and scheduling disabled through
 *   pm_idle(); the nested critical section also makes this helper safe if it
 *   is reused by another board path.
 *
 ****************************************************************************/

#ifdef CONFIG_TIMER_ARCH
int bk7258_systick_prepare_sleep(void)
{
  struct timer_status_s status;
  irqstate_t flags;
  int ret;

  flags = enter_critical_section();
  if (g_bk7258_timer.hardware == NULL ||
      g_bk7258_timer.callback == NULL ||
      g_bk7258_timer.sleep_prepared ||
      g_bk7258_timer.compensation_pending ||
      g_bk7258_timer.compensation_in_progress)
    {
      ret = -EAGAIN;
      goto out;
    }

  /* Freeze CVR before reading it.  A PendST that was already latched remains
   * visible to arm_systick's getstatus() and is folded into the saved phase;
   * restore later clears that hardware bit after accounting for it.
   */

  ret = TIMER_STOP(g_bk7258_timer.hardware);
  if (ret < 0)
    {
      goto out;
    }

  ret = TIMER_GETSTATUS(&g_bk7258_timer.lower, &status);
  if (ret < 0 || (status.flags & TCFLAGS_HANDLER) == 0)
    {
      PANIC();
    }

  if (status.timeout < status.timeleft)
    {
      PANIC();
    }

  g_bk7258_timer.sleep_phase_us = status.timeout - status.timeleft;
  g_bk7258_timer.sleep_prepared = true;
  UP_DSB();
  UP_ISB();
  ret = OK;

out:
  leave_critical_section(flags);
  return ret;
}

/****************************************************************************
 * Name: bk7258_systick_restore_after_sleep
 *
 * Description:
 *   Restore and advance the architecture timer after the immutable CP
 *   low-voltage leaf.  That SDK leaf deliberately loads SysTick with
 *   0x00ffffff while clocks settle, then restores CTRL without restoring the
 *   normal RVR.  Reassert the fixed route, clear the stale pending edge and
 *   queue a real SysTick exception that invokes arch_timer's saved callback
 *   once per elapsed whole tick.  A short first hardware interval carries
 *   the fractional pre-sleep phase forward, so CLOCK_MONOTONIC cannot step
 *   backwards merely because CVR was reset.
 *
 ****************************************************************************/

uint32_t bk7258_systick_restore_after_sleep(uint64_t elapsed_us,
                                            uint32_t max_ticks)
{
  irqstate_t flags;
  uint64_t due_ticks;
  uint64_t total_us;
  uint32_t actual_timeout_us;
  uint32_t desired_reload;
  uint32_t next_interval_us;
  uint32_t remainder_us;
  uint32_t ticks;
  int ret;

  flags = enter_critical_section();
  if (!g_bk7258_timer.sleep_prepared ||
      g_bk7258_timer.hardware == NULL ||
      g_bk7258_timer.callback == NULL || max_ticks == 0)
    {
      PANIC();
    }

  /* Stop the temporary SDK timer before exposing the repaired timebase.
   * Account for any stale PendST in the captured phase/elapsed interval and
   * clear the hardware bit so it cannot deliver the same logical tick twice.
   */

  putreg32(0, NVIC_SYSTICK_CTRL);
  putreg32(NVIC_INTCTRL_PENDSTCLR, NVIC_INTCTRL);
  modifyreg32(BK7258_SYS_CPU_POWER_SLEEP_WAKEUP, 0,
              BK7258_SYSTICK_32K_ROUTE);
  putreg32((BK7258_SYSTICK_FREQUENCY_HZ / CLK_TCK) - 1,
           NVIC_SYSTICK_RELOAD);
  putreg32(0, NVIC_SYSTICK_CURRENT);

  /* The SDK also restored the shared CPU mux before returning. */

  bk7258_systick_recalc();

  total_us = elapsed_us + g_bk7258_timer.sleep_phase_us;
  due_ticks = total_us / BK7258_SYSTICK_TIMEOUT_US;
  remainder_us = (uint32_t)(total_us % BK7258_SYSTICK_TIMEOUT_US);
  if (due_ticks > max_ticks)
    {
      PANIC();
    }

  ticks = (uint32_t)due_ticks;

  /* SysTick's minimum supported reload is one, or two 32-kHz counts.  When
   * the residual phase would require an even shorter first interval, advance
   * one logical tick instead.  This is a bounded forward rounding below
   * 62.5 us; allowing the lower half to clamp upward would move time back.
   */

  if (remainder_us != 0 &&
      BK7258_SYSTICK_TIMEOUT_US - remainder_us <
        BK7258_SYSTICK_MIN_TIMEOUT_US)
    {
      if (ticks >= max_ticks)
        {
          PANIC();
        }

      ticks++;
      remainder_us = 0;
    }

  g_bk7258_timer.phase_active = false;
  g_bk7258_timer.phase_bias_us = 0;
  g_bk7258_timer.phase_boundary_latched = false;

  next_interval_us = BK7258_SYSTICK_TIMEOUT_US;
  if (remainder_us != 0)
    {
      next_interval_us -= remainder_us;
    }

  ret = TIMER_SETTIMEOUT(g_bk7258_timer.hardware, next_interval_us);
  if (ret < 0)
    {
      PANIC();
    }

  desired_reload = getreg32(NVIC_SYSTICK_RELOAD);
  actual_timeout_us =
    ((desired_reload + 1u) * USEC_PER_SEC) /
    BK7258_SYSTICK_FREQUENCY_HZ;
  if (actual_timeout_us > BK7258_SYSTICK_TIMEOUT_US)
    {
      PANIC();
    }

  if (remainder_us != 0)
    {
      g_bk7258_timer.pending_phase_bias_us =
        BK7258_SYSTICK_TIMEOUT_US - actual_timeout_us;
    }
  else
    {
      g_bk7258_timer.pending_phase_bias_us = 0;
    }

  /* Run the compensation callback only after the processor takes a genuine
   * SysTick exception.  Until pm_idle restores the incoming interrupt mask,
   * use the longest hardware period so a very short residual interval cannot
   * wrap before the synthetic PendST trampoline is serviced.  The trampoline
   * replaces this reload directly while higher-priority time readers remain
   * excluded.
   */

  g_bk7258_timer.compensation_ticks = ticks;
  g_bk7258_timer.pending_reload = desired_reload;
  g_bk7258_timer.compensation_pending = true;
  putreg32(NVIC_MAX_SYSTICK_CNT, NVIC_SYSTICK_RELOAD);
  putreg32(0, NVIC_SYSTICK_CURRENT);

  putreg32(NVIC_SYSTICK_CTRL_TICKINT, NVIC_SYSTICK_CTRL);
  ret = TIMER_START(g_bk7258_timer.hardware);
  if (ret < 0)
    {
      PANIC();
    }

  putreg32(NVIC_INTCTRL_PENDSTSET, NVIC_INTCTRL);
  UP_DSB();
  UP_ISB();
  bk7258_systick_verify(NVIC_MAX_SYSTICK_CNT);
  g_bk7258_timer.sleep_phase_us = 0;
  g_bk7258_timer.sleep_prepared = false;
  leave_critical_section(flags);
  return ticks;
}
#endif /* CONFIG_TIMER_ARCH */
