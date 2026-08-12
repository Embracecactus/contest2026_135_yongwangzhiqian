/****************************************************************************
 * board/bk7258/chip/ap/bk7258_pm_coord_ap.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP half of the BK7258 v3.1.1.9 coordinated low-voltage protocol.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/power/pm.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_rptun.h>

#include "arm_internal.h"
#include "nvic.h"
#include "bk7258_pm_coord.h"
#include "bk7258_rptun_mbox.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SYS_BASE                 0x44010000u
#define BK7258_SYS_CPU1_EN_LOW          (BK7258_SYS_BASE + 0x88u)
#define BK7258_SYS_CPU1_EN_HIGH         (BK7258_SYS_BASE + 0x8cu)
#define BK7258_SYS_CPU2_EN_LOW          (BK7258_SYS_BASE + 0x90u)
#define BK7258_SYS_CPU2_EN_HIGH         (BK7258_SYS_BASE + 0x94u)
#define BK7258_SYS_CPU1_STATUS_LOW      (BK7258_SYS_BASE + 0xa8u)
#define BK7258_SYS_CPU1_STATUS_HIGH     (BK7258_SYS_BASE + 0xacu)
#define BK7258_SYS_CPU2_STATUS_LOW      (BK7258_SYS_BASE + 0xb0u)
#define BK7258_SYS_CPU2_STATUS_HIGH     (BK7258_SYS_BASE + 0xb4u)

#define BK7258_NVIC_PENDING0            0xe000e200u
#define BK7258_NVIC_PENDING1            0xe000e204u
#define BK7258_NVIC_ICSR                0xe000ed04u
#define BK7258_NVIC_ICSR_PENDSTSET      (1u << 26)
#define BK7258_SYSTICK_CTRL             0xe000e010u
#define BK7258_MAILBOX_WAKE_BIT         (1u << 31)

#define BK7258_AP_LOGICAL_CPU0          0u
#define BK7258_AP_LOGICAL_CPU1          1u
#define BK7258_PM_AP_PEER_WAIT_US       1000u

#define BK7258_PM_AP_DIAG_MAGIC         0x44415042u /* "BPAD" */
#define BK7258_PM_AP_DIAG_VERSION       2u
#define BK7258_PM_AP_TRACE_MAGIC        0x54504142u /* "BAPT" */
#define BK7258_PM_AP_TRACE_VERSION      1u

#define BK7258_PM_AP_TRACE_RUNTIME      (1u << 0)
#define BK7258_PM_AP_TRACE_IDLE         (1u << 1)
#define BK7258_PM_AP_TRACE_DEEP         (1u << 2)
#define BK7258_PM_AP_TRACE_PENDING_OK   (1u << 3)
#define BK7258_PM_AP_TRACE_VOTE_OK      (1u << 4)
#define BK7258_PM_AP_TRACE_AON_SET      (1u << 5)
#define BK7258_PM_AP_TRACE_WAKE         (1u << 6)
#define BK7258_PM_AP_TRACE_PENDING_REJ  (1u << 7)
#define BK7258_PM_AP_TRACE_VOTE_REJ     (1u << 8)
#define BK7258_PM_AP_TRACE_PEER_SEEN    (1u << 9)
#define BK7258_PM_AP_TRACE_PEER_TIMEOUT (1u << 10)
#define BK7258_PM_AP_TRACE_IPI_ISR      (1u << 11)
#define BK7258_PM_AP_TRACE_IPI_VOTE     (1u << 12)

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

/* The DMA status query and SDK callback dispatcher are optional archive
 * leaves.  Production profiles which initialize those SDK subsystems pull
 * them in; a profile without either subsystem has no corresponding work or
 * callback to drain.
 */

extern uint32_t bk_dma_check_chn_status(void) weak_function;
extern void bk_pm_handle_lv_sleep_callback(int state) weak_function;
extern int up_send_smp_sched(int cpu);

/****************************************************************************
 * Private Data
 ****************************************************************************/

struct bk7258_pm_ap_diag_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t idle_entries[2];
  uint32_t shallow_vote_wakes[2];
  uint32_t deep_entries[2];
  uint32_t pending_rejects[2];
  uint32_t vote_rejects[2];
  uint32_t aon_sets[2];
  uint32_t wakeups[2];
  uint32_t forward_attempts;
  int32_t forward_last_ret;
  uint32_t last_nvic_pending[2];
  uint32_t last_sys_status[2];
  uint32_t last_dma;
  uint32_t last_icsr;
  uint32_t prepare_waits;
  uint32_t prepare_takes;
};

volatile struct bk7258_pm_ap_diag_s g_bk7258_pm_ap_diag;

/* BK7258 AP cores have private, non-coherent D-caches.  Keep each core's
 * trace in a distinct cache-line-aligned object and let only that core write
 * and clean it.  The trace is observational; coordination still uses AON and
 * the physical mailbox exclusively.
 */

struct bk7258_pm_ap_core_trace_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t stages;
  uint32_t idle_entries;
  uint32_t deep_entries;
  uint32_t pending_rejects;
  uint32_t vote_rejects;
  uint32_t aon_sets;
  uint32_t wakeups;
  uint32_t peer_waits;
  uint32_t peer_timeouts;
  uint32_t last_nvic_pending[2];
  uint32_t last_sys_status[2];
  uint32_t last_dma;
  uint32_t last_icsr;
  uint32_t reserved[6];
} aligned_data(32);

volatile struct bk7258_pm_ap_core_trace_s g_bk7258_pm_ap_core_trace[2]
  aligned_data(32);

struct bk7258_pm_ap_irq_sleep_s
{
  bool armed;
  uint32_t systick;
  uint32_t int_low;
  uint32_t int_high;
};

/* AP1 does not have a local SysTick and its qualified scheduler can replace
 * the bootstrap idle exception frame before the CP vote arrives.  Keep the
 * exception-tail sleep state in AP1's private cache; AON remains the protocol
 * truth visible to CP and AP0.
 */

static struct bk7258_pm_ap_irq_sleep_s g_bk7258_pm_ap_irq_sleep;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t bk7258_pm_local_cpu(void)
{
  return *(volatile uint32_t *)(uintptr_t)BK7258_LOCAL_CORE_ID_ADDR;
}

static void bk7258_pm_ap_diag_publish(void)
{
  __asm volatile ("dmb sy" ::: "memory");
  up_clean_dcache((uintptr_t)&g_bk7258_pm_ap_diag,
                  (uintptr_t)&g_bk7258_pm_ap_diag +
                  sizeof(g_bk7258_pm_ap_diag));
}

static void bk7258_pm_ap_trace_publish(uint32_t cpu)
{
  uintptr_t start =
    (uintptr_t)&g_bk7258_pm_ap_core_trace[cpu];

  __asm volatile ("dmb sy" ::: "memory");
  up_clean_dcache(start, start + sizeof(g_bk7258_pm_ap_core_trace[cpu]));
}

static void bk7258_pm_ap_trace_mark(uint32_t cpu, uint32_t stage)
{
  volatile struct bk7258_pm_ap_core_trace_s *trace;

  if (cpu > BK7258_AP_LOGICAL_CPU1)
    {
      return;
    }

  trace = &g_bk7258_pm_ap_core_trace[cpu];
  if ((trace->stages & stage) == 0)
    {
      trace->stages |= stage;
      bk7258_pm_ap_trace_publish(cpu);
    }
}

static inline void bk7258_pm_shallow_wfi(void)
{
  irqstate_t flags = up_irq_save();

  modifyreg32(NVIC_SYSCON, NVIC_SYSCON_SLEEPDEEP, 0);
  up_irq_enable();
  __asm volatile ("dsb sy; wfi; isb sy" ::: "memory");
  up_irq_restore(flags);
}

static inline void bk7258_pm_ap_deep_sleep(void)
{
  uint8_t basepri = getbasepri();
  uint8_t primask = getprimask();

  /* The official AP FreeRTOS SMP port enters sys_hal_enter_normal_sleep()
   * with PRIMASK set and BASEPRI clear.  NuttX pm_idle() instead reaches the
   * board hook with BASEPRI raised.  STAR does not leave deep WFI for the
   * PWC mailbox interrupt while that interrupt is masked by BASEPRI, which
   * strands AP0 after CP has otherwise completed the low-voltage restore.
   *
   * Translate only the interrupt-mask representation at the WFI boundary,
   * exactly as the CP wrapper does for the vendor low-voltage leaf.  A
   * PRIMASK-masked mailbox request releases WFI without running the callback
   * inside this critical section; restoring the incoming NuttX masks lets
   * the callback run through the normal interrupt exit path afterwards.
   */

  setprimask(1);
  setbasepri(0);
  modifyreg32(NVIC_SYSCON, 0, NVIC_SYSCON_SLEEPDEEP);
  __asm volatile ("dsb sy; isb sy; wfi; isb sy" ::: "memory");

  setprimask(1);
  setbasepri(basepri);
  setprimask(primask);
  __asm volatile ("isb sy" ::: "memory");
}

static inline bool bk7258_pm_ap_pending(uint32_t cpu,
                                        uintptr_t status_low,
                                        uintptr_t status_high)
{
  volatile struct bk7258_pm_ap_core_trace_s *trace =
    &g_bk7258_pm_ap_core_trace[cpu];
  uint32_t dma = 0;
  uint32_t nvic0;
  uint32_t nvic1;
  uint32_t sys0;
  uint32_t sys1;
  uint32_t icsr;

  if (bk_dma_check_chn_status != NULL)
    {
      dma = bk_dma_check_chn_status();
    }

  nvic0 = getreg32(BK7258_NVIC_PENDING0);
  nvic1 = getreg32(BK7258_NVIC_PENDING1);
  sys0 = getreg32(status_low);
  sys1 = getreg32(status_high);
  icsr = getreg32(BK7258_NVIC_ICSR);
  g_bk7258_pm_ap_diag.last_nvic_pending[0] = nvic0;
  g_bk7258_pm_ap_diag.last_nvic_pending[1] = nvic1;
  g_bk7258_pm_ap_diag.last_sys_status[0] = sys0;
  g_bk7258_pm_ap_diag.last_sys_status[1] = sys1;
  g_bk7258_pm_ap_diag.last_dma = dma;
  g_bk7258_pm_ap_diag.last_icsr = icsr;
  trace->last_nvic_pending[0] = nvic0;
  trace->last_nvic_pending[1] = nvic1;
  trace->last_sys_status[0] = sys0;
  trace->last_sys_status[1] = sys1;
  trace->last_dma = dma;
  trace->last_icsr = icsr;

  return nvic0 != 0 || nvic1 != 0 || dma != 0 || sys0 != 0 ||
         sys1 != 0 || (icsr & BK7258_NVIC_ICSR_PENDSTSET) != 0;
}

static void bk7258_pm_aon_update(uint32_t set, uint32_t clear)
{
  irqstate_t flags;
  uint32_t value;

  /* Match the SDK bitfield setter with a local interrupt-protected RMW.  AP1
   * publishes its sleep vote before AP0 and AP0 withdraws its vote before
   * releasing AP1, so the two cores never update this device word at the
   * same time.  A shared RAM spinlock cannot provide that guarantee across
   * the AP cores' private, non-coherent caches.
   */

  flags = up_irq_save();
  value = getreg32(BK7258_AON_PMU_R3_ADDR);
  value &= ~clear;
  value |= set;
  putreg32(value, BK7258_AON_PMU_R3_ADDR);
  __asm volatile ("dmb sy" ::: "memory");
  up_irq_restore(flags);
}

static bool bk7258_pm_ap_wait_aon(uint32_t bit, bool asserted)
{
  unsigned int elapsed;

  for (elapsed = 0; elapsed < BK7258_PM_AP_PEER_WAIT_US; elapsed++)
    {
      bool state = (getreg32(BK7258_AON_PMU_R3_ADDR) & bit) != 0;

      if (state == asserted)
        {
          return true;
        }

      up_udelay(1);
    }

  return false;
}

static void bk7258_pm_ap_count_wake(uint32_t cpu)
{
  uintptr_t addr = BK7258_PWR_MNG_ADDR +
                   (cpu == BK7258_AP_LOGICAL_CPU0 ?
                    BK7258_PWR_WAKE_AP1_OFFSET :
                    BK7258_PWR_AP1_DEBUG_OFFSET);

  putreg32(getreg32(addr) + 1u, addr);
}

static void bk7258_pm_ap_deep_wfi(uint32_t cpu)
{
  volatile struct bk7258_pm_ap_core_trace_s *trace =
    &g_bk7258_pm_ap_core_trace[cpu];
  uintptr_t en_low;
  uintptr_t en_high;
  uintptr_t status_low;
  uintptr_t status_high;
  uint32_t aon_bit;
  uint32_t int_low;
  uint32_t int_high;
  uint32_t systick;
  unsigned int i;

  g_bk7258_pm_ap_diag.deep_entries[cpu]++;
  trace->deep_entries++;
  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_DEEP);

  if (cpu == BK7258_AP_LOGICAL_CPU0)
    {
      en_low = BK7258_SYS_CPU1_EN_LOW;
      en_high = BK7258_SYS_CPU1_EN_HIGH;
      status_low = BK7258_SYS_CPU1_STATUS_LOW;
      status_high = BK7258_SYS_CPU1_STATUS_HIGH;
      aon_bit = BK7258_AON_AP0_WFI_BIT;
    }
  else
    {
      en_low = BK7258_SYS_CPU2_EN_LOW;
      en_high = BK7258_SYS_CPU2_EN_HIGH;
      status_low = BK7258_SYS_CPU2_STATUS_LOW;
      status_high = BK7258_SYS_CPU2_STATUS_HIGH;
      aon_bit = BK7258_AON_AP1_WFI_BIT;
    }

  /* The SDK PWC mailbox used by CP wakes AP0.  FreeRTOS periodically brings
   * AP1 through its idle hook inside the official three-millisecond vote
   * window, but NuttX's ten-millisecond SysTick does not guarantee that.
   * Forward the vote edge through the existing SMP scheduler IPI before AP0
   * masks its interrupt sources so AP1 performs its own pending/DMA checks
   * and publishes its AON WFI acknowledgement in the same attempt.
   */

  if (cpu == BK7258_AP_LOGICAL_CPU0)
    {
      g_bk7258_pm_ap_diag.forward_attempts++;
      g_bk7258_pm_ap_diag.forward_last_ret =
        up_send_smp_sched(BK7258_AP_LOGICAL_CPU1);

      /* AON R3 is one device word updated by both AP cores through RMW
       * bitfield setters.  AP1 must publish first; otherwise simultaneous
       * AP0/AP1 writes can lose one WFI bit even though both cores entered
       * the wrapper.  This bounded wait remains inside CP's 3 ms deadline.
       */

      trace->peer_waits++;
      if (!bk7258_pm_ap_wait_aon(BK7258_AON_AP1_WFI_BIT, true))
        {
          trace->peer_timeouts++;
          bk7258_pm_ap_trace_mark(cpu,
                                  BK7258_PM_AP_TRACE_PEER_TIMEOUT);
          return;
        }

      bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_PEER_SEEN);
    }

  systick = getreg32(BK7258_SYSTICK_CTRL);
  putreg32(0, BK7258_SYSTICK_CTRL);
  int_low = getreg32(en_low);
  int_high = getreg32(en_high);
  putreg32(0, en_low);
  putreg32(0, en_high);

  for (i = 0; i < 5; i++)
    {
      __asm volatile ("nop");
    }

  if (bk7258_pm_ap_pending(cpu, status_low, status_high))
    {
      g_bk7258_pm_ap_diag.pending_rejects[cpu]++;
      trace->pending_rejects++;
      bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_PENDING_REJ);
      bk7258_pm_ap_diag_publish();
      putreg32(int_low, en_low);
      putreg32(int_high, en_high);
      putreg32(systick, BK7258_SYSTICK_CTRL);
      return;
    }

  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_PENDING_OK);

  if ((getreg32(BK7258_AON_PMU_R3_ADDR) &
       BK7258_AON_CP_SLEEP_VOTE_BIT) == 0)
    {
      g_bk7258_pm_ap_diag.vote_rejects[cpu]++;
      trace->vote_rejects++;
      bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_VOTE_REJ);
      bk7258_pm_ap_diag_publish();
      putreg32(int_low, en_low);
      putreg32(int_high, en_high);
      putreg32(systick, BK7258_SYSTICK_CTRL);
      return;
    }

  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_VOTE_OK);

  /* Match the official protocol: all normal SYS sources stay masked while
   * the core is in SLEEPDEEP; only the physical-core mailbox can wake it.
   */

  putreg32(BK7258_MAILBOX_WAKE_BIT, en_high);
  if (cpu == BK7258_AP_LOGICAL_CPU0 &&
      bk_pm_handle_lv_sleep_callback != NULL)
    {
      bk_pm_handle_lv_sleep_callback(0); /* PM_LV_ENTER_SLEEP */
    }

  bk7258_pm_aon_update(aon_bit, 0);
  g_bk7258_pm_ap_diag.aon_sets[cpu]++;
  trace->aon_sets++;
  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_AON_SET);
  bk7258_pm_ap_diag_publish();
  bk7258_pm_ap_count_wake(cpu);
  bk7258_pm_ap_deep_sleep();

  /* Follow the SDK resume order exactly: withdraw the local AON vote and
   * restore local timer/callback state before sending the peer RELEASE IPI.
   * Waiting for AP1 here is both unnecessary and unsafe because up_udelay()
   * has no guaranteed running timebase immediately after low-voltage wake.
   * Clearing AP0 first and only then releasing AP1 also serializes the two
   * AON R3 read-modify-write clears.
   */

  bk7258_pm_aon_update(0, aon_bit);
  g_bk7258_pm_ap_diag.wakeups[cpu]++;
  trace->wakeups++;
  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_WAKE);
  bk7258_pm_ap_diag_publish();

  putreg32(systick, BK7258_SYSTICK_CTRL);
  if (cpu == BK7258_AP_LOGICAL_CPU0 &&
      bk_pm_handle_lv_sleep_callback != NULL)
    {
      bk_pm_handle_lv_sleep_callback(1); /* PM_LV_EXIT_SLEEP */
    }

  /* The SDK uses vPortYieldCore() after restoring the local wake state.
   * NuttX's scheduler IPI is the equivalent explicit peer RELEASE edge.
   */

  if (cpu == BK7258_AP_LOGICAL_CPU0)
    {
      (void)up_send_smp_sched(BK7258_AP_LOGICAL_CPU1);
    }
  else
    {
      (void)up_send_smp_sched(BK7258_AP_LOGICAL_CPU0);
    }

  putreg32(int_low, en_low);
  putreg32(int_high, en_high);
}

static int bk7258_pm_ap_prepare(struct pm_callback_s *callback, int domain,
                                enum pm_state_e state)
{
  (void)callback;
  (void)domain;

  return state == PM_NORMAL || state == PM_IDLE ||
         state == PM_STANDBY || state == PM_RESTORE ? OK : -EBUSY;
}

static void bk7258_pm_ap_notify(struct pm_callback_s *callback, int domain,
                                enum pm_state_e state)
{
  (void)callback;
  (void)domain;
  (void)state;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool bk7258_pm_ap_runtime_ready(void)
{
  volatile struct bk7258_ap_boot_state_s *boot = bk7258_ap_boot_state();
  volatile struct bk7258_rptun_control_s *rptun =
    bk7258_rptun_control();
  uint32_t cpu = bk7258_pm_local_cpu();

  /* CPU1 can retain the cold-start copy of the cacheable shared boot/RPTUN
   * records after CPU0 reaches READY.  Do not invalidate those records here:
   * CPU1 may also own dirty heartbeat fields in the same cache lines.  The
   * CP sleep vote is an uncached hardware authorization and is published only
   * after CP has independently verified AP READY, RPTUN CONNECTED, idle votes,
   * mailbox quiescence and pending IRQ state.  It therefore also provides the
   * safe, immediate gate that brings CPU1 into this coordinated attempt.
   */

  if ((getreg32(BK7258_AON_PMU_R3_ADDR) &
       BK7258_AON_CP_SLEEP_VOTE_BIT) != 0)
    {
      bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_RUNTIME);
      return true;
    }

  __asm volatile ("dmb sy" ::: "memory");
  return boot->magic == BK7258_AP_BOOT_STATE_MAGIC &&
         boot->state == BK7258_AP_STATE_READY && boot->error == 0 &&
         rptun->magic == BK7258_RPTUN_CONTROL_MAGIC &&
         rptun->version == BK7258_RPTUN_CONTROL_VERSION &&
         rptun->size == sizeof(*rptun) &&
         rptun->generation == boot->generation &&
         rptun->state == BK7258_RPTUN_STATE_CONNECTED;
}

bool bk7258_pm_ap_ipi_kick(int cpu)
{
  volatile struct bk7258_pm_ap_core_trace_s *trace;
  uint32_t aon;
  unsigned int i;

  if (cpu != BK7258_AP_LOGICAL_CPU1)
    {
      return false;
    }

  trace = &g_bk7258_pm_ap_core_trace[cpu];
  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_IPI_ISR);
  aon = getreg32(BK7258_AON_PMU_R3_ADDR);

  /* A scheduler IPI from AP0 is the explicit RELEASE wake for AP1.  Clear
   * SLEEPONEXIT before returning from this ISR, withdraw AP1's AON WFI bit,
   * then restore the exact interrupt and SysTick state saved at PREPARE.
   */

  if (g_bk7258_pm_ap_irq_sleep.armed)
    {
      if ((aon & BK7258_AON_CP_SLEEP_VOTE_BIT) != 0)
        {
          bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_IPI_VOTE);
          return true;
        }

      modifyreg32(NVIC_SYSCON,
                  NVIC_SYSCON_SLEEPDEEP | NVIC_SYSCON_SLEEPONEXIT, 0);
      bk7258_pm_aon_update(0, BK7258_AON_AP1_WFI_BIT);
      g_bk7258_pm_ap_diag.wakeups[cpu]++;
      trace->wakeups++;
      bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_WAKE);
      putreg32(g_bk7258_pm_ap_irq_sleep.int_low,
               BK7258_SYS_CPU2_EN_LOW);
      putreg32(g_bk7258_pm_ap_irq_sleep.int_high,
               BK7258_SYS_CPU2_EN_HIGH);
      putreg32(g_bk7258_pm_ap_irq_sleep.systick, BK7258_SYSTICK_CTRL);
      g_bk7258_pm_ap_irq_sleep.armed = false;
      bk7258_pm_ap_diag_publish();
      __asm volatile ("dsb sy; isb sy" ::: "memory");
      return true;
    }

  if ((aon & BK7258_AON_CP_SLEEP_VOTE_BIT) == 0)
    {
      return false;
    }

  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_IPI_VOTE);
  g_bk7258_pm_ap_diag.deep_entries[cpu]++;
  trace->deep_entries++;
  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_DEEP);

  g_bk7258_pm_ap_irq_sleep.systick = getreg32(BK7258_SYSTICK_CTRL);
  g_bk7258_pm_ap_irq_sleep.int_low =
    getreg32(BK7258_SYS_CPU2_EN_LOW);
  g_bk7258_pm_ap_irq_sleep.int_high =
    getreg32(BK7258_SYS_CPU2_EN_HIGH);
  putreg32(0, BK7258_SYSTICK_CTRL);
  putreg32(0, BK7258_SYS_CPU2_EN_LOW);
  putreg32(0, BK7258_SYS_CPU2_EN_HIGH);

  for (i = 0; i < 5; i++)
    {
      __asm volatile ("nop");
    }

  if (bk7258_pm_ap_pending(cpu, BK7258_SYS_CPU2_STATUS_LOW,
                          BK7258_SYS_CPU2_STATUS_HIGH))
    {
      g_bk7258_pm_ap_diag.pending_rejects[cpu]++;
      trace->pending_rejects++;
      bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_PENDING_REJ);
      putreg32(g_bk7258_pm_ap_irq_sleep.int_low,
               BK7258_SYS_CPU2_EN_LOW);
      putreg32(g_bk7258_pm_ap_irq_sleep.int_high,
               BK7258_SYS_CPU2_EN_HIGH);
      putreg32(g_bk7258_pm_ap_irq_sleep.systick, BK7258_SYSTICK_CTRL);
      bk7258_pm_ap_diag_publish();
      return true;
    }

  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_PENDING_OK);
  if ((getreg32(BK7258_AON_PMU_R3_ADDR) &
       BK7258_AON_CP_SLEEP_VOTE_BIT) == 0)
    {
      g_bk7258_pm_ap_diag.vote_rejects[cpu]++;
      trace->vote_rejects++;
      bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_VOTE_REJ);
      putreg32(g_bk7258_pm_ap_irq_sleep.int_low,
               BK7258_SYS_CPU2_EN_LOW);
      putreg32(g_bk7258_pm_ap_irq_sleep.int_high,
               BK7258_SYS_CPU2_EN_HIGH);
      putreg32(g_bk7258_pm_ap_irq_sleep.systick, BK7258_SYSTICK_CTRL);
      bk7258_pm_ap_diag_publish();
      return true;
    }

  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_VOTE_OK);
  putreg32(BK7258_MAILBOX_WAKE_BIT, BK7258_SYS_CPU2_EN_HIGH);
  bk7258_pm_aon_update(BK7258_AON_AP1_WFI_BIT, 0);
  g_bk7258_pm_ap_diag.aon_sets[cpu]++;
  trace->aon_sets++;
  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_AON_SET);
  bk7258_pm_ap_count_wake(cpu);
  g_bk7258_pm_ap_irq_sleep.armed = true;
  modifyreg32(NVIC_SYSCON, 0,
              NVIC_SYSCON_SLEEPDEEP | NVIC_SYSCON_SLEEPONEXIT);
  bk7258_pm_ap_diag_publish();
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  return true;
}

void bk7258_pm_ap_idle(void)
{
  uint32_t cpu = bk7258_pm_local_cpu();

  if (cpu > BK7258_AP_LOGICAL_CPU1)
    {
      bk7258_pm_shallow_wfi();
      return;
    }

  g_bk7258_pm_ap_diag.idle_entries[cpu]++;
  g_bk7258_pm_ap_core_trace[cpu].idle_entries++;
  bk7258_pm_ap_trace_mark(cpu, BK7258_PM_AP_TRACE_IDLE);

  if ((getreg32(BK7258_AON_PMU_R3_ADDR) &
       BK7258_AON_CP_SLEEP_VOTE_BIT) == 0)
    {
      bk7258_pm_shallow_wfi();

      /* CP and the AP0 forwarding IPI may have raised the coordinated-sleep
       * vote while this core was inside shallow WFI.  Consume that edge in
       * the same idle invocation; returning to NuttX first can defer AP1's
       * next vote check until its ten-millisecond SysTick, outside the SDK's
       * three-millisecond acknowledgement window.
       */

      if ((getreg32(BK7258_AON_PMU_R3_ADDR) &
           BK7258_AON_CP_SLEEP_VOTE_BIT) == 0)
        {
          return;
        }

      g_bk7258_pm_ap_diag.shallow_vote_wakes[cpu]++;
    }

  /* A shared AON vote can become visible just before the board mailbox edge
   * is delivered.  AP0 must not mask mailbox interrupts and enter deep WFI
   * until the NuttX-owned logical channel has consumed and ACKed the PREPARE
   * command; otherwise CP cannot send the RELEASE edge on an AP1 timeout.
   */

  if (cpu == BK7258_AP_LOGICAL_CPU0 &&
      !bk7258_rptun_mbox_pm_prepare_take())
    {
      g_bk7258_pm_ap_diag.prepare_waits++;

      /* Return through pm_idle() so sched_unlock() can run the pinned
       * mailbox worker.  A second WFI here would keep that worker ready but
       * unscheduled while CP's bounded acknowledgement window expires.
       */

      return;
    }

  if (cpu == BK7258_AP_LOGICAL_CPU0)
    {
      g_bk7258_pm_ap_diag.prepare_takes++;
    }

  bk7258_pm_ap_deep_wfi(cpu);
}

void arm_pminitialize(void)
{
  static struct pm_callback_s callback =
    {
      .prepare = bk7258_pm_ap_prepare,
      .notify = bk7258_pm_ap_notify,
    };
  unsigned int domain;

  memset((void *)(uintptr_t)&g_bk7258_pm_ap_diag, 0,
         sizeof(g_bk7258_pm_ap_diag));
  g_bk7258_pm_ap_diag.magic = BK7258_PM_AP_DIAG_MAGIC;
  g_bk7258_pm_ap_diag.version = BK7258_PM_AP_DIAG_VERSION;
  g_bk7258_pm_ap_diag.size = sizeof(g_bk7258_pm_ap_diag);
  bk7258_pm_ap_diag_publish();

  memset((void *)(uintptr_t)&g_bk7258_pm_ap_core_trace, 0,
         sizeof(g_bk7258_pm_ap_core_trace));
  for (domain = 0; domain < 2; domain++)
    {
      g_bk7258_pm_ap_core_trace[domain].magic =
        BK7258_PM_AP_TRACE_MAGIC;
      g_bk7258_pm_ap_core_trace[domain].version =
        BK7258_PM_AP_TRACE_VERSION;
      g_bk7258_pm_ap_core_trace[domain].size =
        sizeof(g_bk7258_pm_ap_core_trace[domain]);
      bk7258_pm_ap_trace_publish(domain);
    }

  pm_initialize();

  /* The greedy governor returns the first locked state.  Lock every SMP
   * domain at STANDBY so neither per-CPU nor system-domain policy can ever
   * select PM_SLEEP.
   */

  for (domain = 0; domain < CONFIG_PM_NDOMAINS; domain++)
    {
      pm_stay(domain, PM_STANDBY);
    }

  (void)pm_register(&callback);
}

#endif /* CONFIG_BK7258_PM_COORDINATED_STANDBY */
