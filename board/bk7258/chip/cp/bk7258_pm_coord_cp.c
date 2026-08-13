/****************************************************************************
 * board/bk7258/chip/cp/bk7258_pm_coord_cp.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP half of the BK7258 v3.1.1.9 coordinated low-voltage protocol.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_pm.h>
#include <arch/chip/bk7258_rptun.h>

#include <common/bk_err.h>
#include <driver/aon_rtc.h>
#include <driver/flash.h>

#include "bk7258_pm_coord.h"
#include "bk7258_pm_activity.h"
#include "bk7258_rptun_mbox.h"
#include "bk7258_wdt.h"
#include "bk7258_dvfs.h"
#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_PM_WAKE_PERIOD_MS        20u
#define BK7258_PM_AP_WAIT_US            3000u
#define BK7258_PM_MBOX_WAIT_US          1000u
#define BK7258_PM_MAX_COMP_TICKS        100u

#define BK7258_SYS_BASE                 0x44010000u
#define BK7258_SYS_CPU0_STATUS_LOW      (BK7258_SYS_BASE + 0xa0u)
#define BK7258_SYS_CPU0_STATUS_HIGH     (BK7258_SYS_BASE + 0xa4u)
#define BK7258_NVIC_PENDING0            0xe000e200u
#define BK7258_NVIC_PENDING1            0xe000e204u
#define BK7258_NVIC_ICSR                0xe000ed04u
#define BK7258_NVIC_ICSR_PENDSTSET      (1u << 26)

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

extern void sys_hal_enter_low_voltage(void);
extern void sys_hal_rtc_wakeup_enable(uint32_t value);
extern void sys_drv_low_power_hardware_init(void);
extern int __real_bk_pm_module_vote_sleep_ctrl(unsigned int module,
                                                uint32_t sleep_state,
                                                uint32_t sleep_time);

/****************************************************************************
 * Public Data
 ****************************************************************************/

volatile struct bk7258_pm_coord_diag_s g_bk7258_pm_coord_diag;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_bk7258_pm_wake_armed;
static bool g_bk7258_pm_flash_prepared;
static volatile bool g_bk7258_pm_restore_seen;
static uint64_t g_bk7258_pm_tick_remainder_us;
static struct bk7258_pm_activity_s g_bk7258_pm_sdk_activity;
#ifdef CONFIG_BK7258_PM_STANDBY_ONESHOT_VERIFY
static bool g_bk7258_pm_standby_verified;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_pm_enter_low_voltage(void)
{
  uint8_t basepri = getbasepri();
  uint8_t primask = getprimask();

  /* The SDK calls sys_hal_enter_low_voltage() inside
   * rtos_enter_critical().  On its CM33 FreeRTOS port that means PRIMASK=1
   * with BASEPRI left at zero.  NuttX pm_idle(), however, reaches this board
   * handler with BASEPRI raised.  STAR does not leave WFI for an interrupt
   * masked by BASEPRI; hardware inspection then shows RTC pending at AON,
   * SYS and NVIC while the core remains inside arch_deep_sleep().
   *
   * Translate only the interrupt-mask representation at the wrapper
   * boundary.  PRIMASK keeps the SDK's critical-section contract while a
   * zero BASEPRI lets the configured AON wake request release deep WFI.
   * Restore NuttX's exact incoming masks before returning to pm_idle().
   */

  setprimask(1);
  setbasepri(0);
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  sys_hal_enter_low_voltage();

  setprimask(1);
  setbasepri(basepri);
  setprimask(primask);
  __asm volatile ("isb sy" ::: "memory");
}

static inline uint64_t bk7258_pm_vendor_vote(uint32_t offset)
{
  uintptr_t addr = BK7258_PWR_MNG_ADDR + offset;
  uint32_t low = getreg32(addr);
  uint32_t high = getreg32(addr + 4u);

  return ((uint64_t)high << 32) | low;
}

static inline void bk7258_pm_set_cp_vote(bool set)
{
  uint32_t value = getreg32(BK7258_AON_PMU_R3_ADDR);

  if (set)
    {
      value |= BK7258_AON_CP_SLEEP_VOTE_BIT;
    }
  else
    {
      value &= ~BK7258_AON_CP_SLEEP_VOTE_BIT;
    }

  putreg32(value, BK7258_AON_PMU_R3_ADDR);
  __asm volatile ("dmb sy" ::: "memory");
}

static bool bk7258_pm_cp_irq_pending(void)
{
  return getreg32(BK7258_NVIC_PENDING0) != 0 ||
         getreg32(BK7258_NVIC_PENDING1) != 0 ||
         getreg32(BK7258_SYS_CPU0_STATUS_LOW) != 0 ||
         getreg32(BK7258_SYS_CPU0_STATUS_HIGH) != 0 ||
         (getreg32(BK7258_NVIC_ICSR) & BK7258_NVIC_ICSR_PENDSTSET) != 0;
}

static bool bk7258_pm_wait_mbox_idle(void)
{
  irqstate_t flags;
  uint64_t start_us;
  bool idle;

  /* pm_idle() enters the board hook with BASEPRI raised.  PREPARE is already
   * accepted by AP0 before it publishes the AON WFI bit, but CP's logical
   * mailbox TX-complete callback cannot retire tx_active behind that mask.
   * Briefly admit interrupts for the bounded ACK window, then restore the
   * caller's exact mask before the final IRQ/pending check and SDK sleep leaf.
   */

  flags = up_irq_save();
  up_irq_enable();
  start_us = bk_aon_rtc_get_us();
  do
    {
      idle = bk7258_rptun_mbox_is_idle();
      if (idle)
        {
          break;
        }
    }
  while (bk_aon_rtc_get_us() - start_us < BK7258_PM_MBOX_WAIT_US);

  up_irq_restore(flags);
  return idle;
}

static bool bk7258_pm_rptun_idle(void)
{
  volatile struct bk7258_rptun_control_s *rptun = bk7258_rptun_control();

  __asm volatile ("dmb sy" ::: "memory");
  return rptun->magic == BK7258_RPTUN_CONTROL_MAGIC &&
         rptun->state == BK7258_RPTUN_STATE_CONNECTED &&
         rptun->generation != 0 && rptun->cp_to_ap_pending == 0 &&
         rptun->ap_to_cp_pending == 0;
}

static bool bk7258_pm_ap_ready(void)
{
  volatile struct bk7258_ap_boot_state_s *boot = bk7258_ap_boot_state();
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
  volatile struct bk7258_rptun_control_s *rptun = bk7258_rptun_control();

  __asm volatile ("dmb sy" ::: "memory");
  return boot->magic == BK7258_AP_BOOT_STATE_MAGIC &&
         boot->state == BK7258_AP_STATE_READY && boot->error == 0 &&
         boot->generation != 0 && boot->generation == rptun->generation &&
         smp->magic == BK7258_AP_SMP_STATE_MAGIC &&
         (smp->state == BK7258_AP_SMP_STATE_ONLINE ||
          smp->state == BK7258_AP_SMP_STATE_PASSED) &&
         smp->error == 0 && smp->generation == boot->generation &&
         smp->online_mask == 0x3u;
}

static bool bk7258_pm_votes_idle(void)
{
  return bk7258_pm_vendor_vote(BK7258_PWR_AP_SLEEP_VOTE_OFFSET) == 0 &&
         bk7258_pm_vendor_vote(BK7258_PWR_AP_CLOCK_VOTE_OFFSET) == 0 &&
         bk7258_pm_activity_idle(&g_bk7258_pm_sdk_activity) &&
         bk7258_pm_frequency_votes_idle() &&
         bk7258_pm_server_resources_idle();
}

static void bk7258_pm_record(uint32_t reason)
{
  uint64_t sleep_vote =
    bk7258_pm_vendor_vote(BK7258_PWR_AP_SLEEP_VOTE_OFFSET);
  uint64_t clock_vote =
    bk7258_pm_vendor_vote(BK7258_PWR_AP_CLOCK_VOTE_OFFSET);

  /* Re-publish the header on every checkpoint.  SDK initialization may use
   * the same NuttX heap immediately before this recorder first runs, so the
   * diagnostic remains self-identifying even if its initial zero-and-header
   * store was observed between allocator operations by an external probe.
   */

  g_bk7258_pm_coord_diag.magic = BK7258_PM_COORD_DIAG_MAGIC;
  g_bk7258_pm_coord_diag.version = BK7258_PM_COORD_DIAG_VERSION;
  g_bk7258_pm_coord_diag.size = sizeof(g_bk7258_pm_coord_diag);
  g_bk7258_pm_coord_diag.last_reason = reason;
  g_bk7258_pm_coord_diag.last_aon_r3 =
    getreg32(BK7258_AON_PMU_R3_ADDR);
  g_bk7258_pm_coord_diag.last_ap_sleep_vote_low =
    (uint32_t)sleep_vote;
  g_bk7258_pm_coord_diag.last_ap_sleep_vote_high =
    (uint32_t)(sleep_vote >> 32);
  g_bk7258_pm_coord_diag.last_ap_clock_vote_low =
    (uint32_t)clock_vote;
  g_bk7258_pm_coord_diag.last_ap_clock_vote_high =
    (uint32_t)(clock_vote >> 32);
  g_bk7258_pm_coord_diag.last_cp_sdk_awake_low =
    g_bk7258_pm_sdk_activity.awake_low;
  g_bk7258_pm_coord_diag.last_cp_sdk_awake_high =
    g_bk7258_pm_sdk_activity.awake_high;
  __asm volatile ("dmb sy" ::: "memory");

  /* This diagnostic object lives in cacheable CP SRAM.  Clean it after each
   * bounded update so an external SWD debugger observes the same state as the
   * running core; this is evidence plumbing only and is not part of the
   * coordination protocol itself.
   */

  up_clean_dcache((uintptr_t)&g_bk7258_pm_coord_diag,
                  (uintptr_t)&g_bk7258_pm_coord_diag +
                  sizeof(g_bk7258_pm_coord_diag));
}

static void bk7258_pm_wake_alarm(aon_rtc_id_t id, uint8_t *name,
                                  void *arg)
{
  (void)id;
  (void)name;
  (void)arg;
}

static void bk7258_pm_abort(uint32_t reason)
{
  bk7258_pm_set_cp_vote(false);

  /* PREPARE and RELEASE share one hardware slot.  Retire PREPARE's accepted
   * ACK before sending RELEASE so an abort cannot leave either AP core with
   * its AON WFI bit asserted behind a merely deferred logical message.
   */

  (void)bk7258_pm_wait_mbox_idle();
  (void)bk7258_rptun_mbox_pm_wake(BK7258_RPTUN_PM_WAKE_RELEASE);
  g_bk7258_pm_coord_diag.aborted++;
  bk7258_pm_record(reason);
}

static void bk7258_pm_compensate_ticks(uint64_t elapsed_us)
{
  uint64_t total = elapsed_us + g_bk7258_pm_tick_remainder_us;
  uint32_t ticks = (uint32_t)(total / USEC_PER_TICK);
  uint32_t i;

  g_bk7258_pm_tick_remainder_us = total % USEC_PER_TICK;
  if (ticks > BK7258_PM_MAX_COMP_TICKS)
    {
      ticks = BK7258_PM_MAX_COMP_TICKS;
      g_bk7258_pm_tick_remainder_us = 0;
    }

  for (i = 0; i < ticks; i++)
    {
      nxsched_process_timer();
    }

  g_bk7258_pm_coord_diag.compensated_ticks += ticks;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int __wrap_bk_pm_module_vote_sleep_ctrl(unsigned int module,
                                         uint32_t sleep_state,
                                         uint32_t sleep_time)
{
  irqstate_t flags;
  int ret;

  /* Keep the CP SDK's local wake-source side effects, especially the Wi-Fi
   * and Bluetooth system-wake enables.  Mirror only successful set-state
   * transitions into the NuttX-owned admission gate.  The full SDK PM state
   * machine remains unstarted and is never called from NuttX idle.
   */

  if (module >= BK7258_PM_SDK_SLEEP_MODULE_COUNT || sleep_state > 1u)
    {
      return -EINVAL;
    }

  ret = __real_bk_pm_module_vote_sleep_ctrl(module, sleep_state,
                                             sleep_time);
  if (ret != BK_OK)
    {
      return ret;
    }

  flags = up_irq_save();
  ret = bk7258_pm_activity_vote(&g_bk7258_pm_sdk_activity, module,
                                sleep_state);
  up_irq_restore(flags);
  return ret;
}

void bk7258_pm_coord_early_initialize(void)
{
  /* Both the v3.1.1.9 SDK and Tuya call pm_hardware_init() from CPU0
   * startup before entering the application.  NuttX owns the PM policy and
   * must not start the SDK/FreeRTOS PM state machine, but it still requires
   * the hardware half of that initialization: AON wake enables, LPO source,
   * low-voltage parameters, DCO/ROSC calibration, default power state and
   * the shared CP/AP PM words.  Use the exact SDK driver leaf at the same
   * pre-scheduler point, while AP0/AP1 are still held off.
   */

  memset(&g_bk7258_pm_sdk_activity, 0,
         sizeof(g_bk7258_pm_sdk_activity));
  sys_drv_low_power_hardware_init();
}

int bk7258_pm_coord_initialize(void)
{
  alarm_info_t alarm;
  float ticks_per_ms;
  int ret;

  memset((void *)(uintptr_t)&g_bk7258_pm_coord_diag, 0,
         sizeof(g_bk7258_pm_coord_diag));
  g_bk7258_pm_coord_diag.magic = BK7258_PM_COORD_DIAG_MAGIC;
  g_bk7258_pm_coord_diag.version = BK7258_PM_COORD_DIAG_VERSION;
  g_bk7258_pm_coord_diag.size = sizeof(g_bk7258_pm_coord_diag);

  ret = bk_aon_rtc_driver_init();
  if (ret != BK_OK)
    {
      bk7258_pm_record(BK7258_PM_COORD_REASON_WAKE_NOT_ARMED);
      return -EIO;
    }

  memset(&alarm, 0, sizeof(alarm));
  memcpy(alarm.name, "nuttx-pm", sizeof("nuttx-pm"));
  ticks_per_ms = bk_rtc_get_ms_tick_count();
  alarm.period_tick =
    (rtc_tick_t)(BK7258_PM_WAKE_PERIOD_MS * ticks_per_ms);
  alarm.period_cnt = UINT32_MAX;
  alarm.callback = bk7258_pm_wake_alarm;
  ret = bk_alarm_register(AON_RTC_ID_1, &alarm);
  if (ret != BK_OK)
    {
      bk7258_pm_record(BK7258_PM_COORD_REASON_WAKE_NOT_ARMED);
      return -EIO;
    }

  sys_hal_rtc_wakeup_enable(1);
  g_bk7258_pm_wake_armed = true;
#ifdef CONFIG_BK7258_PM_STANDBY_ONESHOT_VERIFY
  g_bk7258_pm_standby_verified = false;
#endif
  bk7258_pm_set_cp_vote(false);
  bk7258_pm_record(BK7258_PM_COORD_REASON_NONE);
  return OK;
}

bool bk7258_pm_cp_can_standby(void)
{
#ifdef CONFIG_BK7258_PM_STANDBY_ONESHOT_VERIFY
  if (g_bk7258_pm_standby_verified)
    {
      return false;
    }
#endif

  if (!g_bk7258_pm_wake_armed || !bk7258_pm_ap_ready())
    {
      return false;
    }

  return bk7258_pm_votes_idle() && bk7258_pm_rptun_idle() &&
         bk7258_rptun_mbox_is_idle() && !bk7258_pm_cp_irq_pending();
}

bool bk7258_pm_cp_standby(void)
{
  uint64_t start_us;
  uint64_t end_us;
  uint32_t aon;

#ifdef CONFIG_BK7258_PM_STANDBY_ONESHOT_VERIFY
  /* The NuttX system domain can remain in PM_STANDBY across consecutive
   * idle passes, in which case pm_idle() does not call prepare() again.
   * Enforce the verification ceiling at the execution boundary as well as
   * in bk7258_pm_cp_can_standby().
   */

  if (g_bk7258_pm_standby_verified)
    {
      return false;
    }
#endif

  g_bk7258_pm_coord_diag.attempts++;
  if (!g_bk7258_pm_wake_armed || !bk7258_pm_ap_ready())
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_NOT_READY);
      return false;
    }

  if (!bk7258_pm_activity_idle(&g_bk7258_pm_sdk_activity))
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_SDK_ACTIVITY);
      return false;
    }

  if (!bk7258_pm_votes_idle())
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_ACTIVE_VOTE);
      return false;
    }

  if (!bk7258_pm_rptun_idle())
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_RPTUN_BUSY);
      return false;
    }

  if (!bk7258_rptun_mbox_is_idle())
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_MBOX_BUSY);
      return false;
    }

  if (bk7258_pm_cp_irq_pending())
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_CP_IRQ_PENDING);
      return false;
    }

  bk7258_pm_set_cp_vote(true);
  if (bk7258_rptun_mbox_pm_wake(BK7258_RPTUN_PM_WAKE_PREPARE) < 0)
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_MBOX_BUSY);
      return false;
    }

  start_us = bk_aon_rtc_get_us();
  do
    {
      aon = getreg32(BK7258_AON_PMU_R3_ADDR);
      if ((aon & BK7258_AON_AP_WFI_BITS) == BK7258_AON_AP_WFI_BITS)
        {
          break;
        }
    }
  while (bk_aon_rtc_get_us() - start_us < BK7258_PM_AP_WAIT_US);

  aon = getreg32(BK7258_AON_PMU_R3_ADDR);
  if ((aon & BK7258_AON_AP_WFI_BITS) != BK7258_AON_AP_WFI_BITS)
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_AP_TIMEOUT);
      return false;
    }

  if (!bk7258_pm_votes_idle() || !bk7258_pm_rptun_idle())
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_AP_VOTE_CHANGED);
      return false;
    }

  if (!bk7258_pm_wait_mbox_idle())
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_MBOX_BUSY);
      return false;
    }

  /* AP acknowledgement can take up to 3 ms.  Close the final race with a
   * CP interrupt arriving after the first eligibility test but before the
   * SDK leaf masks normal interrupt sources and changes flash/voltage.
   */

  if (bk7258_pm_cp_irq_pending())
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_CP_IRQ_PENDING);
      return false;
    }

  if (bk_flash_power_saving_enter() != BK_OK)
    {
      bk7258_pm_abort(BK7258_PM_COORD_REASON_FLASH_PREPARE);
      return false;
    }

  g_bk7258_pm_flash_prepared = true;
#ifdef CONFIG_BK7258_WDT
  bk7258_wdt_pm_prepare();
#endif
  g_bk7258_pm_restore_seen = false;
  start_us = bk_aon_rtc_get_us();
  bk7258_pm_enter_low_voltage();
  end_us = bk_aon_rtc_get_us();

  /* The SDK leaf can reject a last-moment pending IRQ before it reaches its
   * internal restore callback.  Make both resource restores idempotent and
   * unconditional at the NuttX boundary.
   */

  if (g_bk7258_pm_flash_prepared)
    {
      (void)bk_flash_power_saving_exit();
      g_bk7258_pm_flash_prepared = false;
    }
#ifdef CONFIG_BK7258_WDT
  bk7258_wdt_pm_restore();
#endif
  bk7258_systick_recalc();

  /* The immutable SDK leaf has no return value.  Its successful path calls
   * pm_low_voltage_bsp_restore(), then clears the CP vote and releases AP0.
   * A last-moment pending IRQ returns before all three actions.  Treat the
   * wrapped restore callback as the success witness and complete the normal
   * abort/release protocol when it was not observed.
   */

  if (!g_bk7258_pm_restore_seen)
    {
      g_bk7258_pm_coord_diag.last_sleep_us = 0;
      bk7258_pm_abort(BK7258_PM_COORD_REASON_SDK_REJECTED);
      return false;
    }

  /* sys_hal_enter_low_voltage() performs the official MB_CHNL_PWC release
   * and clears the CP AON vote after clocks, flash and voltage are restored.
   * Do not enqueue a second lower-priority MB_CHNL_LOG edge or perform a
   * second whole-register RMW on the successful path: AP0 may already be
   * clearing its own WFI bit concurrently.
   */

  g_bk7258_pm_coord_diag.entered++;
  g_bk7258_pm_coord_diag.wakeups++;
  g_bk7258_pm_coord_diag.last_sleep_us = (uint32_t)(end_us - start_us);
  bk7258_pm_compensate_ticks(end_us - start_us);
#ifdef CONFIG_BK7258_PM_STANDBY_ONESHOT_VERIFY
  g_bk7258_pm_standby_verified = true;
#endif
  bk7258_pm_record(BK7258_PM_COORD_REASON_ENTERED);
  return true;
}

void __wrap_pm_low_voltage_bsp_restore(void)
{
  g_bk7258_pm_restore_seen = true;

  if (g_bk7258_pm_flash_prepared)
    {
      (void)bk_flash_power_saving_exit();
      g_bk7258_pm_flash_prepared = false;
    }

#ifdef CONFIG_BK7258_WDT
  bk7258_wdt_pm_restore();
#endif
  sys_hal_rtc_wakeup_enable(1);
}

#endif /* CONFIG_BK7258_PM_COORDINATED_STANDBY */
