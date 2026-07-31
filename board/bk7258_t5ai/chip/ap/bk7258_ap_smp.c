/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/
 * bk7258_ap_smp.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N8-B1 through N8-C4 physical CPU2 secondary bootstrap.  N8-C1 enters the
 * logical CPU1 scheduler IDLE path through the SDK-wrapped mailbox IPI data
 * plane.  N8-C2 remote-dispatches exactly one CPU1-affinity pthread; N8-C3
 * proves its first semaphore block/wake and N8-C4 reuses that same task and
 * semaphore for a fixed eight-cycle wake loop.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP

#include <errno.h>
#ifdef CONFIG_BK7258_AP_SMP_CPU1_AFFINITY
#  include <pthread.h>
#endif
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
#  include <nuttx/irq.h>
#  include <nuttx/semaphore.h>
#endif
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
#  include <nuttx/sched_note.h>
#endif

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/irq.h>

#include "arm_internal.h"
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
#  include "init/init.h"
#  include "nvic.h"
#  include "sched/sched.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if !defined(CONFIG_SMP)
#  error CONFIG_BK7258_AP_SMP_BOOTSTRAP requires CONFIG_SMP
#endif

#if CONFIG_SMP_NCPUS != 2
#  error N8-B supports exactly two AP logical CPUs
#endif

#if CONFIG_SMP_DEFAULT_CPUSET != 0x1
#  error N8-C1/N8-C2/N8-C3/N8-C4 must keep ordinary tasks on AP logical CPU0
#endif

#if defined(CONFIG_BK7258_AP_SMP_SCHED_ONLINE) && \
    !defined(CONFIG_BK7258_AP_IPI)
#  error BK7258 scheduler-online mode requires the SDK-wrapped AP IPI
#endif

#if defined(CONFIG_BK7258_AP_SMP_CPU1_AFFINITY) && \
    !defined(CONFIG_BK7258_AP_SMP_SCHED_ONLINE)
#  error BK7258 CPU1 affinity gate requires scheduler-online mode
#endif

#if defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE) && \
    !defined(CONFIG_BK7258_AP_SMP_CPU1_AFFINITY)
#  error BK7258 CPU1 semaphore-wake gate requires the affinity gate
#endif

#if defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP) && \
    !defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE)
#  error BK7258 CPU1 semaphore-wake loop requires the N8-C3 wake gate
#endif

#define BK7258_SCB_VTOR             (*(volatile uint32_t *)0xe000ed08u)
#define BK7258_SCB_CCR              (*(volatile uint32_t *)0xe000ed14u)
#define BK7258_SCB_CFSR             (*(volatile uint32_t *)0xe000ed28u)
#define BK7258_SCB_HFSR             (*(volatile uint32_t *)0xe000ed2cu)
#define BK7258_SCB_CPACR            (*(volatile uint32_t *)0xe000ed88u)
#define BK7258_MPU_CTRL             (*(volatile uint32_t *)0xe000ed94u)
#define BK7258_FPU_FPCCR            (*(volatile uint32_t *)0xe000ef34u)
#define BK7258_SYSTICK_CTRL          (*(volatile uint32_t *)0xe000e010u)
#define BK7258_SYSTICK_RELOAD        (*(volatile uint32_t *)0xe000e014u)
#define BK7258_SYSTICK_CURRENT       (*(volatile uint32_t *)0xe000e018u)
#define BK7258_SYSTICK_CTRL_ENABLE   (1u << 0)
#define BK7258_AP_PRIMARY_CPU       0
#define BK7258_AP_SECONDARY_CPU     1
#define BK7258_CPU2_LOCAL_CORE_ID   1u
#define BK7258_CPU2_PHYSICAL_ID     2u
#define BK7258_CPU2_VECTOR_COUNT    80u
#define BK7258_CPU2_FRAME_WORDS     8u
#define BK7258_CPU2_INVALID_VALUE   UINT32_MAX
#define BK7258_AP_PRIMARY_MASK      (1u << BK7258_AP_PRIMARY_CPU)
#define BK7258_AP_ONLINE_MASK       ((1u << BK7258_AP_PRIMARY_CPU) | \
                                     (1u << BK7258_AP_SECONDARY_CPU))
#define BK7258_CPU2_ONLINE_VECTOR_XOR (1u << 8)
#define BK7258_CPU2_IDLE_VECTOR_XOR   (1u << 9)
#define BK7258_CPU2_UNLOCK_VECTOR_XOR (1u << 10)

/****************************************************************************
 * External Function Prototypes / Data
 ****************************************************************************/

extern void exception_common(void);
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
extern void __real_nx_bringup(void);
#endif
extern void bk7258_ap_smp_memory_initialize(void);
extern const void *const
  __vector_core1_table[BK7258_CPU2_VECTOR_COUNT];

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
static struct smp_call_data_s g_bk7258_ap_smp_forward_call;
static struct smp_call_data_s g_bk7258_ap_smp_reverse_call;
#endif

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
static sem_t g_bk7258_ap_cpu1_sem;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_cpu2_fpu_initialize(void)
{
  /* Match CPU0's reset contract before touching any cross-core state. */

  bk7258_ap_smp_memory_initialize();

  /* Match the primary AP core before CPU2 can enter exception_common. */

  BK7258_SCB_CPACR &= ~((3u << 20) | (3u << 22));
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  BK7258_FPU_FPCCR &= ~((1u << 31) | (1u << 30) | (1u << 29));
  BK7258_SCB_CPACR |= ((3u << 20) | (3u << 22));
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

static inline volatile uint32_t *bk7258_cpu2_control(void)
{
  return (volatile uint32_t *)(uintptr_t)BK7258_SYS_CPU2_CONTROL;
}

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
static void bk7258_cpu2_signal_boot_ready(void)
{
  volatile uint32_t *control = bk7258_cpu2_control();
  uint32_t value = *control;

  /* Tell CPU0 that CPU2 has completed all bootstrap work which must run
   * before up_cpu_start() releases NuttX's scheduler critical section.
   * Entering the secondary scheduler first would deadlock: CPU0 waits for
   * CPU2 while arm_initialize_stack()/this_cpu() waits for CPU0.
   */

  value &= ~BK7258_SYS_CPU2_BOOT_MASK;
  value |=
    ((uint32_t)(uintptr_t)__vector_core1_table &
     BK7258_SYS_CPU2_BOOT_MASK) ^ BK7258_CPU2_ONLINE_VECTOR_XOR;
  *control = value;
  __asm volatile ("dsb sy; sev" ::: "memory");
}

static void bk7258_cpu2_wait_idle_release(void)
{
  volatile uint32_t *control = bk7258_cpu2_control();
  uint32_t expected_vector =
    (uint32_t)(uintptr_t)__vector_core1_table &
    BK7258_SYS_CPU2_BOOT_MASK;
  uint32_t idle_vector =
    expected_vector ^ BK7258_CPU2_IDLE_VECTOR_XOR;
  uint32_t value;

  /* CPU0 publishes this token only after nx_bringup() has returned.  Use the
   * uncached system-control register for the handoff because a CPU-only reset
   * can leave each STAR core with a different private copy of NuttX's global
   * initialization state.
   */

  for (;;)
    {
      value = *control;
      if ((value & BK7258_SYS_CPU2_BOOT_MASK) == idle_vector)
        {
          break;
        }

      __asm volatile ("wfe" ::: "memory");
    }

  value &= ~BK7258_SYS_CPU2_BOOT_MASK;
  value |= expected_vector;
  *control = value;
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

static void bk7258_cpu2_signal_idle_release(void)
{
  volatile uint32_t *control = bk7258_cpu2_control();
  uint32_t value = *control;

  value &= ~BK7258_SYS_CPU2_BOOT_MASK;
  value |=
    ((uint32_t)(uintptr_t)__vector_core1_table &
     BK7258_SYS_CPU2_BOOT_MASK) ^ BK7258_CPU2_IDLE_VECTOR_XOR;
  *control = value;
  __asm volatile ("dsb sy; sev" ::: "memory");
}

static void bk7258_cpu2_signal_scheduler_unlocked(void)
{
  volatile uint32_t *control = bk7258_cpu2_control();
  uint32_t expected_vector =
    (uint32_t)(uintptr_t)__vector_core1_table &
    BK7258_SYS_CPU2_BOOT_MASK;
  uint32_t value = *control;

  value &= ~BK7258_SYS_CPU2_BOOT_MASK;
  value |= expected_vector ^ BK7258_CPU2_UNLOCK_VECTOR_XOR;
  *control = value;
  __asm volatile ("dsb sy; sev" ::: "memory");

  /* CPU0 clears the token after it has left the nx_bringup() wrapper.
   * Keep CPU2's interrupts disabled until that acknowledgement arrives.
   * Otherwise sched_unlock() may send a scheduler IPI which switches CPU0
   * away while it is still waiting for this token.
   */

  for (;;)
    {
      value = *control;
      if ((value & BK7258_SYS_CPU2_BOOT_MASK) == expected_vector)
        {
          break;
        }

      __asm volatile ("wfe" ::: "memory");
    }

  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

static void bk7258_cpu2_wait_scheduler_unlocked(void)
{
  volatile uint32_t *control = bk7258_cpu2_control();
  uint32_t expected_vector =
    (uint32_t)(uintptr_t)__vector_core1_table &
    BK7258_SYS_CPU2_BOOT_MASK;
  uint32_t unlocked_vector =
    expected_vector ^ BK7258_CPU2_UNLOCK_VECTOR_XOR;
  uint32_t value;

  /* Do not let CPU0 and CPU2 release their startup scheduler locks at the
   * same time.  On a cold CPU-only reset, the private data caches can make
   * that lock handoff invisible even though both cores execute the normal
   * NuttX unlock path.
   */

  for (;;)
    {
      value = *control;
      if ((value & BK7258_SYS_CPU2_BOOT_MASK) == unlocked_vector)
        {
          break;
        }

      __asm volatile ("wfe" ::: "memory");
    }

  value &= ~BK7258_SYS_CPU2_BOOT_MASK;
  value |= expected_vector;
  *control = value;
  __asm volatile ("dsb sy; isb sy" ::: "memory");

}
#endif

static void bk7258_cpu2_hold_reset(
  volatile struct bk7258_cpu2_probe_state_s *state)
{
  volatile uint32_t *control = bk7258_cpu2_control();
  uint32_t value = *control;

  value &= ~BK7258_SYS_CPU2_RESET;
  *control = value;
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  if (state != NULL)
    {
      state->control_after = value;
      __asm volatile ("dmb sy" ::: "memory");
    }
}

static int bk7258_cpu2_wait(uint32_t wanted, uint32_t timeout_ms)
{
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  volatile uint32_t *control = bk7258_cpu2_control();
  uint32_t expected_vector =
    (uint32_t)(uintptr_t)__vector_core1_table &
    BK7258_SYS_CPU2_BOOT_MASK;
  uint32_t online_vector =
    expected_vector ^ BK7258_CPU2_ONLINE_VECTOR_XOR;
  uint32_t sys_control;

  (void)wanted;
#else
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  uint32_t current;
#endif
  uint64_t elapsed_cycles = 0;
  uint64_t timeout_cycles;
  uint32_t period;
  uint32_t last;
  uint32_t now;

  /* nx_start() calls this while CPU0 owns the scheduler critical section and
   * CPU2 is becoming online.  NuttX clock access reaches up_irq_save(), which
   * can try to acquire the same SMP scheduler lock.  Scheduler sleeps are not
   * available either.  Read the already-running SysTick counter directly so
   * this bootstrap wait depends on neither facility nor delay-loop clock
   * calibration left by the loader.
   */

  period = BK7258_SYSTICK_RELOAD + 1u;
  if ((BK7258_SYSTICK_CTRL & BK7258_SYSTICK_CTRL_ENABLE) == 0 ||
      period == 0)
    {
      return -ETIMEDOUT;
    }

  timeout_cycles = (uint64_t)period * MSEC2TICK(timeout_ms);
  last = BK7258_SYSTICK_CURRENT;

  for (;;)
    {
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
      /* A cold CPU-only reset can retain a CPU0 cache line even after the
       * architectural cache and MPU controls have been changed.  CPU2
       * therefore acknowledges scheduler entry through its uncached system
       * control register.  The boot-vector field only takes effect on a
       * future CPU2 reset, so one bit is a safe transient online token.
       */

      sys_control = *control;
      if ((sys_control & BK7258_SYS_CPU2_BOOT_MASK) == online_vector)
        {
          sys_control &= ~BK7258_SYS_CPU2_BOOT_MASK;
          sys_control |= expected_vector;
          *control = sys_control;
          __asm volatile ("dsb sy; isb sy" ::: "memory");
          return OK;
        }
#else
      current = state->state;
      __asm volatile ("dmb sy" ::: "memory");
      if (current == wanted)
        {
          return OK;
        }

      if (current == BK7258_CPU2_PROBE_STATE_FAILED)
        {
          return -EIO;
        }
#endif

      now = BK7258_SYSTICK_CURRENT;
      if (last >= now)
        {
          elapsed_cycles += last - now;
        }
      else
        {
          elapsed_cycles += last + (period - now);
        }

      last = now;
      if (elapsed_cycles >= timeout_cycles)
        {
          break;
        }
    }

  return -ETIMEDOUT;
}

static void bk7258_cpu2_fail(uint32_t error)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();

  state->secondary_ready = 0;
  state->error = error;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_CPU2_PROBE_STATE_FAILED;
  __asm volatile ("dmb sy" ::: "memory");
}

#ifndef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
static void bk7258_cpu2_note_missing_ipi(void)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();

  if (state->magic == BK7258_CPU2_PROBE_STATE_MAGIC &&
      state->version == BK7258_CPU2_PROBE_STATE_VERSION)
    {
      state->smp_call_requests++;
      if (state->error == BK7258_CPU2_PROBE_ERROR_NONE)
        {
          state->error = BK7258_CPU2_PROBE_ERROR_IPI_UNAVAILABLE;
        }

      __asm volatile ("dmb sy; sev" ::: "memory");
    }
}
#endif

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
static int bk7258_cpu2_scheduler_irq_initialize(void)
{
  int ret;

  ret = up_prioritize_irq(NVIC_IRQ_PENDSV, NVIC_SYSH_PRIORITY_MIN);
  if (ret < 0)
    {
      return ret;
    }

  ret = up_prioritize_irq(NVIC_IRQ_SVCALL, NVIC_SYSH_SVCALL_PRIORITY);
  if (ret < 0)
    {
      return ret;
    }

  __asm volatile ("dsb sy; isb sy" ::: "memory");
  return OK;
}

static int bk7258_ap_smp_primary_callback(FAR void *arg)
{
  volatile struct bk7258_ap_smp_state_s *state = arg;

  if (state == NULL || up_cpu_index() != BK7258_AP_PRIMARY_CPU)
    {
      return -EINVAL;
    }

  state->callback_count[BK7258_AP_PRIMARY_CPU]++;
  state->last_callback_cpu = BK7258_AP_PRIMARY_CPU;
  __asm volatile ("dmb sy" ::: "memory");
  return OK;
}

static int bk7258_ap_smp_secondary_callback(FAR void *arg)
{
  volatile struct bk7258_ap_smp_state_s *state = arg;

  if (state == NULL || up_cpu_index() != BK7258_AP_SECONDARY_CPU)
    {
      return -EINVAL;
    }

  state->callback_count[BK7258_AP_SECONDARY_CPU]++;
  state->last_callback_cpu = BK7258_AP_SECONDARY_CPU;
  __asm volatile ("dmb sy" ::: "memory");

  nxsched_smp_call_init(&g_bk7258_ap_smp_reverse_call,
                        bk7258_ap_smp_primary_callback,
                        (FAR void *)state);
  return nxsched_smp_call_single_async(BK7258_AP_PRIMARY_CPU,
                                      &g_bk7258_ap_smp_reverse_call);
}

#ifdef CONFIG_BK7258_AP_SMP_CPU1_AFFINITY
static void bk7258_ap_affinity_fail(
  volatile struct bk7258_ap_affinity_state_s *state, uint32_t error)
{
  if (state->error == BK7258_AP_AFFINITY_ERROR_NONE)
    {
      state->error = error;
    }

  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_AFFINITY_STATE_FAILED;
  __asm volatile ("dmb sy; sev" ::: "memory");
}

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
static void bk7258_ap_sem_wake_fail(
  volatile struct bk7258_ap_sem_wake_state_s *state, uint32_t error)
{
  if (state->error == BK7258_AP_SEM_WAKE_ERROR_NONE)
    {
      state->error = error;
    }

  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_SEM_WAKE_STATE_FAILED;
  __asm volatile ("dmb sy; sev" ::: "memory");
}

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
static void bk7258_ap_sem_wake_loop_fail(
  volatile struct bk7258_ap_sem_wake_loop_state_s *state,
  uint32_t error)
{
  if (state->error == BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE)
    {
      state->error = error;
    }

  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_SEM_WAKE_LOOP_STATE_FAILED;
  __asm volatile ("dmb sy; sev" ::: "memory");
}

static uint32_t bk7258_ap_sem_wake_loop_sem_error(uint32_t error)
{
  switch (error)
    {
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_WAIT_TIMEOUT:
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_WAKE_TIMEOUT:
        return BK7258_AP_SEM_WAKE_ERROR_WAIT_TIMEOUT;
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_SEM_WAIT:
        return BK7258_AP_SEM_WAKE_ERROR_SEM_WAIT;
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_SEM_POST:
        return BK7258_AP_SEM_WAKE_ERROR_SEM_POST;
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_CPU:
        return BK7258_AP_SEM_WAKE_ERROR_BAD_CPU;
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_COUNT_MISMATCH:
        return BK7258_AP_SEM_WAKE_ERROR_COUNT_MISMATCH;
      default:
        return BK7258_AP_SEM_WAKE_ERROR_BAD_STATE;
    }
}

static uint32_t bk7258_ap_sem_wake_loop_affinity_error(uint32_t error)
{
  switch (error)
    {
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_WAIT_TIMEOUT:
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_WAKE_TIMEOUT:
        return BK7258_AP_AFFINITY_ERROR_TIMEOUT;
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_CPU:
        return BK7258_AP_AFFINITY_ERROR_BAD_CPU;
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_SEQUENCE:
      case BK7258_AP_SEM_WAKE_LOOP_ERROR_COUNT_MISMATCH:
        return BK7258_AP_AFFINITY_ERROR_COUNT_MISMATCH;
      default:
        return BK7258_AP_AFFINITY_ERROR_BAD_STATE;
    }
}
#endif

static int32_t bk7258_ap_sem_exact_waiter_value(
  pthread_t thread, cpu_set_t cpuset)
{
  FAR struct tcb_s *tcb;
  irqstate_t flags;
  int32_t observed = INT32_MAX;
  int value = 0;
  int ret;

  tcb = nxsched_get_tcb((pid_t)thread);
  if (tcb == NULL)
    {
      return observed;
    }

  flags = enter_critical_section();
  ret = nxsem_get_value(&g_bk7258_ap_cpu1_sem, &value);
  if (ret >= 0 &&
      tcb->task_state == TSTATE_WAIT_SEM &&
      tcb->waitobj == (FAR void *)&g_bk7258_ap_cpu1_sem &&
      value == -1 && tcb->affinity == cpuset)
    {
      observed = (int32_t)value;
    }

  leave_critical_section(flags);
  nxsched_put_tcb(tcb);
  __asm volatile ("dmb sy" ::: "memory");
  return observed;
}

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
static void bk7258_ap_sem_wake_loop_task_fail(
  volatile struct bk7258_ap_affinity_state_s *affinity,
  volatile struct bk7258_ap_sem_wake_state_s *sem_state,
  volatile struct bk7258_ap_sem_wake_loop_state_s *loop_state,
  uint32_t error, uint32_t cycle)
{
  bk7258_ap_sem_wake_loop_fail(loop_state, error);
  if (cycle == 1 && loop_state->completed_cycles == 0)
    {
      bk7258_ap_sem_wake_fail(
        sem_state, bk7258_ap_sem_wake_loop_sem_error(error));
    }

  bk7258_ap_affinity_fail(
    affinity, bk7258_ap_sem_wake_loop_affinity_error(error));
  __asm volatile ("dmb sy; sev" ::: "memory");
}

static int bk7258_ap_sem_wake_loop_abort(
  volatile struct bk7258_ap_affinity_state_s *affinity,
  volatile struct bk7258_ap_sem_wake_state_s *sem_state,
  volatile struct bk7258_ap_sem_wake_loop_state_s *loop_state,
  uint32_t error, int first_cycle_completed, int sem_initialized,
  int ret)
{
  bk7258_ap_sem_wake_loop_fail(loop_state, error);
  if (!first_cycle_completed)
    {
      bk7258_ap_sem_wake_fail(
        sem_state, bk7258_ap_sem_wake_loop_sem_error(error));
    }

  bk7258_ap_affinity_fail(
    affinity, bk7258_ap_sem_wake_loop_affinity_error(error));
  if (sem_initialized && affinity->task_completed == 0)
    {
      (void)nxsem_post(&g_bk7258_ap_cpu1_sem);
    }

  __asm volatile ("dmb sy; sev" ::: "memory");
  return ret;
}
#endif

/* Do not take a TCB reference here.  If CPU1 is already in
 * nxsched_release_pid(), CPU0 releasing that reference can wake exit_sem and
 * add an extra scheduler IPI to the N8-C3/N8-C4 measurement window.
 */

static int bk7258_ap_pid_released(pthread_t thread)
{
  pid_t pid = (pid_t)thread;
  irqstate_t flags;
  int released = 1;
  int hash_ndx;

  flags = spin_lock_irqsave_notrace(&g_pidhashlock);
  if (g_pidhash != NULL && g_npidhash > 0 && pid >= 0)
    {
      hash_ndx = PIDHASH(pid);
      if (g_pidhash[hash_ndx] != NULL &&
          g_pidhash[hash_ndx]->pid == pid)
        {
          released = 0;
        }
    }

  spin_unlock_irqrestore_notrace(&g_pidhashlock, flags);
  return released;
}
#endif

static FAR void *bk7258_ap_cpu1_affinity_task(FAR void *arg)
{
  volatile struct bk7258_ap_affinity_state_s *state = arg;
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
  volatile struct bk7258_ap_sem_wake_state_s *sem_state =
    bk7258_ap_sem_wake_state();
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  volatile struct bk7258_ap_sem_wake_loop_state_s *loop_state =
    bk7258_ap_sem_wake_loop_state();
  uint32_t cycle;
  uint32_t elapsed;
  uint32_t loop_wait_state;
  uint32_t sem_wait_state;
  uint32_t post_sequence;
  uint32_t published_error;
  uint32_t published_sem_error;
  int continued;
#  else
  uint32_t wait_state;
#  endif
#endif
  cpu_set_t observed = 0;
  pthread_t self = pthread_self();
  int ret;

  if (state == NULL)
    {
      return NULL;
    }

  state->task_id = (uint32_t)self;
  state->task_cpu = (uint32_t)up_cpu_index();
  state->task_started++;
  __asm volatile ("dmb sy" ::: "memory");

  if (state->magic != BK7258_AP_AFFINITY_STATE_MAGIC ||
      state->version != BK7258_AP_AFFINITY_STATE_VERSION ||
      state->size != sizeof(struct bk7258_ap_affinity_state_s) ||
      state->generation != bk7258_ap_boot_state()->generation ||
      state->state != BK7258_AP_AFFINITY_STATE_DISPATCHING)
    {
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_STATE);
      goto done;
    }

  state->state = BK7258_AP_AFFINITY_STATE_RUNNING;
  __asm volatile ("dmb sy" ::: "memory");

  ret = pthread_getaffinity_np(self, sizeof(observed), &observed);
  state->observed_mask = (uint32_t)observed;

  if (ret != 0)
    {
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_MASK);
    }
  else if (state->task_cpu != BK7258_AP_SECONDARY_CPU)
    {
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_CPU);
    }
  else if (state->observed_mask != state->requested_mask)
    {
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_MASK);
    }
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  else if (sem_state->magic != BK7258_AP_SEM_WAKE_STATE_MAGIC ||
           sem_state->version != BK7258_AP_SEM_WAKE_STATE_VERSION ||
           sem_state->size != sizeof(struct bk7258_ap_sem_wake_state_s) ||
           sem_state->generation != state->generation ||
           sem_state->state != BK7258_AP_SEM_WAKE_STATE_INITIALIZING ||
           sem_state->error != BK7258_AP_SEM_WAKE_ERROR_NONE ||
           loop_state->magic != BK7258_AP_SEM_WAKE_LOOP_STATE_MAGIC ||
           loop_state->version != BK7258_AP_SEM_WAKE_LOOP_STATE_VERSION ||
           loop_state->size !=
             sizeof(struct bk7258_ap_sem_wake_loop_state_s) ||
           loop_state->generation != state->generation ||
           loop_state->state !=
             BK7258_AP_SEM_WAKE_LOOP_STATE_INITIALIZING ||
           loop_state->error != BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE ||
           loop_state->requested_cycles !=
             BK7258_AP_SEM_WAKE_LOOP_CYCLES)
    {
      bk7258_ap_sem_wake_loop_task_fail(
        state, sem_state, loop_state,
        BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE, 1);
    }
  else
    {
      sem_state->task_id = (uint32_t)self;
      for (cycle = 1; cycle <= BK7258_AP_SEM_WAKE_LOOP_CYCLES;
           cycle++)
        {
          __asm volatile ("dmb sy" ::: "memory");
          if (loop_state->state !=
                (cycle == 1 ?
                   BK7258_AP_SEM_WAKE_LOOP_STATE_INITIALIZING :
                   BK7258_AP_SEM_WAKE_LOOP_STATE_CONTINUE) ||
              loop_state->completed_cycles != cycle - 1 ||
              loop_state->wait_entered != cycle - 1 ||
              loop_state->waiter_observed != cycle - 1 ||
              loop_state->post_count != cycle - 1 ||
              loop_state->wait_returned != cycle - 1)
            {
              bk7258_ap_sem_wake_loop_task_fail(
                state, sem_state, loop_state,
                BK7258_AP_SEM_WAKE_LOOP_ERROR_SEQUENCE, cycle);
              goto done;
            }

          loop_state->wait_sequence = cycle;
          loop_state->wait_entered++;
          if (cycle == 1)
            {
              sem_state->wait_entered = 1;
              sem_state->state = BK7258_AP_SEM_WAKE_STATE_WAITING;
            }

          __asm volatile ("dmb sy" ::: "memory");
          loop_state->state = BK7258_AP_SEM_WAKE_LOOP_STATE_WAITING;
          __asm volatile ("dmb sy; sev" ::: "memory");

          ret = nxsem_wait_uninterruptible(&g_bk7258_ap_cpu1_sem);
          __asm volatile ("dmb sy" ::: "memory");
          loop_wait_state = loop_state->state;
          post_sequence = loop_state->post_sequence;
          sem_wait_state = cycle == 1 ? sem_state->state :
            BK7258_AP_SEM_WAKE_STATE_POSTED;
          loop_state->wait_result = (int32_t)ret;
          loop_state->wake_cpu = (uint32_t)up_cpu_index();
          loop_state->wait_returned++;
          if (cycle == 1)
            {
              sem_state->wait_result = (int32_t)ret;
              sem_state->wait_returned = 1;
              sem_state->wake_cpu = loop_state->wake_cpu;
            }

          __asm volatile ("dmb sy" ::: "memory");
          if (ret < 0)
            {
              bk7258_ap_sem_wake_loop_task_fail(
                state, sem_state, loop_state,
                BK7258_AP_SEM_WAKE_LOOP_ERROR_SEM_WAIT, cycle);
              goto done;
            }
          else if (loop_wait_state !=
                     BK7258_AP_SEM_WAKE_LOOP_STATE_POSTED ||
                   sem_wait_state != BK7258_AP_SEM_WAKE_STATE_POSTED)
            {
              bk7258_ap_sem_wake_loop_task_fail(
                state, sem_state, loop_state,
                BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE, cycle);
              goto done;
            }
          else if (post_sequence != cycle)
            {
              bk7258_ap_sem_wake_loop_task_fail(
                state, sem_state, loop_state,
                BK7258_AP_SEM_WAKE_LOOP_ERROR_SEQUENCE, cycle);
              goto done;
            }
          else if (loop_state->post_count != cycle)
            {
              bk7258_ap_sem_wake_loop_task_fail(
                state, sem_state, loop_state,
                BK7258_AP_SEM_WAKE_LOOP_ERROR_COUNT_MISMATCH, cycle);
              goto done;
            }
          else if (loop_state->wake_cpu != BK7258_AP_SECONDARY_CPU)
            {
              bk7258_ap_sem_wake_loop_task_fail(
                state, sem_state, loop_state,
                BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_CPU, cycle);
              goto done;
            }

          loop_state->completed_cycles++;
          loop_state->wake_sequence = cycle;
          if (cycle == 1)
            {
              sem_state->state = BK7258_AP_SEM_WAKE_STATE_WOKEN;
            }

          __asm volatile ("dmb sy" ::: "memory");
          loop_state->state = BK7258_AP_SEM_WAKE_LOOP_STATE_WOKEN;
          __asm volatile ("dmb sy" ::: "memory");

          /* CPU0 publishes the loop error before FAILED.  If its bounded
           * abort raced this wake publication, restore terminal FAILED after
           * the task's last normal state writes instead of leaving
           * error != NONE paired with WOKEN.
           */

          published_error = loop_state->error;
          if (published_error != BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE)
            {
              bk7258_ap_sem_wake_loop_fail(loop_state, published_error);
              published_sem_error = sem_state->error;
              if (published_sem_error != BK7258_AP_SEM_WAKE_ERROR_NONE)
                {
                  bk7258_ap_sem_wake_fail(sem_state,
                                         published_sem_error);
                }

              bk7258_ap_affinity_fail(
                state,
                bk7258_ap_sem_wake_loop_affinity_error(published_error));
              goto done;
            }

          __asm volatile ("sev" ::: "memory");

          if (cycle < BK7258_AP_SEM_WAKE_LOOP_CYCLES)
            {
              continued = 0;
              for (elapsed = 0;
                   elapsed < BK7258_AP_SEM_WAKE_LOOP_TIMEOUT_MS;
                   elapsed++)
                {
                  __asm volatile ("dmb sy" ::: "memory");
                  if (loop_state->state ==
                        BK7258_AP_SEM_WAKE_LOOP_STATE_CONTINUE &&
                      loop_state->completed_cycles == cycle &&
                      loop_state->wake_sequence == cycle)
                    {
                      continued = 1;
                      break;
                    }

                  if (loop_state->state ==
                        BK7258_AP_SEM_WAKE_LOOP_STATE_FAILED ||
                      state->state == BK7258_AP_AFFINITY_STATE_FAILED)
                    {
                      goto done;
                    }

                  if (loop_state->state !=
                        BK7258_AP_SEM_WAKE_LOOP_STATE_WOKEN)
                    {
                      bk7258_ap_sem_wake_loop_task_fail(
                        state, sem_state, loop_state,
                        BK7258_AP_SEM_WAKE_LOOP_ERROR_SEQUENCE, cycle);
                      goto done;
                    }

                  __asm volatile ("wfe" ::: "memory");
                }

              if (!continued)
                {
                  bk7258_ap_sem_wake_loop_task_fail(
                    state, sem_state, loop_state,
                    BK7258_AP_SEM_WAKE_LOOP_ERROR_WAKE_TIMEOUT, cycle);
                  goto done;
                }
            }
        }
    }
#  else
  else if (sem_state->magic != BK7258_AP_SEM_WAKE_STATE_MAGIC ||
           sem_state->version != BK7258_AP_SEM_WAKE_STATE_VERSION ||
           sem_state->size != sizeof(struct bk7258_ap_sem_wake_state_s) ||
           sem_state->generation != state->generation ||
           sem_state->state != BK7258_AP_SEM_WAKE_STATE_INITIALIZING ||
           sem_state->error != BK7258_AP_SEM_WAKE_ERROR_NONE)
    {
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_BAD_STATE);
    }
  else
    {
      sem_state->task_id = (uint32_t)self;
      sem_state->wait_entered = 1;
      sem_state->state = BK7258_AP_SEM_WAKE_STATE_WAITING;
      __asm volatile ("dmb sy; sev" ::: "memory");

      ret = nxsem_wait_uninterruptible(&g_bk7258_ap_cpu1_sem);
      __asm volatile ("dmb sy" ::: "memory");
      wait_state = sem_state->state;
      sem_state->wait_result = (int32_t)ret;
      sem_state->wait_returned = 1;
      sem_state->wake_cpu = (uint32_t)up_cpu_index();
      __asm volatile ("dmb sy" ::: "memory");

      if (ret < 0)
        {
          bk7258_ap_sem_wake_fail(sem_state,
                                 BK7258_AP_SEM_WAKE_ERROR_SEM_WAIT);
        }
      else if (wait_state != BK7258_AP_SEM_WAKE_STATE_POSTED)
        {
          bk7258_ap_sem_wake_fail(sem_state,
                                 BK7258_AP_SEM_WAKE_ERROR_BAD_STATE);
        }
      else if (sem_state->wake_cpu != BK7258_AP_SECONDARY_CPU)
        {
          bk7258_ap_sem_wake_fail(sem_state,
                                 BK7258_AP_SEM_WAKE_ERROR_BAD_CPU);
        }
      else
        {
          sem_state->state = BK7258_AP_SEM_WAKE_STATE_WOKEN;
          __asm volatile ("dmb sy; sev" ::: "memory");
        }
    }
#  endif
#endif

done:
  state->task_completed++;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return NULL;
}
#endif

static void __attribute__((noinline, noreturn, used))
bk7258_ap_secondary_scheduler_entry(void)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  uint32_t control;
  uint32_t msp;

  arm_initialize_stack();
  __asm volatile ("mrs %0, msp" : "=r" (msp));
  __asm volatile ("mrs %0, control" : "=r" (control));

  state->runtime_msp = msp;
  state->control = control;
  state->reserved[0] = BK7258_SCB_CCR;
  state->reserved[1] = BK7258_MPU_CTRL;
  state->online_mask = BK7258_AP_ONLINE_MASK;
  state->secondary_ready = 1;

#ifdef CONFIG_SCHED_INSTRUMENTATION
  sched_note_cpu_started(this_task());
#endif

  bk7258_ap_ipi_mark_scheduler_online();
  state->state = BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE;
  __asm volatile ("dmb sy" ::: "memory");

  /* CPU0 still owns NuttX's scheduler critical section here.  Publish the
   * boot-ready token only after the secondary idle context is complete, but
   * before enabling interrupts: a pending scheduler interrupt can otherwise
   * enter code that waits for CPU0 and deadlock the bootstrap handshake.
   */

  bk7258_cpu2_signal_boot_ready();

  /* nx_idle_trampoline() waits on g_nx_initstate.  The CPU0 reset path can
   * retain a stale private cache line on CPU2, so wait for CPU0's exact
   * post-bringup token and publish the same state into CPU2's local view.
   * CPU0 performs the normal assignment immediately after the wrapper
   * returns.
   */

  bk7258_cpu2_wait_idle_release();
  g_nx_initstate = OSINIT_IDLELOOP;
  __asm volatile ("dmb sy" ::: "memory");

  /* This is the architecture-owned tail of nx_idle_trampoline().  Keeping
   * the first CPU2 sched_unlock() here preserves NuttX's normal secondary
   * idle semantics.  Complete the device-register handshake while interrupts
   * are still disabled, then allow either core's pending scheduler work.
   */

#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
  sched_note_start(this_task());
#endif

  bk7258_cpu2_signal_scheduler_unlocked();
  __asm volatile ("cpsie i; dsb sy; isb sy" ::: "memory");
  sched_unlock();

  for (; ; )
    {
      up_idle();
    }
}

static void __attribute__((naked, noreturn, used))
bk7258_ap_secondary_enter_scheduler(
  uint32_t stack_top __attribute__((unused)))
{
  __asm volatile
    (
      "msr msp, r0\n"
      "dsb sy\n"
      "isb sy\n"
      "b bk7258_ap_secondary_scheduler_entry\n"
    );
}
#endif

static void __attribute__((noinline, noreturn, used))
bk7258_ap_secondary_bootstrap(void)
{
  volatile struct bk7258_cpu2_probe_state_s *state;

  bk7258_cpu2_fpu_initialize();
  state = bk7258_cpu2_probe_state();
  uint32_t control;
  uint32_t msp;

  __asm volatile ("mrs %0, msp" : "=r" (msp));
  __asm volatile ("mrs %0, control" : "=r" (control));

  state->local_core_id = BK7258_CPU2_LOCAL_CORE_ID;
  state->physical_core_id = BK7258_CPU2_PHYSICAL_ID;
  state->runtime_vtor = BK7258_SCB_VTOR;
  state->runtime_msp = msp;
  state->control = control;
  state->boot_count++;
  __asm volatile ("dmb sy" ::: "memory");

  if (state->magic != BK7258_CPU2_PROBE_STATE_MAGIC ||
      state->version != BK7258_CPU2_PROBE_STATE_VERSION ||
      state->size != sizeof(struct bk7258_cpu2_probe_state_s))
    {
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_BAD_BOOT_STATE);
      goto parked;
    }

  state->state = BK7258_CPU2_PROBE_STATE_BOOTSTRAP;
  __asm volatile ("dmb sy" ::: "memory");

  if (*(volatile uint32_t *)BK7258_LOCAL_CORE_ID_ADDR !=
        BK7258_CPU2_LOCAL_CORE_ID ||
      state->physical_core_id != BK7258_CPU2_PHYSICAL_ID)
    {
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_BAD_CORE_ID);
      goto parked;
    }

  if (state->runtime_vtor !=
      (uint32_t)(uintptr_t)__vector_core1_table)
    {
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_BAD_VTOR);
      goto parked;
    }

  if (msp <= BK7258_CPU2_BOOT_STACK_BASE ||
      msp > BK7258_CPU2_BOOT_STACK_TOP)
    {
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_BAD_MSP);
      goto parked;
    }

  if (state->idle_stack_base < BK7258_AP_RAM_BASE ||
      state->idle_stack_base >= state->idle_stack_top ||
      state->idle_stack_top > BK7258_CPU2_BOOT_STACK_BASE)
    {
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_BAD_IDLE_STACK);
      goto parked;
    }

#ifdef CONFIG_BK7258_AP_IPI
  if (bk7258_ap_ipi_secondary_initialize() < 0)
    {
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_IPI_INIT);
      goto parked;
    }
#endif

  state->command = BK7258_CPU2_PROBE_COMMAND_NONE;
  state->error = BK7258_CPU2_PROBE_ERROR_NONE;
  state->online_mask = BK7258_AP_PRIMARY_MASK;

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  state->secondary_ready = 0;
  __asm volatile ("dmb sy" ::: "memory");

  if (bk7258_cpu2_scheduler_irq_initialize() < 0)
    {
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_IPI_INIT);
      goto parked;
    }

  bk7258_ap_secondary_enter_scheduler(state->idle_stack_top);
#else
  state->secondary_ready = 1;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_CPU2_PROBE_STATE_SECONDARY_READY;
  __asm volatile ("dmb sy; sev" ::: "memory");

#  ifdef CONFIG_BK7258_AP_IPI
  __asm volatile ("cpsie i; dsb sy; isb sy" ::: "memory");
#  endif

  for (; ; )
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->command == BK7258_CPU2_PROBE_COMMAND_STOP)
        {
          state->state = BK7258_CPU2_PROBE_STATE_STOPPING;
          state->secondary_ready = 0;
          __asm volatile ("dmb sy" ::: "memory");
          state->command = BK7258_CPU2_PROBE_COMMAND_NONE;
          state->state = BK7258_CPU2_PROBE_STATE_STOPPED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto parked;
        }

      state->heartbeat++;
#  ifdef CONFIG_BK7258_AP_IPI
      __asm volatile ("dmb sy; dsb sy; wfi" ::: "memory");
#  else
      __asm volatile ("dmb sy; wfe" ::: "memory");
#  endif
    }
#endif

parked:
  __asm volatile ("cpsid i; dsb sy; isb sy" ::: "memory");
  for (; ; )
    {
      __asm volatile ("wfe");
    }
}

static void __attribute__((naked, noreturn, used))
bk7258_ap_secondary_reset(void)
{
  __asm volatile
    (
      "cpsid i\n"
      "movs r0, #0\n"
      "msr control, r0\n"
      "msr basepri, r0\n"
      "msr faultmask, r0\n"
      "ldr r0, =0x20000000\n"
      "movs r1, #1\n"
      "str r1, [r0]\n"
      "ldr r0, =__vector_core1_table\n"
      "ldr r1, =0xe000ed08\n"
      "str r0, [r1]\n"
      "dsb sy\n"
      "isb sy\n"
      "b bk7258_ap_secondary_bootstrap\n"
    );
}

static void __attribute__((noinline, noreturn, used))
bk7258_ap_secondary_fault_handler(uint32_t *stack, uint32_t exception)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  uintptr_t frame = (uintptr_t)stack;
  uint32_t error;

  state->fault_exception = exception;
  state->fault_hfsr = BK7258_SCB_HFSR;
  state->fault_cfsr = BK7258_SCB_CFSR;
  state->fault_lr = BK7258_CPU2_INVALID_VALUE;
  state->fault_pc = BK7258_CPU2_INVALID_VALUE;
  state->fault_xpsr = BK7258_CPU2_INVALID_VALUE;

  if ((frame & (sizeof(uint32_t) - 1u)) == 0 &&
      ((frame >= BK7258_CPU2_BOOT_STACK_BASE &&
        frame <= BK7258_CPU2_BOOT_STACK_TOP -
                 BK7258_CPU2_FRAME_WORDS * sizeof(uint32_t))
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
       ||
       (frame >= state->idle_stack_base &&
        frame <= state->idle_stack_top -
                 BK7258_CPU2_FRAME_WORDS * sizeof(uint32_t))
#endif
      ))
    {
      const volatile uint32_t *regs =
        (const volatile uint32_t *)frame;

      state->fault_lr = regs[5];
      state->fault_pc = regs[6];
      state->fault_xpsr = regs[7];
    }

  error = exception == 2u ?
    BK7258_CPU2_PROBE_ERROR_NMI :
    BK7258_CPU2_PROBE_ERROR_HARDFAULT;
  bk7258_cpu2_fail(error);

  for (; ; )
    {
      __asm volatile ("wfe");
    }
}

static void __attribute__((naked, noreturn, used))
bk7258_ap_secondary_fault_entry(void)
{
  __asm volatile
    (
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "mrs r1, ipsr\n"
      "b bk7258_ap_secondary_fault_handler\n"
    );
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
void __wrap_nx_bringup(void)
{
  __real_nx_bringup();
  bk7258_cpu2_signal_idle_release();
  bk7258_cpu2_wait_scheduler_unlocked();
}
#endif

int up_cpu_index(void)
{
  uint32_t cpu = *(volatile uint32_t *)BK7258_LOCAL_CORE_ID_ADDR;

  return cpu == BK7258_AP_SECONDARY_CPU ?
    BK7258_AP_SECONDARY_CPU : BK7258_AP_PRIMARY_CPU;
}

uintptr_t up_get_intstackbase(int cpu)
{
  if (cpu == BK7258_AP_SECONDARY_CPU)
    {
      return BK7258_CPU2_BOOT_STACK_BASE;
    }

  return (uintptr_t)g_intstackalloc;
}

int up_cpu_idlestack(int cpu, struct tcb_s *tcb, size_t stack_size)
{
  int ret;

  if (cpu != BK7258_AP_SECONDARY_CPU || tcb == NULL)
    {
      return -EINVAL;
    }

  ret = up_create_stack(tcb, stack_size, TCB_FLAG_TTYPE_KERNEL);
  if (ret >= 0)
    {
      tcb->affinity = (cpu_set_t)1u << cpu;
    }

  return ret;
}

int up_cpu_start(int cpu)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  volatile uint32_t *control = bk7258_cpu2_control();
  struct tcb_s *tcb;
  uintptr_t idle_base;
  uintptr_t idle_top;
  uint32_t generation;
  uint32_t value;
  int ret;

  if (cpu != BK7258_AP_SECONDARY_CPU)
    {
      return -EINVAL;
    }

  bk7258_cpu2_hold_reset(state);
  up_mdelay(1);

  generation = bk7258_ap_boot_state()->generation;
  if (generation == 0)
    {
      generation = 1;
    }

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_CPU2_PROBE_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = generation;
  state->command = BK7258_CPU2_PROBE_COMMAND_START;
  state->state = BK7258_CPU2_PROBE_STATE_STARTING;
  state->local_core_id = BK7258_CPU2_LOCAL_CORE_ID;
  state->physical_core_id = BK7258_CPU2_PHYSICAL_ID;
  state->vector = (uint32_t)(uintptr_t)__vector_core1_table;
  state->initial_msp = BK7258_CPU2_BOOT_STACK_TOP;
  state->secondary_entry =
    (uint32_t)(uintptr_t)bk7258_ap_secondary_bootstrap;
  state->online_mask = BK7258_AP_PRIMARY_MASK;

  tcb = current_task(cpu);
  if (tcb == NULL || tcb->stack_base_ptr == NULL ||
      tcb->adj_stack_size == 0)
    {
      state->magic = BK7258_CPU2_PROBE_STATE_MAGIC;
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_BAD_IDLE_STACK);
      return -ENOMEM;
    }

  idle_base = (uintptr_t)tcb->stack_base_ptr;
  idle_top = idle_base + tcb->adj_stack_size;
  state->idle_stack_base = (uint32_t)idle_base;
  state->idle_stack_top = (uint32_t)idle_top;

  if (idle_base < BK7258_AP_RAM_BASE || idle_base >= idle_top ||
      idle_top > BK7258_CPU2_BOOT_STACK_BASE)
    {
      state->magic = BK7258_CPU2_PROBE_STATE_MAGIC;
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_BAD_IDLE_STACK);
      return -ENOMEM;
    }

  value = *control;
  state->control_before = value;
  value &= ~(BK7258_SYS_CPU2_RESET |
             BK7258_SYS_CPU2_POWER_DOWN |
             BK7258_SYS_CPU2_HALT |
             BK7258_SYS_CPU2_RXEVT_SEL |
             BK7258_SYS_CPU2_BOOT_MASK);
  value |= BK7258_SYS_CPU2_RXEVT_SEL;
  value |= state->vector & BK7258_SYS_CPU2_BOOT_MASK;
  *control = value;
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  state->control_after = value;
  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_CPU2_PROBE_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

#ifdef CONFIG_BK7258_AP_IPI
  ret = bk7258_ap_ipi_primary_initialize();
  if (ret < 0)
    {
      bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_IPI_INIT);
      return ret;
    }
#endif

  value |= BK7258_SYS_CPU2_RESET;
  *control = value;
  state->control_after = value;
  __asm volatile ("dsb sy; sev" ::: "memory");

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  ret = bk7258_cpu2_wait(BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE,
                         BK7258_CPU2_PROBE_TIMEOUT_MS);
#else
  ret = bk7258_cpu2_wait(BK7258_CPU2_PROBE_STATE_SECONDARY_READY,
                         BK7258_CPU2_PROBE_TIMEOUT_MS);
#endif
  if (ret < 0)
    {
      bk7258_cpu2_hold_reset(state);
      if (state->state != BK7258_CPU2_PROBE_STATE_FAILED)
        {
          bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_TIMEOUT);
        }
    }

  return ret;
}

int up_send_smp_sched(int cpu)
{
  int self = up_cpu_index();
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  int ret;
#endif

  if (cpu == self)
    {
      return OK;
    }

  if (cpu < BK7258_AP_PRIMARY_CPU || cpu > BK7258_AP_SECONDARY_CPU)
    {
      return -EINVAL;
    }

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  ret = bk7258_ap_ipi_send_smp(cpu);
  if (ret >= 0 &&
      state->magic == BK7258_CPU2_PROBE_STATE_MAGIC)
    {
      state->smp_call_requests++;
      __asm volatile ("dmb sy" ::: "memory");
    }

  return ret;
#else
  bk7258_cpu2_note_missing_ipi();
  return -ENOSYS;
#endif
}

void up_send_smp_call(cpu_set_t cpuset)
{
  int cpu;
  int self = up_cpu_index();

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      if ((cpuset & ((cpu_set_t)1u << cpu)) != 0 && cpu != self)
        {
          (void)up_send_smp_sched(cpu);
        }
    }
}

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
int bk7258_ap_smp_scheduler_selftest(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_smp_state_s *state = bk7258_ap_smp_state();
  uint32_t elapsed;
  int cpu;
  int ret;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_SMP_DEFAULT_TIMEOUT_MS;
    }

  if (state->magic != BK7258_AP_SMP_STATE_MAGIC ||
      state->version != BK7258_AP_SMP_STATE_VERSION ||
      state->size != sizeof(struct bk7258_ap_smp_state_s) ||
      state->generation != bk7258_ap_boot_state()->generation ||
      state->state != BK7258_AP_SMP_STATE_ONLINE ||
      state->online_mask != BK7258_AP_ONLINE_MASK)
    {
      return -EAGAIN;
    }

  state->error = BK7258_AP_SMP_ERROR_NONE;
  state->requested_count = 2;
  state->completed_count = 0;
  state->test_runs++;
  state->last_callback_cpu = UINT32_MAX;

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      state->tx_count[cpu] = 0;
      state->rx_count[cpu] = 0;
      state->coalesced_count[cpu] = 0;
      state->send_failures[cpu] = 0;
      state->call_handler_count[cpu] = 0;
      state->delivered_handler_count[cpu] = 0;
      state->callback_count[cpu] = 0;
      state->last_command[cpu] = 0;
    }

  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_SMP_STATE_TESTING;
  __asm volatile ("dmb sy" ::: "memory");

  nxsched_smp_call_init(&g_bk7258_ap_smp_forward_call,
                        bk7258_ap_smp_secondary_callback,
                        (FAR void *)state);
  ret = nxsched_smp_call_single_async(BK7258_AP_SECONDARY_CPU,
                                      &g_bk7258_ap_smp_forward_call);
  if (ret < 0)
    {
      state->error = BK7258_AP_SMP_ERROR_CALL;
      state->state = BK7258_AP_SMP_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return ret;
    }

  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->callback_count[BK7258_AP_PRIMARY_CPU] == 1 &&
          state->callback_count[BK7258_AP_SECONDARY_CPU] == 1)
        {
          break;
        }

      if (state->state == BK7258_AP_SMP_STATE_FAILED)
        {
          return -EIO;
        }

      up_mdelay(1);
      (void)sched_yield();
    }

  if (elapsed == timeout_ms)
    {
      state->error = BK7258_AP_SMP_ERROR_TIMEOUT;
      state->state = BK7258_AP_SMP_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ETIMEDOUT;
    }

  if (state->tx_count[0] < 1 || state->tx_count[1] < 1 ||
      state->rx_count[0] < 1 || state->rx_count[1] < 1 ||
      state->call_handler_count[0] < 1 ||
      state->call_handler_count[1] < 1 ||
      state->delivered_handler_count[0] < 1 ||
      state->delivered_handler_count[1] < 1 ||
      state->send_failures[0] != 0 ||
      state->send_failures[1] != 0 ||
      state->last_callback_cpu != BK7258_AP_PRIMARY_CPU)
    {
      state->error = BK7258_AP_SMP_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_SMP_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  state->completed_count = 2;
  state->error = BK7258_AP_SMP_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_SMP_STATE_PASSED;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}
#endif

#ifdef CONFIG_BK7258_AP_SMP_CPU1_AFFINITY
int bk7258_ap_smp_affinity_selftest(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_affinity_state_s *state =
    bk7258_ap_affinity_state();
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
  volatile struct bk7258_ap_sem_wake_state_s *sem_state =
    bk7258_ap_sem_wake_state();
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  volatile struct bk7258_ap_sem_wake_loop_state_s *loop_state =
    bk7258_ap_sem_wake_loop_state();
#  endif
#endif
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
  volatile struct bk7258_ap_ipi_state_s *ipi = bk7258_ap_ipi_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
#ifndef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
  FAR struct tcb_s *tcb;
#endif
  pthread_attr_t attr;
  pthread_t thread;
  cpu_set_t cpuset = (cpu_set_t)1u << BK7258_AP_SECONDARY_CPU;
  uint32_t elapsed;
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
  int32_t waiter_value;
  int blocked;
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  uint32_t callback_cpu0_before;
  uint32_t callback_cpu1_before;
  uint32_t cycle;
  uint32_t loop_error;
  int first_cycle_completed = 0;
  int sem_initialized = 0;
  int woken;
#  endif
#endif
  int ret;

  if (timeout_ms == 0)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      timeout_ms = BK7258_AP_SEM_WAKE_LOOP_TIMEOUT_MS;
#elif defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE)
      timeout_ms = BK7258_AP_SEM_WAKE_TIMEOUT_MS;
#else
      timeout_ms = BK7258_AP_AFFINITY_TIMEOUT_MS;
#endif
    }

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_AP_AFFINITY_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = bk7258_ap_boot_state()->generation;
  state->state = BK7258_AP_AFFINITY_STATE_INITIALIZING;
  state->requested_mask = (uint32_t)cpuset;
  state->test_runs = 1;
  state->timeout_ms = timeout_ms;
  state->smp_tx_before = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx_before = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_fail_before = smp->send_failures[BK7258_AP_PRIMARY_CPU];
  state->ipi_irq_before = ipi->irq_count[BK7258_AP_SECONDARY_CPU];
  state->ipi_wake_before = ipi->wake_count[BK7258_AP_SECONDARY_CPU];
  state->cpu2_calls_before = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_AP_AFFINITY_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
  memset((void *)(uintptr_t)sem_state, 0, sizeof(*sem_state));
  sem_state->version = BK7258_AP_SEM_WAKE_STATE_VERSION;
  sem_state->size = sizeof(*sem_state);
  sem_state->generation = state->generation;
  sem_state->state = BK7258_AP_SEM_WAKE_STATE_INITIALIZING;
  sem_state->test_runs = 1;
  sem_state->timeout_ms = timeout_ms;
  __asm volatile ("dmb sy" ::: "memory");
  sem_state->magic = BK7258_AP_SEM_WAKE_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  memset((void *)(uintptr_t)loop_state, 0, sizeof(*loop_state));
  loop_state->version = BK7258_AP_SEM_WAKE_LOOP_STATE_VERSION;
  loop_state->size = sizeof(*loop_state);
  loop_state->generation = state->generation;
  loop_state->state = BK7258_AP_SEM_WAKE_LOOP_STATE_INITIALIZING;
  loop_state->requested_cycles = BK7258_AP_SEM_WAKE_LOOP_CYCLES;
  __asm volatile ("dmb sy" ::: "memory");
  loop_state->magic = BK7258_AP_SEM_WAKE_LOOP_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

  callback_cpu0_before = smp->callback_count[BK7258_AP_PRIMARY_CPU];
  callback_cpu1_before = smp->callback_count[BK7258_AP_SECONDARY_CPU];
#endif

  ret = nxsem_init(&g_bk7258_ap_cpu1_sem, 0, 0);
  if (ret < 0)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      bk7258_ap_sem_wake_loop_fail(
        loop_state, BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE);
#endif
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_SEM_INIT);
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_STATE);
      return ret;
    }

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  sem_initialized = 1;
#endif

#ifdef CONFIG_PRIORITY_INHERITANCE
  ret = nxsem_set_protocol(&g_bk7258_ap_cpu1_sem, SEM_PRIO_NONE);
  if (ret < 0)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      bk7258_ap_sem_wake_loop_fail(
        loop_state, BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE);
#endif
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_SEM_INIT);
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_STATE);
      return ret;
    }
#endif
#endif

  if (smp->magic != BK7258_AP_SMP_STATE_MAGIC ||
      smp->version != BK7258_AP_SMP_STATE_VERSION ||
      smp->size != sizeof(struct bk7258_ap_smp_state_s) ||
      smp->generation != state->generation ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != BK7258_AP_ONLINE_MASK ||
      ipi->magic != BK7258_AP_IPI_STATE_MAGIC ||
      ipi->version != BK7258_AP_IPI_STATE_VERSION ||
      ipi->size != sizeof(struct bk7258_ap_ipi_state_s) ||
      ipi->generation != state->generation ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != BK7258_AP_ONLINE_MASK)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      bk7258_ap_sem_wake_loop_fail(
        loop_state, BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE);
#  endif
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_BAD_STATE);
#endif
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_STATE);
      return -EAGAIN;
    }

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      bk7258_ap_sem_wake_loop_fail(
        loop_state, BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE);
#  endif
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_BAD_STATE);
#endif
      bk7258_ap_affinity_fail(state, BK7258_AP_AFFINITY_ERROR_ATTR);
      return -ret;
    }

  ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    }

  if (ret != 0)
    {
      (void)pthread_attr_destroy(&attr);
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      bk7258_ap_sem_wake_loop_fail(
        loop_state, BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE);
#  endif
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_BAD_STATE);
#endif
      bk7258_ap_affinity_fail(state, BK7258_AP_AFFINITY_ERROR_ATTR);
      return -ret;
    }

  state->state = BK7258_AP_AFFINITY_STATE_DISPATCHING;
  __asm volatile ("dmb sy" ::: "memory");
  ret = pthread_create(&thread, &attr, bk7258_ap_cpu1_affinity_task,
                       (FAR void *)state);
  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      bk7258_ap_sem_wake_loop_fail(
        loop_state, BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE);
#  endif
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_BAD_STATE);
#endif
      bk7258_ap_affinity_fail(state, BK7258_AP_AFFINITY_ERROR_CREATE);
      return -ret;
    }

  state->task_id = (uint32_t)thread;
  __asm volatile ("dmb sy" ::: "memory");

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  for (cycle = 1; cycle <= BK7258_AP_SEM_WAKE_LOOP_CYCLES; cycle++)
    {
      blocked = 0;
      waiter_value = INT32_MAX;
      ret = OK;
      for (elapsed = 0; elapsed < timeout_ms; elapsed++)
        {
          __asm volatile ("dmb sy" ::: "memory");
          if (loop_state->state ==
                BK7258_AP_SEM_WAKE_LOOP_STATE_WAITING &&
              loop_state->wait_sequence == cycle &&
              loop_state->wait_entered == cycle &&
              loop_state->waiter_observed == cycle - 1 &&
              loop_state->post_count == cycle - 1 &&
              loop_state->wait_returned == cycle - 1 &&
              loop_state->completed_cycles == cycle - 1 &&
              state->task_id == (uint32_t)thread &&
              (cycle != 1 ||
               (sem_state->state == BK7258_AP_SEM_WAKE_STATE_WAITING &&
                sem_state->task_id == (uint32_t)thread &&
                sem_state->wait_entered == 1)))
            {
              waiter_value =
                bk7258_ap_sem_exact_waiter_value(thread, cpuset);
              if (waiter_value == -1)
                {
                  blocked = 1;
                  break;
                }
            }

          if (state->state == BK7258_AP_AFFINITY_STATE_FAILED ||
              loop_state->state ==
                BK7258_AP_SEM_WAKE_LOOP_STATE_FAILED ||
              sem_state->state == BK7258_AP_SEM_WAKE_STATE_FAILED ||
              state->task_completed != 0)
            {
              ret = -EIO;
              break;
            }

          up_mdelay(1);
          (void)sched_yield();
        }

      if (!blocked)
        {
          if (elapsed == timeout_ms)
            {
              loop_error = BK7258_AP_SEM_WAKE_LOOP_ERROR_WAIT_TIMEOUT;
              ret = -ETIMEDOUT;
            }
          else
            {
              loop_error = loop_state->error !=
                             BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE ?
                             loop_state->error :
                             BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE;
              ret = -EIO;
            }

          return bk7258_ap_sem_wake_loop_abort(
            state, sem_state, loop_state, loop_error,
            first_cycle_completed, sem_initialized, ret);
        }

      loop_state->waiter_sem_value = waiter_value;
      loop_state->waiter_observed++;
      if (cycle == 1)
        {
          sem_state->waiter_sem_value = waiter_value;
          sem_state->waiter_observed = 1;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (loop_state->waiter_observed != cycle)
        {
          return bk7258_ap_sem_wake_loop_abort(
            state, sem_state, loop_state,
            BK7258_AP_SEM_WAKE_LOOP_ERROR_COUNT_MISMATCH,
            first_cycle_completed, sem_initialized, -EIO);
        }

      if (cycle == 1)
        {
          sem_state->state = BK7258_AP_SEM_WAKE_STATE_BLOCKED;
        }

      loop_state->state = BK7258_AP_SEM_WAKE_LOOP_STATE_BLOCKED;
      __asm volatile ("dmb sy" ::: "memory");

      if (cycle == 1)
        {
          sem_state->smp_tx_before =
            smp->tx_count[BK7258_AP_PRIMARY_CPU];
          sem_state->smp_rx_before =
            smp->rx_count[BK7258_AP_SECONDARY_CPU];
          sem_state->smp_fail_before =
            smp->send_failures[BK7258_AP_PRIMARY_CPU];
          sem_state->ipi_irq_before =
            ipi->irq_count[BK7258_AP_SECONDARY_CPU];
          sem_state->ipi_wake_before =
            ipi->wake_count[BK7258_AP_SECONDARY_CPU];
          sem_state->cpu2_calls_before = cpu2->smp_call_requests;

          loop_state->smp_tx_before = sem_state->smp_tx_before;
          loop_state->smp_rx_before = sem_state->smp_rx_before;
          loop_state->smp_fail_before = sem_state->smp_fail_before;
          loop_state->ipi_irq_before = sem_state->ipi_irq_before;
          loop_state->ipi_wake_before = sem_state->ipi_wake_before;
          loop_state->cpu2_calls_before = sem_state->cpu2_calls_before;
        }

      loop_state->post_cpu = (uint32_t)up_cpu_index();
      loop_state->post_count++;
      loop_state->post_sequence = cycle;
      if (cycle == 1)
        {
          sem_state->post_cpu = loop_state->post_cpu;
          sem_state->post_count = 1;
          sem_state->state = BK7258_AP_SEM_WAKE_STATE_POSTED;
        }

      __asm volatile ("dmb sy" ::: "memory");
      loop_state->state = BK7258_AP_SEM_WAKE_LOOP_STATE_POSTED;
      __asm volatile ("dmb sy" ::: "memory");

      ret = nxsem_post(&g_bk7258_ap_cpu1_sem);
      loop_state->post_result = (int32_t)ret;
      if (cycle == 1)
        {
          sem_state->post_result = (int32_t)ret;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (ret < 0)
        {
          return bk7258_ap_sem_wake_loop_abort(
            state, sem_state, loop_state,
            BK7258_AP_SEM_WAKE_LOOP_ERROR_SEM_POST,
            first_cycle_completed, sem_initialized, ret);
        }

      if (loop_state->post_cpu != BK7258_AP_PRIMARY_CPU)
        {
          return bk7258_ap_sem_wake_loop_abort(
            state, sem_state, loop_state,
            BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_CPU,
            first_cycle_completed, sem_initialized, -EIO);
        }

      woken = 0;
      ret = OK;
      for (elapsed = 0; elapsed < timeout_ms; elapsed++)
        {
          __asm volatile ("dmb sy" ::: "memory");
          if (loop_state->state ==
                BK7258_AP_SEM_WAKE_LOOP_STATE_WOKEN &&
              loop_state->completed_cycles == cycle &&
              loop_state->wait_returned == cycle &&
              loop_state->wake_sequence == cycle)
            {
              woken = 1;
              break;
            }

          if (state->state == BK7258_AP_AFFINITY_STATE_FAILED ||
              loop_state->state ==
                BK7258_AP_SEM_WAKE_LOOP_STATE_FAILED ||
              sem_state->state == BK7258_AP_SEM_WAKE_STATE_FAILED ||
              state->task_completed != 0)
            {
              ret = -EIO;
              break;
            }

          up_mdelay(1);
          (void)sched_yield();
        }

      if (!woken)
        {
          if (elapsed == timeout_ms)
            {
              loop_error = BK7258_AP_SEM_WAKE_LOOP_ERROR_WAKE_TIMEOUT;
              ret = -ETIMEDOUT;
            }
          else
            {
              loop_error = loop_state->error !=
                             BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE ?
                             loop_state->error :
                             BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE;
              ret = -EIO;
            }

          return bk7258_ap_sem_wake_loop_abort(
            state, sem_state, loop_state, loop_error,
            first_cycle_completed, sem_initialized, ret);
        }

      if (cycle == 1)
        {
          sem_state->smp_tx_after =
            smp->tx_count[BK7258_AP_PRIMARY_CPU];
          sem_state->smp_rx_after =
            smp->rx_count[BK7258_AP_SECONDARY_CPU];
          sem_state->smp_fail_after =
            smp->send_failures[BK7258_AP_PRIMARY_CPU];
          sem_state->ipi_irq_after =
            ipi->irq_count[BK7258_AP_SECONDARY_CPU];
          sem_state->ipi_wake_after =
            ipi->wake_count[BK7258_AP_SECONDARY_CPU];
          sem_state->cpu2_calls_after = cpu2->smp_call_requests;
          __asm volatile ("dmb sy" ::: "memory");

          if (sem_state->magic != BK7258_AP_SEM_WAKE_STATE_MAGIC ||
              sem_state->version != BK7258_AP_SEM_WAKE_STATE_VERSION ||
              sem_state->size !=
                sizeof(struct bk7258_ap_sem_wake_state_s) ||
              sem_state->generation != state->generation ||
              sem_state->state != BK7258_AP_SEM_WAKE_STATE_WOKEN ||
              sem_state->error != BK7258_AP_SEM_WAKE_ERROR_NONE ||
              sem_state->task_id != state->task_id ||
              sem_state->test_runs != 1 ||
              sem_state->timeout_ms != timeout_ms ||
              sem_state->wait_entered != 1 ||
              sem_state->waiter_observed != 1 ||
              sem_state->waiter_sem_value != -1 ||
              sem_state->post_cpu != BK7258_AP_PRIMARY_CPU ||
              sem_state->post_count != 1 ||
              sem_state->post_result != 0 ||
              sem_state->wait_returned != 1 ||
              sem_state->wait_result != 0 ||
              sem_state->wake_cpu != BK7258_AP_SECONDARY_CPU ||
              sem_state->smp_tx_after != sem_state->smp_tx_before + 1 ||
              sem_state->smp_rx_after != sem_state->smp_rx_before + 1 ||
              sem_state->smp_fail_after != sem_state->smp_fail_before ||
              sem_state->ipi_irq_after != sem_state->ipi_irq_before + 1 ||
              sem_state->ipi_wake_after !=
                sem_state->ipi_wake_before + 1 ||
              sem_state->cpu2_calls_after !=
                sem_state->cpu2_calls_before + 1)
            {
              return bk7258_ap_sem_wake_loop_abort(
                state, sem_state, loop_state,
                BK7258_AP_SEM_WAKE_LOOP_ERROR_COUNT_MISMATCH,
                first_cycle_completed, sem_initialized, -EIO);
            }

          first_cycle_completed = 1;
        }

      if (cycle == BK7258_AP_SEM_WAKE_LOOP_CYCLES)
        {
          loop_state->smp_tx_after =
            smp->tx_count[BK7258_AP_PRIMARY_CPU];
          loop_state->smp_rx_after =
            smp->rx_count[BK7258_AP_SECONDARY_CPU];
          loop_state->smp_fail_after =
            smp->send_failures[BK7258_AP_PRIMARY_CPU];
          loop_state->ipi_irq_after =
            ipi->irq_count[BK7258_AP_SECONDARY_CPU];
          loop_state->ipi_wake_after =
            ipi->wake_count[BK7258_AP_SECONDARY_CPU];
          loop_state->cpu2_calls_after = cpu2->smp_call_requests;
          __asm volatile ("dmb sy" ::: "memory");
        }
      else
        {
          loop_state->state = BK7258_AP_SEM_WAKE_LOOP_STATE_CONTINUE;
          __asm volatile ("dmb sy; sev" ::: "memory");
        }
    }
#  else
  blocked = 0;
  ret = OK;
  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->state == BK7258_AP_AFFINITY_STATE_FAILED ||
          sem_state->state == BK7258_AP_SEM_WAKE_STATE_FAILED ||
          state->task_completed != 0)
        {
          ret = -EIO;
          break;
        }

      if (sem_state->state == BK7258_AP_SEM_WAKE_STATE_WAITING)
        {
          waiter_value = bk7258_ap_sem_exact_waiter_value(thread, cpuset);
          sem_state->waiter_sem_value = waiter_value;
          if (sem_state->task_id == (uint32_t)thread &&
              waiter_value == -1)
            {
              blocked = 1;
              break;
            }
        }

      up_mdelay(1);
      (void)sched_yield();
    }

  if (!blocked)
    {
      if (elapsed == timeout_ms)
        {
          bk7258_ap_sem_wake_fail(
            sem_state, BK7258_AP_SEM_WAKE_ERROR_WAIT_TIMEOUT);
          bk7258_ap_affinity_fail(state,
                                 BK7258_AP_AFFINITY_ERROR_TIMEOUT);
          ret = -ETIMEDOUT;
        }
      else
        {
          bk7258_ap_sem_wake_fail(sem_state,
                                 BK7258_AP_SEM_WAKE_ERROR_BAD_STATE);
          bk7258_ap_affinity_fail(state,
                                 BK7258_AP_AFFINITY_ERROR_BAD_STATE);
          ret = -EIO;
        }

      (void)nxsem_post(&g_bk7258_ap_cpu1_sem);
      return ret;
    }

  sem_state->waiter_observed = 1;
  __asm volatile ("dmb sy" ::: "memory");
  sem_state->state = BK7258_AP_SEM_WAKE_STATE_BLOCKED;
  __asm volatile ("dmb sy" ::: "memory");
  sem_state->smp_tx_before = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  sem_state->smp_rx_before = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  sem_state->smp_fail_before =
    smp->send_failures[BK7258_AP_PRIMARY_CPU];
  sem_state->ipi_irq_before = ipi->irq_count[BK7258_AP_SECONDARY_CPU];
  sem_state->ipi_wake_before =
    ipi->wake_count[BK7258_AP_SECONDARY_CPU];
  sem_state->cpu2_calls_before = cpu2->smp_call_requests;
  sem_state->post_cpu = (uint32_t)up_cpu_index();
  sem_state->post_count = 1;
  __asm volatile ("dmb sy" ::: "memory");
  sem_state->state = BK7258_AP_SEM_WAKE_STATE_POSTED;
  __asm volatile ("dmb sy" ::: "memory");

  ret = nxsem_post(&g_bk7258_ap_cpu1_sem);
  sem_state->post_result = (int32_t)ret;
  __asm volatile ("dmb sy" ::: "memory");
  if (ret < 0)
    {
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_SEM_POST);
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_STATE);
      return ret;
    }
#  endif
#endif

  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->task_completed == 1)
        {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
          if (bk7258_ap_pid_released(thread))
#else
          tcb = nxsched_get_tcb((pid_t)thread);
          if (tcb == NULL)
#endif
            {
              state->pid_released = 1;
              __asm volatile ("dmb sy" ::: "memory");
              break;
            }

#ifndef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
          nxsched_put_tcb(tcb);
#endif
        }

      up_mdelay(1);
      (void)sched_yield();
    }

  if (elapsed == timeout_ms)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      return bk7258_ap_sem_wake_loop_abort(
        state, sem_state, loop_state,
        BK7258_AP_SEM_WAKE_LOOP_ERROR_WAKE_TIMEOUT,
        first_cycle_completed, sem_initialized, -ETIMEDOUT);
#elif defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE)
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_WAIT_TIMEOUT);
#endif
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_TIMEOUT);
      return -ETIMEDOUT;
    }

  __asm volatile ("dmb sy" ::: "memory");
  state->smp_tx_after = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx_after = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_fail_after = smp->send_failures[BK7258_AP_PRIMARY_CPU];
  state->ipi_irq_after = ipi->irq_count[BK7258_AP_SECONDARY_CPU];
  state->ipi_wake_after = ipi->wake_count[BK7258_AP_SECONDARY_CPU];
  state->cpu2_calls_after = cpu2->smp_call_requests;
#if defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE) && \
    !defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP)
  sem_state->smp_tx_after = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  sem_state->smp_rx_after = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  sem_state->smp_fail_after =
    smp->send_failures[BK7258_AP_PRIMARY_CPU];
  sem_state->ipi_irq_after = ipi->irq_count[BK7258_AP_SECONDARY_CPU];
  sem_state->ipi_wake_after =
    ipi->wake_count[BK7258_AP_SECONDARY_CPU];
  sem_state->cpu2_calls_after = cpu2->smp_call_requests;
#endif
  __asm volatile ("dmb sy" ::: "memory");

  if (state->state == BK7258_AP_AFFINITY_STATE_FAILED)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      loop_error = loop_state->error !=
                     BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE ?
                     loop_state->error :
                     BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE;
      return bk7258_ap_sem_wake_loop_abort(
        state, sem_state, loop_state, loop_error,
        first_cycle_completed, sem_initialized, -EIO);
#else
      return -EIO;
#endif
    }

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  if (loop_state->state == BK7258_AP_SEM_WAKE_LOOP_STATE_FAILED ||
      sem_state->state == BK7258_AP_SEM_WAKE_STATE_FAILED)
    {
      loop_error = loop_state->error !=
                     BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE ?
                     loop_state->error :
                     BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE;
      return bk7258_ap_sem_wake_loop_abort(
        state, sem_state, loop_state, loop_error,
        first_cycle_completed, sem_initialized, -EIO);
    }

  if (sem_state->post_cpu != BK7258_AP_PRIMARY_CPU ||
      sem_state->wake_cpu != BK7258_AP_SECONDARY_CPU ||
      loop_state->post_cpu != BK7258_AP_PRIMARY_CPU ||
      loop_state->wake_cpu != BK7258_AP_SECONDARY_CPU)
    {
      return bk7258_ap_sem_wake_loop_abort(
        state, sem_state, loop_state,
        BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_CPU,
        first_cycle_completed, sem_initialized, -EIO);
    }
#  else
  if (sem_state->state == BK7258_AP_SEM_WAKE_STATE_FAILED)
    {
      bk7258_ap_affinity_fail(state,
                             BK7258_AP_AFFINITY_ERROR_BAD_STATE);
      return -EIO;
    }

  if (sem_state->post_cpu != BK7258_AP_PRIMARY_CPU ||
      sem_state->wake_cpu != BK7258_AP_SECONDARY_CPU)
    {
      bk7258_ap_sem_wake_fail(sem_state,
                             BK7258_AP_SEM_WAKE_ERROR_BAD_CPU);
      bk7258_ap_affinity_fail(
        state, BK7258_AP_AFFINITY_ERROR_COUNT_MISMATCH);
      return -EIO;
    }
#  endif
#endif

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  if (loop_state->wait_sequence != BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->post_sequence != BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->wake_sequence != BK7258_AP_SEM_WAKE_LOOP_CYCLES)
    {
      return bk7258_ap_sem_wake_loop_abort(
        state, sem_state, loop_state,
        BK7258_AP_SEM_WAKE_LOOP_ERROR_SEQUENCE,
        first_cycle_completed, sem_initialized, -EIO);
    }

  if (sem_state->magic != BK7258_AP_SEM_WAKE_STATE_MAGIC ||
      sem_state->version != BK7258_AP_SEM_WAKE_STATE_VERSION ||
      sem_state->size != sizeof(struct bk7258_ap_sem_wake_state_s) ||
      sem_state->generation != state->generation ||
      sem_state->state != BK7258_AP_SEM_WAKE_STATE_WOKEN ||
      sem_state->error != BK7258_AP_SEM_WAKE_ERROR_NONE ||
      sem_state->task_id != state->task_id ||
      sem_state->test_runs != 1 ||
      sem_state->timeout_ms != timeout_ms ||
      sem_state->wait_entered != 1 ||
      sem_state->waiter_observed != 1 ||
      sem_state->waiter_sem_value != -1 ||
      sem_state->post_cpu != BK7258_AP_PRIMARY_CPU ||
      sem_state->post_count != 1 ||
      sem_state->post_result != 0 ||
      sem_state->wait_returned != 1 ||
      sem_state->wait_result != 0 ||
      sem_state->wake_cpu != BK7258_AP_SECONDARY_CPU ||
      sem_state->smp_tx_after != sem_state->smp_tx_before + 1 ||
      sem_state->smp_rx_after != sem_state->smp_rx_before + 1 ||
      sem_state->smp_fail_after != sem_state->smp_fail_before ||
      sem_state->ipi_irq_after != sem_state->ipi_irq_before + 1 ||
      sem_state->ipi_wake_after != sem_state->ipi_wake_before + 1 ||
      sem_state->cpu2_calls_after != sem_state->cpu2_calls_before + 1)
    {
      bk7258_ap_sem_wake_fail(
        sem_state, BK7258_AP_SEM_WAKE_ERROR_COUNT_MISMATCH);
      return bk7258_ap_sem_wake_loop_abort(
        state, sem_state, loop_state,
        BK7258_AP_SEM_WAKE_LOOP_ERROR_COUNT_MISMATCH,
        first_cycle_completed, sem_initialized, -EIO);
    }

  if (state->magic != BK7258_AP_AFFINITY_STATE_MAGIC ||
      state->version != BK7258_AP_AFFINITY_STATE_VERSION ||
      state->size != sizeof(struct bk7258_ap_affinity_state_s) ||
      state->generation != bk7258_ap_boot_state()->generation ||
      state->state != BK7258_AP_AFFINITY_STATE_RUNNING ||
      state->error != BK7258_AP_AFFINITY_ERROR_NONE ||
      state->task_id != (uint32_t)thread ||
      state->task_started != 1 || state->task_completed != 1 ||
      state->pid_released != 1 ||
      state->test_runs != 1 || state->timeout_ms != timeout_ms ||
      state->task_cpu != BK7258_AP_SECONDARY_CPU ||
      state->requested_mask !=
        ((uint32_t)1u << BK7258_AP_SECONDARY_CPU) ||
      state->observed_mask != state->requested_mask ||
      state->smp_tx_after != state->smp_tx_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES + 1u ||
      state->smp_rx_after != state->smp_rx_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES + 1u ||
      state->ipi_irq_after != state->ipi_irq_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES + 1u ||
      state->ipi_wake_after != state->ipi_wake_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES + 1u ||
      state->cpu2_calls_after != state->cpu2_calls_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES + 1u ||
      state->smp_fail_after != state->smp_fail_before ||
      loop_state->magic != BK7258_AP_SEM_WAKE_LOOP_STATE_MAGIC ||
      loop_state->version != BK7258_AP_SEM_WAKE_LOOP_STATE_VERSION ||
      loop_state->size !=
        sizeof(struct bk7258_ap_sem_wake_loop_state_s) ||
      loop_state->generation != state->generation ||
      loop_state->state != BK7258_AP_SEM_WAKE_LOOP_STATE_WOKEN ||
      loop_state->error != BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE ||
      loop_state->requested_cycles != BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->completed_cycles != BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->wait_entered != BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->waiter_observed != BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->post_count != BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->wait_returned != BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->waiter_sem_value != -1 ||
      loop_state->post_cpu != BK7258_AP_PRIMARY_CPU ||
      loop_state->post_result != 0 ||
      loop_state->wait_result != 0 ||
      loop_state->wake_cpu != BK7258_AP_SECONDARY_CPU ||
      loop_state->smp_tx_after != loop_state->smp_tx_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->smp_rx_after != loop_state->smp_rx_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->smp_fail_after != loop_state->smp_fail_before ||
      loop_state->ipi_irq_after != loop_state->ipi_irq_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->ipi_wake_after != loop_state->ipi_wake_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      loop_state->cpu2_calls_after != loop_state->cpu2_calls_before +
        BK7258_AP_SEM_WAKE_LOOP_CYCLES ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != BK7258_AP_ONLINE_MASK ||
      smp->callback_count[BK7258_AP_PRIMARY_CPU] !=
        callback_cpu0_before ||
      smp->callback_count[BK7258_AP_SECONDARY_CPU] !=
        callback_cpu1_before ||
      smp->coalesced_count[BK7258_AP_PRIMARY_CPU] != 0 ||
      smp->coalesced_count[BK7258_AP_SECONDARY_CPU] != 0 ||
      smp->send_failures[BK7258_AP_PRIMARY_CPU] != 0 ||
      smp->send_failures[BK7258_AP_SECONDARY_CPU] != 0 ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE ||
      ipi->send_failures[BK7258_AP_PRIMARY_CPU] != 0 ||
      ipi->send_failures[BK7258_AP_SECONDARY_CPU] != 0 ||
      ipi->stale_count != 0 || ipi->spurious_count != 0 ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != BK7258_AP_ONLINE_MASK)
    {
      return bk7258_ap_sem_wake_loop_abort(
        state, sem_state, loop_state,
        BK7258_AP_SEM_WAKE_LOOP_ERROR_COUNT_MISMATCH,
        first_cycle_completed, sem_initialized, -EIO);
    }

  sem_state->error = BK7258_AP_SEM_WAKE_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  sem_state->state = BK7258_AP_SEM_WAKE_STATE_PASSED;
  __asm volatile ("dmb sy" ::: "memory");

  loop_state->error = BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  loop_state->state = BK7258_AP_SEM_WAKE_LOOP_STATE_PASSED;
  __asm volatile ("dmb sy" ::: "memory");
#else
  if (state->state != BK7258_AP_AFFINITY_STATE_RUNNING ||
      state->task_started != 1 || state->task_completed != 1 ||
      state->pid_released != 1 ||
      state->task_cpu != BK7258_AP_SECONDARY_CPU ||
      state->requested_mask !=
        ((uint32_t)1u << BK7258_AP_SECONDARY_CPU) ||
      state->observed_mask != state->requested_mask ||
      state->smp_tx_after <= state->smp_tx_before ||
      state->smp_rx_after <= state->smp_rx_before ||
      state->ipi_irq_after <= state->ipi_irq_before ||
      state->ipi_wake_after <= state->ipi_wake_before ||
      state->cpu2_calls_after <= state->cpu2_calls_before ||
      state->smp_fail_after != state->smp_fail_before ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
      || sem_state->magic != BK7258_AP_SEM_WAKE_STATE_MAGIC
      || sem_state->version != BK7258_AP_SEM_WAKE_STATE_VERSION
      || sem_state->size != sizeof(struct bk7258_ap_sem_wake_state_s)
      || sem_state->generation != state->generation
      || sem_state->state != BK7258_AP_SEM_WAKE_STATE_WOKEN
      || sem_state->error != BK7258_AP_SEM_WAKE_ERROR_NONE
      || sem_state->task_id != state->task_id
      || sem_state->test_runs != 1
      || sem_state->timeout_ms != timeout_ms
      || sem_state->wait_entered != 1
      || sem_state->waiter_observed != 1
      || sem_state->waiter_sem_value != -1
      || sem_state->post_count != 1
      || sem_state->post_result != 0
      || sem_state->wait_returned != 1
      || sem_state->wait_result != 0
      || sem_state->smp_tx_after != sem_state->smp_tx_before + 1
      || sem_state->smp_rx_after != sem_state->smp_rx_before + 1
      || sem_state->smp_fail_after != sem_state->smp_fail_before
      || sem_state->ipi_irq_after != sem_state->ipi_irq_before + 1
      || sem_state->ipi_wake_after != sem_state->ipi_wake_before + 1
      || sem_state->cpu2_calls_after !=
         sem_state->cpu2_calls_before + 1
      || smp->coalesced_count[BK7258_AP_PRIMARY_CPU] != 0
      || smp->coalesced_count[BK7258_AP_SECONDARY_CPU] != 0
      || ipi->state != BK7258_AP_IPI_STATE_READY
      || ipi->error != BK7258_AP_IPI_ERROR_NONE
      || ipi->stale_count != 0
      || ipi->spurious_count != 0
#endif
      )
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
      bk7258_ap_sem_wake_fail(
        sem_state, BK7258_AP_SEM_WAKE_ERROR_COUNT_MISMATCH);
#endif
      bk7258_ap_affinity_fail(
        state, BK7258_AP_AFFINITY_ERROR_COUNT_MISMATCH);
      return -EIO;
    }

#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE
  sem_state->error = BK7258_AP_SEM_WAKE_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  sem_state->state = BK7258_AP_SEM_WAKE_STATE_PASSED;
  __asm volatile ("dmb sy" ::: "memory");
#endif
#endif

  state->error = BK7258_AP_AFFINITY_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_AFFINITY_STATE_PASSED;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}
#endif

int bk7258_ap_smp_secondary_stop(uint32_t timeout_ms)
{
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  (void)timeout_ms;
  return -ENOTSUP;
#else
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  int ret = OK;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_CPU2_PROBE_STOP_TIMEOUT_MS;
    }

  if (state->magic != BK7258_CPU2_PROBE_STATE_MAGIC ||
      state->version != BK7258_CPU2_PROBE_STATE_VERSION)
    {
      ret = -EIO;
    }
  else if (state->state == BK7258_CPU2_PROBE_STATE_SECONDARY_READY ||
           state->state == BK7258_CPU2_PROBE_STATE_BOOTSTRAP ||
           state->state == BK7258_CPU2_PROBE_STATE_STARTING)
    {
      state->command = BK7258_CPU2_PROBE_COMMAND_STOP;
      __asm volatile ("dmb sy" ::: "memory");
#ifdef CONFIG_BK7258_AP_IPI
      ret = bk7258_ap_ipi_wake_secondary();
      if (ret >= 0)
        {
          ret = bk7258_cpu2_wait(BK7258_CPU2_PROBE_STATE_STOPPED,
                                 timeout_ms);
        }
#else
      __asm volatile ("sev" ::: "memory");
      ret = bk7258_cpu2_wait(BK7258_CPU2_PROBE_STATE_STOPPED,
                             timeout_ms);
#endif
    }
  else if (state->state == BK7258_CPU2_PROBE_STATE_FAILED)
    {
      ret = -EIO;
    }

  bk7258_cpu2_hold_reset(state);
  state->secondary_ready = 0;
  __asm volatile ("dmb sy" ::: "memory");

#ifdef CONFIG_BK7258_AP_IPI
  if (ret >= 0)
    {
      bk7258_ap_ipi_mark_stopped();
    }
#endif

  if (ret < 0 && state->state != BK7258_CPU2_PROBE_STATE_FAILED)
    {
      bk7258_cpu2_fail(ret == -ETIMEDOUT ?
        BK7258_CPU2_PROBE_ERROR_TIMEOUT :
        BK7258_CPU2_PROBE_ERROR_BAD_BOOT_STATE);
    }

  return ret;
#endif
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

__attribute__((section(".vectors_core1"), used, aligned(512)))
const void *const __vector_core1_table[BK7258_CPU2_VECTOR_COUNT] =
{
  [0] = (void *)BK7258_CPU2_BOOT_STACK_TOP,
  [1] = (void *)bk7258_ap_secondary_reset,
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  [2 ... 3] = &bk7258_ap_secondary_fault_entry,
  [4 ... 79] = &exception_common
#else
  [2 ... 78] = &bk7258_ap_secondary_fault_entry,
#  ifdef CONFIG_BK7258_AP_IPI
  [BK7258_IRQ_MAILBOX] = &exception_common
#  else
  [BK7258_IRQ_MAILBOX] = &bk7258_ap_secondary_fault_entry
#  endif
#endif
};

#endif /* CONFIG_BK7258_AP_SMP_BOOTSTRAP */
