/****************************************************************************
 * chips/bk7258/ap/
 * bk7258_ap_smp_advanced.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N8-C5 bidirectional pingpong, N8-C6 dual CPU1 tasks, N8-C7 controlled
 * migration, N8-C8 timed wake, and N8-D1 scheduler quiesce/resume.  Uses the
 * shared generic advanced-stage ABI struct at offsets 0x480..0x680.  Compiled
 * only when one advanced config symbol is enabled; otherwise the file is
 * empty.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_AP_SMP_BIDIR_PINGPONG) || \
    defined(CONFIG_BK7258_AP_SMP_CPU1_DUALTASK) || \
    defined(CONFIG_BK7258_AP_SMP_CONTROLLED_MIGRATION) || \
    defined(CONFIG_BK7258_AP_SMP_CPU1_TIMED_WAKE) || \
    defined(CONFIG_BK7258_AP_SMP_LIFECYCLE_QUIESCE)

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/irq.h>

#include "arm_internal.h"
#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if !defined(CONFIG_SMP)
#  error BK7258 advanced SMP tests require CONFIG_SMP
#endif

#if CONFIG_SMP_NCPUS != 2
#  error N8-C5..D1 support exactly two AP logical CPUs
#endif

#if CONFIG_SMP_DEFAULT_CPUSET != 0x1
#  error N8-C5..D1 must keep ordinary tasks on AP logical CPU0
#endif

#define BK7258_AP_PRIMARY_CPU     0
#define BK7258_AP_SECONDARY_CPU   1

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_BIDIR_PINGPONG
static sem_t g_bp2p_sem[2];
#endif

#ifdef CONFIG_BK7258_AP_SMP_CPU1_DUALTASK
static sem_t g_bdul_sem[2];
#endif

#ifdef CONFIG_BK7258_AP_SMP_CONTROLLED_MIGRATION
struct bmig_rendezvous_s
{
  volatile struct bk7258_ap_advanced_state_s *state;
  pthread_t thread;
  cpu_set_t target;
  volatile uint32_t target_cpu;
  volatile uint32_t cycle;
  volatile uint32_t callback_started;
  volatile uint32_t callback_completed;
};

static sem_t g_bmig_sem;
static struct smp_call_data_s g_bmig_call;
static struct bmig_rendezvous_s g_bmig_rendezvous;
#endif

#ifdef CONFIG_BK7258_AP_SMP_LIFECYCLE_QUIESCE
static struct smp_call_data_s g_blcy_quiesce_call;
#endif

/****************************************************************************
 * Private Helpers
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_BIDIR_PINGPONG
/* PID-release check without taking a TCB reference. */

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

/****************************************************************************
 * N8-C5 Bidirectional Pingpong
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_BIDIR_PINGPONG

static int bp2p_attr_initialize(pthread_attr_t *attr, cpu_set_t cpuset,
                                int priority)
{
  struct sched_param param;
  int initialized = 0;
  int ret;

  ret = pthread_attr_init(attr);
  if (ret == 0)
    {
      initialized = 1;
      ret = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(attr, sizeof(cpuset), &cpuset);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = priority;
      ret = pthread_attr_setschedparam(attr, &param);
    }

  if (ret != 0 && initialized)
    {
      (void)pthread_attr_destroy(attr);
    }

  return ret;
}

/* Each BP2P semaphore is private to this test and has exactly one waiter.
 * A count of -1 therefore proves that the corresponding task is blocked on
 * that exact semaphore.  Do not inspect the remote CPU's TCB here: acquiring
 * a TCB reference and then entering the global scheduler critical section can
 * contend with the semaphore block/context-switch path that this test is
 * trying to observe.
 */

static int32_t bp2p_private_waiter(sem_t *sem)
{
  int32_t observed = INT32_MAX;
  int value = 0;
  int ret;

  ret = nxsem_get_value(sem, &value);
  if (ret >= 0 && value == -1)
    {
      observed = (int32_t)value;
    }

  __asm volatile ("dmb sy" ::: "memory");
  return observed;
}

static FAR void *bp2p_initiator_task(FAR void *arg)
{
  volatile struct bk7258_ap_advanced_state_s *state = arg;
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  uint32_t cycle;
  uint32_t elapsed;
  int ret;

  if (state == NULL)
    {
      return NULL;
    }

  state->task_id[0] = (uint32_t)pthread_self();
  state->task_cpu[0] = (uint32_t)up_cpu_index();
  state->task_started[0]++;
  __asm volatile ("dmb sy" ::: "memory");

  for (cycle = 1; cycle <= BK7258_AP_ADV_CYCLES; cycle++)
    {
      /* Wait for responder to post (wake us). */

      for (elapsed = 0; elapsed < BK7258_AP_ADV_TIMEOUT_MS; elapsed++)
        {
          __asm volatile ("dmb sy" ::: "memory");
          if (state->state == BK7258_AP_BP2P_STATE_FAILED)
            {
              goto done;
            }

          if (state->sequence[1] == cycle - 1 &&
              state->task_completed[1] == 0)
            {
              break;
            }

          up_mdelay(1);
        }

      if (elapsed == BK7258_AP_ADV_TIMEOUT_MS &&
          cycle > 1)
        {
          state->error = BK7258_AP_BP2P_ERROR_WAIT_TIMEOUT;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      /* Block on our semaphore; responder will post us. */

      state->value[0] = INT32_MAX;
      __asm volatile ("dmb sy; sev" ::: "memory");
      ret = nxsem_wait_uninterruptible(&g_bp2p_sem[0]);
      __asm volatile ("dmb sy" ::: "memory");
      state->value[0] = (int32_t)ret;

      if (ret < 0)
        {
          state->error = BK7258_AP_BP2P_ERROR_SEM_POST;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      if (up_cpu_index() != BK7258_AP_PRIMARY_CPU)
        {
          state->error = BK7258_AP_BP2P_ERROR_BAD_CPU;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      state->sequence[0] = cycle;
      __asm volatile ("dmb sy" ::: "memory");

      /* Now prove responder is waiting before posting. */

      for (elapsed = 0; elapsed < BK7258_AP_ADV_TIMEOUT_MS; elapsed++)
        {
          if (state->task_id[1] != 0 &&
              state->task_started[1] != 0 &&
              state->task_cpu[1] == BK7258_AP_SECONDARY_CPU &&
              bp2p_private_waiter(&g_bp2p_sem[1]) == -1)
            {
              break;
            }

          if (state->state == BK7258_AP_BP2P_STATE_FAILED)
            {
              goto done;
            }

          up_mdelay(1);
        }

      if (elapsed == BK7258_AP_ADV_TIMEOUT_MS)
        {
          state->error = BK7258_AP_BP2P_ERROR_WAIT_TIMEOUT;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      /* Post responder. */

      ret = nxsem_post(&g_bp2p_sem[1]);
      if (ret < 0)
        {
          state->error = BK7258_AP_BP2P_ERROR_SEM_POST;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }
    }

done:
  state->task_completed[0]++;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return NULL;
}

static FAR void *bp2p_responder_task(FAR void *arg)
{
  volatile struct bk7258_ap_advanced_state_s *state = arg;
  uint32_t cycle;
  uint32_t elapsed;
  int ret;

  if (state == NULL)
    {
      return NULL;
    }

  state->task_id[1] = (uint32_t)pthread_self();
  state->task_cpu[1] = (uint32_t)up_cpu_index();
  state->task_started[1]++;
  __asm volatile ("dmb sy" ::: "memory");

  for (cycle = 1; cycle <= BK7258_AP_ADV_CYCLES; cycle++)
    {
      /* Prove initiator is waiting before posting. */

      for (elapsed = 0; elapsed < BK7258_AP_ADV_TIMEOUT_MS; elapsed++)
        {
          if (state->task_id[0] != 0 &&
              state->task_started[0] != 0 &&
              state->task_cpu[0] == BK7258_AP_PRIMARY_CPU &&
              bp2p_private_waiter(&g_bp2p_sem[0]) == -1)
            {
              break;
            }

          if (state->state == BK7258_AP_BP2P_STATE_FAILED)
            {
              goto done;
            }

          up_mdelay(1);
        }

      if (elapsed == BK7258_AP_ADV_TIMEOUT_MS)
        {
          state->error = BK7258_AP_BP2P_ERROR_WAIT_TIMEOUT;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      /* Post initiator. */

      ret = nxsem_post(&g_bp2p_sem[0]);
      if (ret < 0)
        {
          state->error = BK7258_AP_BP2P_ERROR_SEM_POST;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      /* Block on our semaphore; initiator will post us after waking. */

      state->value[1] = INT32_MAX;
      __asm volatile ("dmb sy; sev" ::: "memory");
      ret = nxsem_wait_uninterruptible(&g_bp2p_sem[1]);
      __asm volatile ("dmb sy" ::: "memory");
      state->value[1] = (int32_t)ret;

      if (ret < 0)
        {
          state->error = BK7258_AP_BP2P_ERROR_SEM_POST;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      if (up_cpu_index() != BK7258_AP_SECONDARY_CPU)
        {
          state->error = BK7258_AP_BP2P_ERROR_BAD_CPU;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      state->sequence[1] = cycle;
      __asm volatile ("dmb sy; sev" ::: "memory");
    }

done:
  state->task_completed[1]++;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return NULL;
}

int bk7258_ap_smp_bp2p_selftest(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_advanced_state_s *state = bk7258_ap_bp2p_state();
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
  volatile struct bk7258_ap_ipi_state_s *ipi = bk7258_ap_ipi_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  pthread_attr_t attr;
  pthread_t thread[2];
  struct sched_param controller_param;
  cpu_set_t cpuset0 = (cpu_set_t)(1u << BK7258_AP_PRIMARY_CPU);
  cpu_set_t cpuset1 = (cpu_set_t)(1u << BK7258_AP_SECONDARY_CPU);
  uint32_t elapsed;
  int controller_policy;
  int priority_max;
  int task_priority;
  int ret;
  int i;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_ADV_TIMEOUT_MS;
    }

  /* Validate N8-C4 prerequisites. */

  if (smp->magic != BK7258_AP_SMP_STATE_MAGIC ||
      smp->version != BK7258_AP_SMP_STATE_VERSION ||
      smp->size != sizeof(struct bk7258_ap_smp_state_s) ||
      smp->generation != bk7258_ap_boot_state()->generation ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != 0x3u ||
      ipi->magic != BK7258_AP_IPI_STATE_MAGIC ||
      ipi->version != BK7258_AP_IPI_STATE_VERSION ||
      ipi->size != sizeof(struct bk7258_ap_ipi_state_s) ||
      ipi->generation != bk7258_ap_boot_state()->generation ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != 0x3u)
    {
      return -EAGAIN;
    }

  /* Initialize shared state. */

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_AP_BP2P_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = bk7258_ap_boot_state()->generation;
  state->state = BK7258_AP_BP2P_STATE_INITIALIZING;
  state->requested = BK7258_AP_ADV_CYCLES;
  state->smp_tx0_before = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_before = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_before = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_before = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_before = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_AP_BP2P_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

  /* Both diagnostic tasks must outrank the CPU0 controller.  Otherwise a
   * CPU1 post can make the CPU0 initiator ready without requiring an
   * immediate reverse scheduler IPI, and the local CPU0 timer will run it
   * later instead of proving the intended CPU1-to-CPU0 remote wake.
   */

  ret = pthread_getschedparam(pthread_self(), &controller_policy,
                              &controller_param);
  priority_max = sched_get_priority_max(SCHED_FIFO);
  if (ret != 0 || priority_max < 0 ||
      controller_param.sched_priority >= priority_max)
    {
      state->error = BK7258_AP_BP2P_ERROR_CREATE;
      state->state = BK7258_AP_BP2P_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return ret != 0 ? -ret : -ERANGE;
    }

  (void)controller_policy;
  task_priority = controller_param.sched_priority + 1;

  /* Initialize two semaphores with count 0. */

  for (i = 0; i < 2; i++)
    {
      ret = nxsem_init(&g_bp2p_sem[i], 0, 0);
      if (ret < 0)
        {
          state->error = BK7258_AP_BP2P_ERROR_SEM_INIT;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          return ret;
        }

#ifdef CONFIG_PRIORITY_INHERITANCE
      ret = nxsem_set_protocol(&g_bp2p_sem[i], SEM_PRIO_NONE);
      if (ret < 0)
        {
          state->error = BK7258_AP_BP2P_ERROR_SEM_INIT;
          state->state = BK7258_AP_BP2P_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          return ret;
        }
#endif
    }

  /* Create responder first (mask 0x2/CPU1). */

  ret = bp2p_attr_initialize(&attr, cpuset1, task_priority);
  if (ret != 0)
    {
      state->error = BK7258_AP_BP2P_ERROR_CREATE;
      state->state = BK7258_AP_BP2P_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  state->state = BK7258_AP_BP2P_STATE_RUNNING;
  __asm volatile ("dmb sy" ::: "memory");

  ret = pthread_create(&thread[1], &attr, bp2p_responder_task,
                       (FAR void *)state);
  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BP2P_ERROR_CREATE;
      state->state = BK7258_AP_BP2P_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  state->task_id[1] = (uint32_t)thread[1];
  __asm volatile ("dmb sy" ::: "memory");

  /* Create initiator (mask 0x1/CPU0). */

  ret = bp2p_attr_initialize(&attr, cpuset0, task_priority);
  if (ret != 0)
    {
      state->error = BK7258_AP_BP2P_ERROR_CREATE;
      state->state = BK7258_AP_BP2P_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  ret = pthread_create(&thread[0], &attr, bp2p_initiator_task,
                       (FAR void *)state);
  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BP2P_ERROR_CREATE;
      state->state = BK7258_AP_BP2P_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  state->task_id[0] = (uint32_t)thread[0];
  __asm volatile ("dmb sy" ::: "memory");

  /* Wait for both tasks to complete and PIDs to be released. */

  for (elapsed = 0; elapsed < timeout_ms * 3; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->task_completed[0] != 0 && state->task_completed[1] != 0)
        {
          if (bk7258_ap_pid_released(thread[0]) &&
              bk7258_ap_pid_released(thread[1]))
            {
              state->aux[0] = 1;
              state->aux[1] = 1;
              break;
            }
        }

      if (state->state == BK7258_AP_BP2P_STATE_FAILED)
        {
          return -EIO;
        }

      /* The diagnostic tasks outrank this CPU0 controller, so task creation
       * and each remote semaphore post can preempt it directly.  Keep the
       * controller out of the scheduler-locked sleep path, but explicitly
       * re-enter local scheduling after each bounded poll.  A CPU1 scheduler
       * IPI can make the CPU0 initiator ready just before exception return;
       * sched_yield() closes that missed-switch window without sleeping.
       */

      up_mdelay(1);
      (void)sched_yield();
    }

  if (elapsed == timeout_ms * 3)
    {
      state->error = BK7258_AP_BP2P_ERROR_WAIT_TIMEOUT;
      state->state = BK7258_AP_BP2P_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ETIMEDOUT;
    }

  /* Verify final counter deltas. */

  __asm volatile ("dmb sy" ::: "memory");
  state->smp_tx0_after = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_after = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_after = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_after = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_after = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");

  if (state->state == BK7258_AP_BP2P_STATE_FAILED)
    {
      return -EIO;
    }

  /* CPU0->CPU1: +9 (1 dispatch + 8 wakes), CPU1->CPU0: +8, calls: +17 */

  if (state->smp_tx0_after != state->smp_tx0_before + 9 ||
      state->smp_rx1_after != state->smp_rx1_before + 9 ||
      state->smp_tx1_after != state->smp_tx1_before + 8 ||
      state->smp_rx0_after != state->smp_rx0_before + 8 ||
      state->calls_after != state->calls_before + 17)
    {
      state->error = BK7258_AP_BP2P_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_BP2P_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  if (state->task_started[0] != 1 || state->task_started[1] != 1 ||
      state->task_completed[0] != 1 || state->task_completed[1] != 1 ||
      state->sequence[0] != BK7258_AP_ADV_CYCLES ||
      state->sequence[1] != BK7258_AP_ADV_CYCLES ||
      state->task_cpu[0] != BK7258_AP_PRIMARY_CPU ||
      state->task_cpu[1] != BK7258_AP_SECONDARY_CPU)
    {
      state->error = BK7258_AP_BP2P_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_BP2P_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  state->completed = BK7258_AP_ADV_CYCLES;
  state->error = BK7258_AP_BP2P_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_BP2P_STATE_PASSED;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

#endif /* CONFIG_BK7258_AP_SMP_BIDIR_PINGPONG */

/****************************************************************************
 * N8-C6 Dual CPU1 Local Scheduling
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_CPU1_DUALTASK

static int32_t bdul_exact_waiter(sem_t *sem, pthread_t thread,
                                  cpu_set_t cpuset)
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
  ret = nxsem_get_value(sem, &value);
  if (ret >= 0 &&
      tcb->task_state == TSTATE_WAIT_SEM &&
      tcb->waitobj == (FAR void *)sem &&
      value == -1 && tcb->affinity == cpuset)
    {
      observed = (int32_t)value;
    }

  leave_critical_section(flags);
  nxsched_put_tcb(tcb);
  __asm volatile ("dmb sy" ::: "memory");
  return observed;
}

static int bk7258_ap_bdul_pid_released(pthread_t thread)
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

/* Task A runs on CPU1, executes 8 times, posts B each round. */

static FAR void *bdul_task_a(FAR void *arg)
{
  volatile struct bk7258_ap_advanced_state_s *state = arg;
  uint32_t cycle;
  uint32_t elapsed;
  int ret;

  if (state == NULL)
    {
      return NULL;
    }

  state->task_id[0] = (uint32_t)pthread_self();
  state->task_cpu[0] = (uint32_t)up_cpu_index();
  state->task_started[0]++;
  __asm volatile ("dmb sy" ::: "memory");

  /* Wait to be started by CPU0 post. */

  ret = nxsem_wait_uninterruptible(&g_bdul_sem[0]);
  if (ret < 0)
    {
      state->error = BK7258_AP_BDUL_ERROR_SEM_POST;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      goto done;
    }

  for (cycle = 1; cycle <= BK7258_AP_ADV_CYCLES; cycle++)
    {
      if (up_cpu_index() != BK7258_AP_SECONDARY_CPU)
        {
          state->error = BK7258_AP_BDUL_ERROR_BAD_CPU;
          state->state = BK7258_AP_BDUL_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      state->sequence[0] = cycle;
      __asm volatile ("dmb sy" ::: "memory");

      /* Prove B is waiting before posting. */

      for (elapsed = 0; elapsed < BK7258_AP_ADV_TIMEOUT_MS; elapsed++)
        {
          if (state->task_id[1] != 0 &&
              bdul_exact_waiter(&g_bdul_sem[1],
                                (pthread_t)state->task_id[1],
                                (cpu_set_t)(1u <<
                                  BK7258_AP_SECONDARY_CPU)) == -1)
            {
              break;
            }

          if (state->state == BK7258_AP_BDUL_STATE_FAILED)
            {
              goto done;
            }

          up_mdelay(1);
          (void)sched_yield();
        }

      if (elapsed == BK7258_AP_ADV_TIMEOUT_MS)
        {
          state->error = BK7258_AP_BDUL_ERROR_WAIT_TIMEOUT;
          state->state = BK7258_AP_BDUL_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      ret = nxsem_post(&g_bdul_sem[1]);
      if (ret < 0)
        {
          state->error = BK7258_AP_BDUL_ERROR_SEM_POST;
          state->state = BK7258_AP_BDUL_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      /* Wait for B to post us back (rounds 1..7) or done (round 8). */

      if (cycle < BK7258_AP_ADV_CYCLES)
        {
          for (elapsed = 0; elapsed < BK7258_AP_ADV_TIMEOUT_MS; elapsed++)
            {
              __asm volatile ("dmb sy" ::: "memory");
              if (state->sequence[1] == cycle)
                {
                  break;
                }

              if (state->state == BK7258_AP_BDUL_STATE_FAILED)
                {
                  goto done;
                }

              up_mdelay(1);
              (void)sched_yield();
            }

          if (elapsed == BK7258_AP_ADV_TIMEOUT_MS)
            {
              state->error = BK7258_AP_BDUL_ERROR_WAIT_TIMEOUT;
              state->state = BK7258_AP_BDUL_STATE_FAILED;
              __asm volatile ("dmb sy; sev" ::: "memory");
              goto done;
            }

          ret = nxsem_wait_uninterruptible(&g_bdul_sem[0]);
          if (ret < 0)
            {
              state->error = BK7258_AP_BDUL_ERROR_SEM_POST;
              state->state = BK7258_AP_BDUL_STATE_FAILED;
              __asm volatile ("dmb sy; sev" ::: "memory");
              goto done;
            }
        }
    }

done:
  state->task_completed[0]++;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return NULL;
}

/* Task B runs on CPU1, executes 8 times, posts A rounds 1..7 only. */

static FAR void *bdul_task_b(FAR void *arg)
{
  volatile struct bk7258_ap_advanced_state_s *state = arg;
  uint32_t cycle;
  uint32_t elapsed;
  int ret;

  if (state == NULL)
    {
      return NULL;
    }

  state->task_id[1] = (uint32_t)pthread_self();
  state->task_cpu[1] = (uint32_t)up_cpu_index();
  state->task_started[1]++;
  __asm volatile ("dmb sy" ::: "memory");

  for (cycle = 1; cycle <= BK7258_AP_ADV_CYCLES; cycle++)
    {
      /* Wait for A to post us. */

      ret = nxsem_wait_uninterruptible(&g_bdul_sem[1]);
      if (ret < 0)
        {
          state->error = BK7258_AP_BDUL_ERROR_SEM_POST;
          state->state = BK7258_AP_BDUL_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      if (up_cpu_index() != BK7258_AP_SECONDARY_CPU)
        {
          state->error = BK7258_AP_BDUL_ERROR_BAD_CPU;
          state->state = BK7258_AP_BDUL_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      state->sequence[1] = cycle;
      __asm volatile ("dmb sy" ::: "memory");

      /* Post A back for rounds 1..7.  Round 8: no post. */

      if (cycle < BK7258_AP_ADV_CYCLES)
        {
          /* Prove A is waiting before posting. */

          for (elapsed = 0; elapsed < BK7258_AP_ADV_TIMEOUT_MS; elapsed++)
            {
              if (state->task_id[0] != 0 &&
                  bdul_exact_waiter(&g_bdul_sem[0],
                                    (pthread_t)state->task_id[0],
                                    (cpu_set_t)(1u <<
                                      BK7258_AP_SECONDARY_CPU)) == -1)
                {
                  break;
                }

              if (state->state == BK7258_AP_BDUL_STATE_FAILED)
                {
                  goto done;
                }

              up_mdelay(1);
              (void)sched_yield();
            }

          if (elapsed == BK7258_AP_ADV_TIMEOUT_MS)
            {
              state->error = BK7258_AP_BDUL_ERROR_WAIT_TIMEOUT;
              state->state = BK7258_AP_BDUL_STATE_FAILED;
              __asm volatile ("dmb sy; sev" ::: "memory");
              goto done;
            }

          ret = nxsem_post(&g_bdul_sem[0]);
          if (ret < 0)
            {
              state->error = BK7258_AP_BDUL_ERROR_SEM_POST;
              state->state = BK7258_AP_BDUL_STATE_FAILED;
              __asm volatile ("dmb sy; sev" ::: "memory");
              goto done;
            }
        }
    }

done:
  state->task_completed[1]++;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return NULL;
}

int bk7258_ap_smp_bdul_selftest(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_advanced_state_s *state = bk7258_ap_bdul_state();
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
  volatile struct bk7258_ap_ipi_state_s *ipi = bk7258_ap_ipi_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  pthread_attr_t attr;
  pthread_t thread[2];
  cpu_set_t cpuset1 = (cpu_set_t)(1u << BK7258_AP_SECONDARY_CPU);
  uint32_t elapsed;
  int ret;
  int i;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_ADV_TIMEOUT_MS;
    }

  /* Validate N8-C4 prerequisites. */

  if (smp->magic != BK7258_AP_SMP_STATE_MAGIC ||
      smp->version != BK7258_AP_SMP_STATE_VERSION ||
      smp->size != sizeof(struct bk7258_ap_smp_state_s) ||
      smp->generation != bk7258_ap_boot_state()->generation ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != 0x3u ||
      ipi->magic != BK7258_AP_IPI_STATE_MAGIC ||
      ipi->version != BK7258_AP_IPI_STATE_VERSION ||
      ipi->size != sizeof(struct bk7258_ap_ipi_state_s) ||
      ipi->generation != bk7258_ap_boot_state()->generation ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != 0x3u)
    {
      return -EAGAIN;
    }

  /* Initialize shared state. */

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_AP_BDUL_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = bk7258_ap_boot_state()->generation;
  state->state = BK7258_AP_BDUL_STATE_INITIALIZING;
  state->requested = BK7258_AP_ADV_CYCLES;
  state->smp_tx0_before = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_before = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_before = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_before = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_before = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_AP_BDUL_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

  /* Initialize two semaphores with count 0. */

  for (i = 0; i < 2; i++)
    {
      ret = nxsem_init(&g_bdul_sem[i], 0, 0);
      if (ret < 0)
        {
          state->error = BK7258_AP_BDUL_ERROR_SEM_INIT;
          state->state = BK7258_AP_BDUL_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          return ret;
        }

#ifdef CONFIG_PRIORITY_INHERITANCE
      ret = nxsem_set_protocol(&g_bdul_sem[i], SEM_PRIO_NONE);
      if (ret < 0)
        {
          state->error = BK7258_AP_BDUL_ERROR_SEM_INIT;
          state->state = BK7258_AP_BDUL_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          return ret;
        }
#endif
    }

  /* Create task B first (mask 0x2/CPU1). */

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BDUL_ERROR_CREATE;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  (void)pthread_attr_setaffinity_np(&attr, sizeof(cpuset1), &cpuset1);
  state->state = BK7258_AP_BDUL_STATE_RUNNING;
  __asm volatile ("dmb sy" ::: "memory");

  ret = pthread_create(&thread[1], &attr, bdul_task_b,
                       (FAR void *)state);
  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BDUL_ERROR_CREATE;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  state->task_id[1] = (uint32_t)thread[1];
  __asm volatile ("dmb sy" ::: "memory");

  /* Create task A (mask 0x2/CPU1). */

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BDUL_ERROR_CREATE;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  (void)pthread_attr_setaffinity_np(&attr, sizeof(cpuset1), &cpuset1);

  ret = pthread_create(&thread[0], &attr, bdul_task_a,
                       (FAR void *)state);
  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BDUL_ERROR_CREATE;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  state->task_id[0] = (uint32_t)thread[0];
  __asm volatile ("dmb sy" ::: "memory");

  /* Prove task A is blocked before CPU0 performs the only starter wake. */

  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      if (bdul_exact_waiter(&g_bdul_sem[0], thread[0], cpuset1) == -1)
        {
          break;
        }

      if (state->state == BK7258_AP_BDUL_STATE_FAILED)
        {
          return -EIO;
        }

      up_mdelay(1);
    }

  if (elapsed == timeout_ms)
    {
      state->error = BK7258_AP_BDUL_ERROR_WAIT_TIMEOUT;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ETIMEDOUT;
    }

  /* Post task A to start. */

  ret = nxsem_post(&g_bdul_sem[0]);
  if (ret < 0)
    {
      state->error = BK7258_AP_BDUL_ERROR_SEM_POST;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return ret;
    }

  /* Wait for both tasks to complete and PIDs to be released. */

  for (elapsed = 0; elapsed < timeout_ms * 3; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->task_completed[0] != 0 && state->task_completed[1] != 0)
        {
          if (bk7258_ap_bdul_pid_released(thread[0]) &&
              bk7258_ap_bdul_pid_released(thread[1]))
            {
              state->aux[0] = 1;
              state->aux[1] = 1;
              break;
            }
        }

      if (state->state == BK7258_AP_BDUL_STATE_FAILED)
        {
          return -EIO;
        }

      up_mdelay(1);
    }

  if (elapsed == timeout_ms * 3)
    {
      state->error = BK7258_AP_BDUL_ERROR_WAIT_TIMEOUT;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ETIMEDOUT;
    }

  /* Verify final counter deltas: CPU0->CPU1 +3, CPU1->CPU0 +0, calls +3 */

  __asm volatile ("dmb sy" ::: "memory");
  state->smp_tx0_after = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_after = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_after = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_after = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_after = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");

  if (state->state == BK7258_AP_BDUL_STATE_FAILED)
    {
      return -EIO;
    }

  if (state->smp_tx0_after != state->smp_tx0_before + 3 ||
      state->smp_rx1_after != state->smp_rx1_before + 3 ||
      state->smp_tx1_after != state->smp_tx1_before ||
      state->smp_rx0_after != state->smp_rx0_before ||
      state->calls_after != state->calls_before + 3)
    {
      state->error = BK7258_AP_BDUL_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  if (state->task_started[0] != 1 || state->task_started[1] != 1 ||
      state->task_completed[0] != 1 || state->task_completed[1] != 1 ||
      state->sequence[0] != BK7258_AP_ADV_CYCLES ||
      state->sequence[1] != BK7258_AP_ADV_CYCLES ||
      state->task_cpu[0] != BK7258_AP_SECONDARY_CPU ||
      state->task_cpu[1] != BK7258_AP_SECONDARY_CPU)
    {
      state->error = BK7258_AP_BDUL_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_BDUL_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  state->completed = BK7258_AP_ADV_CYCLES;
  state->error = BK7258_AP_BDUL_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_BDUL_STATE_PASSED;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

#endif /* CONFIG_BK7258_AP_SMP_CPU1_DUALTASK */

/****************************************************************************
 * N8-C7 Controlled Migration
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_CONTROLLED_MIGRATION

static int bmig_callback_fail(FAR struct bmig_rendezvous_s *rendezvous,
                              uint32_t error, int ret)
{
  volatile struct bk7258_ap_advanced_state_s *state = rendezvous->state;

  if (state != NULL)
    {
      state->error = error;
      __asm volatile ("dmb sy" ::: "memory");
      state->state = BK7258_AP_BMIG_STATE_FAILED;
    }

  rendezvous->callback_completed = rendezvous->cycle;
  __asm volatile ("dmb sy" ::: "memory");
  (void)nxsem_post(&g_bmig_sem);
  __asm volatile ("sev" ::: "memory");
  return ret;
}

static int bmig_migration_callback(FAR void *arg)
{
  FAR struct bmig_rendezvous_s *rendezvous = arg;
  volatile struct bk7258_ap_advanced_state_s *state;
  FAR struct tcb_s *tcb;
  irqstate_t flags;
  uint32_t cycle;
  uint32_t elapsed;
  int blocked = 0;
  int value = 0;
  int ret;

  if (rendezvous == NULL || rendezvous->state == NULL)
    {
      return -EINVAL;
    }

  state = rendezvous->state;
  cycle = rendezvous->cycle;
  if (cycle == 0 || up_cpu_index() != rendezvous->target_cpu)
    {
      return bmig_callback_fail(rendezvous,
                                BK7258_AP_BMIG_ERROR_BAD_CPU, -EIO);
    }

  rendezvous->callback_started = cycle;
  state->value[0] = (int32_t)cycle;
  __asm volatile ("dmb sy; sev" ::: "memory");

  tcb = nxsched_get_tcb((pid_t)rendezvous->thread);
  if (tcb == NULL)
    {
      return bmig_callback_fail(rendezvous,
                                BK7258_AP_BMIG_ERROR_BAD_STATE, -ESRCH);
    }

  /* Wait until the migration task is exactly blocked.  Changing a blocked
   * task's affinity avoids the unsupported running-task remote-delivery path.
   */

  for (elapsed = 0; elapsed < BK7258_AP_ADV_TIMEOUT_MS; elapsed++)
    {
      /* Poll only the atomic semaphore count until the task has decremented
       * it.  Taking an outer critical section in the target IRQ before that
       * point can prevent the source CPU from completing its blocking path.
       */

      ret = nxsem_get_value(&g_bmig_sem, &value);
      if (ret >= 0 && value == -1)
        {
          blocked = 1;
          break;
        }

      if (state->state == BK7258_AP_BMIG_STATE_FAILED)
        {
          nxsched_put_tcb(tcb);
          return -EIO;
        }

      up_mdelay(1);
    }

  if (blocked)
    {
      /* Once the atomic count proves a waiter exists, take the scheduler
       * critical section exactly once to prove the TCB and wait object.
       */

      flags = enter_critical_section();
      ret = nxsem_get_value(&g_bmig_sem, &value);
      blocked = ret >= 0 && tcb->task_state == TSTATE_WAIT_SEM &&
                tcb->waitobj == (FAR void *)&g_bmig_sem && value == -1;
      leave_critical_section(flags);
    }

  nxsched_put_tcb(tcb);
  if (!blocked)
    {
      return bmig_callback_fail(rendezvous,
                                BK7258_AP_BMIG_ERROR_TIMEOUT,
                                -ETIMEDOUT);
    }

  /* Update the blocked task's mask from the target CPU, then post the same
   * semaphore locally so the task resumes on that target CPU.
   */

  ret = pthread_setaffinity_np(rendezvous->thread,
                               sizeof(rendezvous->target),
                               &rendezvous->target);
  if (ret != 0)
    {
      return bmig_callback_fail(rendezvous,
                                BK7258_AP_BMIG_ERROR_SETAFFINITY, -ret);
    }

  tcb = nxsched_get_tcb((pid_t)rendezvous->thread);
  if (tcb == NULL)
    {
      return bmig_callback_fail(rendezvous,
                                BK7258_AP_BMIG_ERROR_BAD_STATE, -ESRCH);
    }

  flags = enter_critical_section();
  ret = nxsem_get_value(&g_bmig_sem, &value);
  blocked = ret >= 0 && tcb->task_state == TSTATE_WAIT_SEM &&
            tcb->waitobj == (FAR void *)&g_bmig_sem && value == -1 &&
            tcb->affinity == rendezvous->target;
  leave_critical_section(flags);
  nxsched_put_tcb(tcb);
  if (!blocked)
    {
      return bmig_callback_fail(rendezvous,
                                BK7258_AP_BMIG_ERROR_GETAFFINITY, -EIO);
    }

  ret = nxsem_post(&g_bmig_sem);
  if (ret < 0)
    {
      return bmig_callback_fail(rendezvous,
                                BK7258_AP_BMIG_ERROR_BAD_STATE, ret);
    }

  rendezvous->callback_completed = cycle;
  state->value[1] = (int32_t)cycle;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

static FAR void *bmig_migration_task(FAR void *arg)
{
  volatile struct bk7258_ap_advanced_state_s *state = arg;
  cpu_set_t target;
  cpu_set_t observed;
  uint32_t cycle;
  int target_cpu;
  int ret;

  if (state == NULL)
    {
      return NULL;
    }

  state->task_id[0] = (uint32_t)pthread_self();
  state->task_cpu[0] = (uint32_t)up_cpu_index();
  state->task_started[0]++;
  __asm volatile ("dmb sy" ::: "memory");

  for (cycle = 1; cycle <= BK7258_AP_ADV_CYCLES; cycle++)
    {
      /* Odd transitions target 0x2/CPU1, even target 0x1/CPU0. */

      target_cpu = (cycle & 1u) != 0 ? BK7258_AP_SECONDARY_CPU :
                   BK7258_AP_PRIMARY_CPU;
      target = (cpu_set_t)(1u << target_cpu);

      memset(&g_bmig_rendezvous, 0, sizeof(g_bmig_rendezvous));
      g_bmig_rendezvous.state = state;
      g_bmig_rendezvous.thread = pthread_self();
      g_bmig_rendezvous.target = target;
      g_bmig_rendezvous.target_cpu = (uint32_t)target_cpu;
      g_bmig_rendezvous.cycle = cycle;
      __asm volatile ("dmb sy" ::: "memory");

      nxsched_smp_call_init(&g_bmig_call, bmig_migration_callback,
                            &g_bmig_rendezvous);
      ret = nxsched_smp_call_single_async(target_cpu, &g_bmig_call);
      if (ret < 0)
        {
          state->error = BK7258_AP_BMIG_ERROR_BAD_STATE;
          state->state = BK7258_AP_BMIG_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      /* Block immediately after queueing the target callback.  Waiting for a
       * callback-start acknowledgement here can time out before a delayed
       * target IPI is serviced.  The callback already tolerates either order:
       * it waits until this exact task reaches the semaphore.
       */

      ret = nxsem_wait_uninterruptible(&g_bmig_sem);
      if (ret < 0)
        {
          state->error = BK7258_AP_BMIG_ERROR_BAD_STATE;
          state->state = BK7258_AP_BMIG_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (state->state == BK7258_AP_BMIG_STATE_FAILED)
        {
          goto done;
        }

      if (g_bmig_rendezvous.callback_completed != cycle)
        {
          state->error = BK7258_AP_BMIG_ERROR_TIMEOUT;
          state->state = BK7258_AP_BMIG_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      /* Verify affinity and CPU after the target-local wake. */

      observed = 0;
      ret = pthread_getaffinity_np(pthread_self(), sizeof(observed),
                                   &observed);
      if (ret != 0 || observed != target)
        {
          state->error = BK7258_AP_BMIG_ERROR_GETAFFINITY;
          state->state = BK7258_AP_BMIG_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      if (up_cpu_index() != target_cpu)
        {
          state->error = BK7258_AP_BMIG_ERROR_BAD_CPU;
          state->state = BK7258_AP_BMIG_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      state->sequence[0] = cycle;
      state->task_cpu[0] = (uint32_t)up_cpu_index();
      __asm volatile ("dmb sy; sev" ::: "memory");
    }

done:
  state->task_completed[0]++;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return NULL;
}

static int bk7258_ap_bmig_pid_released(pthread_t thread)
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

int bk7258_ap_smp_bmig_selftest(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_advanced_state_s *state = bk7258_ap_bmig_state();
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
  volatile struct bk7258_ap_ipi_state_s *ipi = bk7258_ap_ipi_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  pthread_attr_t attr;
  pthread_t thread;
  cpu_set_t cpuset = (cpu_set_t)(1u << BK7258_AP_PRIMARY_CPU);
  uint32_t elapsed;
  int ret;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_ADV_TIMEOUT_MS;
    }

  /* Validate N8-C4 prerequisites. */

  if (smp->magic != BK7258_AP_SMP_STATE_MAGIC ||
      smp->version != BK7258_AP_SMP_STATE_VERSION ||
      smp->size != sizeof(struct bk7258_ap_smp_state_s) ||
      smp->generation != bk7258_ap_boot_state()->generation ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != 0x3u ||
      ipi->magic != BK7258_AP_IPI_STATE_MAGIC ||
      ipi->version != BK7258_AP_IPI_STATE_VERSION ||
      ipi->size != sizeof(struct bk7258_ap_ipi_state_s) ||
      ipi->generation != bk7258_ap_boot_state()->generation ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != 0x3u)
    {
      return -EAGAIN;
    }

  /* Initialize shared state. */

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_AP_BMIG_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = bk7258_ap_boot_state()->generation;
  state->state = BK7258_AP_BMIG_STATE_INITIALIZING;
  state->requested = BK7258_AP_ADV_CYCLES;
  state->smp_tx0_before = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_before = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_before = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_before = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_before = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_AP_BMIG_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

  memset(&g_bmig_rendezvous, 0, sizeof(g_bmig_rendezvous));
  ret = nxsem_init(&g_bmig_sem, 0, 0);
  if (ret < 0)
    {
      state->error = BK7258_AP_BMIG_ERROR_BAD_STATE;
      state->state = BK7258_AP_BMIG_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return ret;
    }

#ifdef CONFIG_PRIORITY_INHERITANCE
  ret = nxsem_set_protocol(&g_bmig_sem, SEM_PRIO_NONE);
  if (ret < 0)
    {
      state->error = BK7258_AP_BMIG_ERROR_BAD_STATE;
      state->state = BK7258_AP_BMIG_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return ret;
    }
#endif

  /* Create detached task with mask 0x1/CPU0. */

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BMIG_ERROR_CREATE;
      state->state = BK7258_AP_BMIG_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  (void)pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
  state->state = BK7258_AP_BMIG_STATE_RUNNING;
  __asm volatile ("dmb sy" ::: "memory");

  ret = pthread_create(&thread, &attr, bmig_migration_task,
                       (FAR void *)state);
  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BMIG_ERROR_CREATE;
      state->state = BK7258_AP_BMIG_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  state->task_id[0] = (uint32_t)thread;
  __asm volatile ("dmb sy" ::: "memory");

  /* Wait for task to complete and PID to be released. */

  for (elapsed = 0; elapsed < timeout_ms * 3; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->task_completed[0] != 0)
        {
          if (bk7258_ap_bmig_pid_released(thread))
            {
              state->aux[0] = 1;
              break;
            }
        }

      if (state->state == BK7258_AP_BMIG_STATE_FAILED)
        {
          return -EIO;
        }

      /* The controller and migration task both start on CPU0. */

      (void)nxsig_usleep(1000);
    }

  if (elapsed == timeout_ms * 3)
    {
      state->error = BK7258_AP_BMIG_ERROR_TIMEOUT;
      state->state = BK7258_AP_BMIG_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ETIMEDOUT;
    }

  /* Verify: tx/rx +4 each direction, calls +8. */

  __asm volatile ("dmb sy" ::: "memory");
  state->smp_tx0_after = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_after = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_after = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_after = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_after = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");

  if (state->state == BK7258_AP_BMIG_STATE_FAILED)
    {
      return -EIO;
    }

  if (state->smp_tx0_after != state->smp_tx0_before + 4 ||
      state->smp_rx1_after != state->smp_rx1_before + 4 ||
      state->smp_tx1_after != state->smp_tx1_before + 4 ||
      state->smp_rx0_after != state->smp_rx0_before + 4 ||
      state->calls_after != state->calls_before + 8)
    {
      state->error = BK7258_AP_BMIG_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_BMIG_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  if (state->task_started[0] != 1 || state->task_completed[0] != 1 ||
      state->sequence[0] != BK7258_AP_ADV_CYCLES)
    {
      state->error = BK7258_AP_BMIG_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_BMIG_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  state->completed = BK7258_AP_ADV_CYCLES;
  state->error = BK7258_AP_BMIG_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_BMIG_STATE_PASSED;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

#endif /* CONFIG_BK7258_AP_SMP_CONTROLLED_MIGRATION */

/****************************************************************************
 * N8-C8 Timed Wake
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_CPU1_TIMED_WAKE

static FAR void *btim_timed_task(FAR void *arg)
{
  volatile struct bk7258_ap_advanced_state_s *state = arg;
  uint32_t cycle;
  int ret;

  if (state == NULL)
    {
      return NULL;
    }

  state->task_id[0] = (uint32_t)pthread_self();
  state->task_cpu[0] = (uint32_t)up_cpu_index();
  state->task_started[0]++;
  __asm volatile ("dmb sy" ::: "memory");

  for (cycle = 1; cycle <= BK7258_AP_ADV_CYCLES; cycle++)
    {
      state->value[0] = (int32_t)cycle;
      __asm volatile ("dmb sy" ::: "memory");

      ret = nxsig_usleep(BK7258_AP_ADV_TIMED_INTERVAL_US);
      __asm volatile ("dmb sy" ::: "memory");

      state->value[1] = (int32_t)ret;
      state->sequence[0] = cycle;

      if (ret < 0)
        {
          state->error = BK7258_AP_BTIM_ERROR_SLEEP;
          state->state = BK7258_AP_BTIM_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      if (up_cpu_index() != BK7258_AP_SECONDARY_CPU)
        {
          state->error = BK7258_AP_BTIM_ERROR_BAD_CPU;
          state->state = BK7258_AP_BTIM_STATE_FAILED;
          __asm volatile ("dmb sy; sev" ::: "memory");
          goto done;
        }

      __asm volatile ("dmb sy; sev" ::: "memory");
    }

done:
  state->task_completed[0]++;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return NULL;
}

static int bk7258_ap_btim_pid_released(pthread_t thread)
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

int bk7258_ap_smp_btim_selftest(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_advanced_state_s *state = bk7258_ap_btim_state();
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
  volatile struct bk7258_ap_ipi_state_s *ipi = bk7258_ap_ipi_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  pthread_attr_t attr;
  pthread_t thread;
  cpu_set_t cpuset = (cpu_set_t)(1u << BK7258_AP_SECONDARY_CPU);
  uint32_t elapsed;
  int ret;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_ADV_TIMEOUT_MS;
    }

  /* Validate N8-C4 prerequisites. */

  if (smp->magic != BK7258_AP_SMP_STATE_MAGIC ||
      smp->version != BK7258_AP_SMP_STATE_VERSION ||
      smp->size != sizeof(struct bk7258_ap_smp_state_s) ||
      smp->generation != bk7258_ap_boot_state()->generation ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != 0x3u ||
      ipi->magic != BK7258_AP_IPI_STATE_MAGIC ||
      ipi->version != BK7258_AP_IPI_STATE_VERSION ||
      ipi->size != sizeof(struct bk7258_ap_ipi_state_s) ||
      ipi->generation != bk7258_ap_boot_state()->generation ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != 0x3u)
    {
      return -EAGAIN;
    }

  /* Initialize shared state. */

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_AP_BTIM_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = bk7258_ap_boot_state()->generation;
  state->state = BK7258_AP_BTIM_STATE_INITIALIZING;
  state->requested = BK7258_AP_ADV_CYCLES;
  state->aux[0] = BK7258_AP_ADV_TIMED_INTERVAL_US;
  state->smp_tx0_before = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_before = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_before = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_before = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_before = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_AP_BTIM_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

  /* Create detached task with mask 0x2/CPU1. */

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BTIM_ERROR_CREATE;
      state->state = BK7258_AP_BTIM_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  (void)pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
  state->state = BK7258_AP_BTIM_STATE_RUNNING;
  __asm volatile ("dmb sy" ::: "memory");

  ret = pthread_create(&thread, &attr, btim_timed_task,
                       (FAR void *)state);
  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      state->error = BK7258_AP_BTIM_ERROR_CREATE;
      state->state = BK7258_AP_BTIM_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ret;
    }

  state->task_id[0] = (uint32_t)thread;
  __asm volatile ("dmb sy" ::: "memory");

  /* Wait for task to complete and PID to be released. */

  for (elapsed = 0; elapsed < timeout_ms * 3; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->task_completed[0] != 0)
        {
          if (bk7258_ap_btim_pid_released(thread))
            {
              state->aux[1] = 1;
              break;
            }
        }

      if (state->state == BK7258_AP_BTIM_STATE_FAILED)
        {
          return -EIO;
        }

      up_mdelay(1);
    }

  if (elapsed == timeout_ms * 3)
    {
      state->error = BK7258_AP_BTIM_ERROR_TIMEOUT;
      state->state = BK7258_AP_BTIM_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -ETIMEDOUT;
    }

  /* Verify: initial CPU1 dispatch + 8 timer wakes. */

  __asm volatile ("dmb sy" ::: "memory");
  state->smp_tx0_after = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_after = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_after = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_after = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_after = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");

  if (state->state == BK7258_AP_BTIM_STATE_FAILED)
    {
      return -EIO;
    }

  if (state->smp_tx0_after != state->smp_tx0_before +
                                  BK7258_AP_ADV_CYCLES + 1 ||
      state->smp_rx1_after != state->smp_rx1_before +
                                  BK7258_AP_ADV_CYCLES + 1 ||
      state->smp_tx1_after != state->smp_tx1_before ||
      state->smp_rx0_after != state->smp_rx0_before ||
      state->calls_after != state->calls_before +
                                  BK7258_AP_ADV_CYCLES + 1)
    {
      state->error = BK7258_AP_BTIM_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_BTIM_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  if (state->task_started[0] != 1 || state->task_completed[0] != 1 ||
      state->sequence[0] != BK7258_AP_ADV_CYCLES)
    {
      state->error = BK7258_AP_BTIM_ERROR_COUNT_MISMATCH;
      state->state = BK7258_AP_BTIM_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EIO;
    }

  state->completed = BK7258_AP_ADV_CYCLES;
  state->error = BK7258_AP_BTIM_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_BTIM_STATE_PASSED;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

#endif /* CONFIG_BK7258_AP_SMP_CPU1_TIMED_WAKE */

/****************************************************************************
 * N8-D1 Scheduler Quiesce/Resume Foundation
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_LIFECYCLE_QUIESCE

static void blcy_fail(
  volatile struct bk7258_ap_advanced_state_s *state, uint32_t error)
{
  if (state->error == BK7258_AP_BLCY_ERROR_NONE)
    {
      state->error = error;
    }

  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_BLCY_STATE_FAILED;
  __asm volatile ("dmb sy; sev" ::: "memory");
}

static int blcy_abort(
  volatile struct bk7258_ap_advanced_state_s *state, uint32_t error,
  int ret)
{
  blcy_fail(state, error);
  state->sequence[1] = 1;
  __asm volatile ("dsb sy; sev" ::: "memory");
  return ret;
}

static int blcy_quiesce_callback(FAR void *arg)
{
  volatile struct bk7258_ap_advanced_state_s *state = arg;
  int cpu;

  if (state == NULL)
    {
      return -EINVAL;
    }

  __asm volatile ("dmb sy" ::: "memory");
  if (state->magic != BK7258_AP_BLCY_STATE_MAGIC ||
      state->version != BK7258_AP_BLCY_STATE_VERSION ||
      state->size != sizeof(struct bk7258_ap_advanced_state_s) ||
      state->generation != bk7258_ap_boot_state()->generation ||
      state->state != BK7258_AP_BLCY_STATE_RUNNING ||
      state->error != BK7258_AP_BLCY_ERROR_NONE)
    {
      state->value[0] = -EAGAIN;
      blcy_fail(state, BK7258_AP_BLCY_ERROR_BAD_STATE);
      return -EAGAIN;
    }

  cpu = up_cpu_index();
  if (cpu != BK7258_AP_SECONDARY_CPU)
    {
      state->value[0] = -EINVAL;
      blcy_fail(state, BK7258_AP_BLCY_ERROR_BAD_CPU);
      return -EINVAL;
    }

  /* This asynchronous SMP callback remains in the CPU1 call-handler context
   * until CPU0 publishes the resume sequence and executes SEV.  It does not
   * change NuttX's online mask, disable CPU1 interrupts, reset CPU2, or claim
   * scheduler hot-unplug support.
   */

  state->task_cpu[0] = (uint32_t)cpu;
  state->task_started[0]++;
  state->sequence[0] = 1;
  state->aux[0] = 1;
  __asm volatile ("dsb sy; sev" ::: "memory");

  for (; ; )
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->state == BK7258_AP_BLCY_STATE_FAILED)
        {
          state->value[0] = -ECANCELED;
          state->task_cpu[1] = (uint32_t)up_cpu_index();
          state->task_completed[0]++;
          __asm volatile ("dmb sy; sev" ::: "memory");
          return -ECANCELED;
        }

      if (state->sequence[1] == 1)
        {
          break;
        }

      __asm volatile ("wfe" ::: "memory");
    }

  cpu = up_cpu_index();
  if (cpu != BK7258_AP_SECONDARY_CPU)
    {
      state->value[0] = -EINVAL;
      blcy_fail(state, BK7258_AP_BLCY_ERROR_BAD_CPU);
      state->task_cpu[1] = (uint32_t)cpu;
      state->task_completed[0]++;
      __asm volatile ("dmb sy; sev" ::: "memory");
      return -EINVAL;
    }

  state->task_cpu[1] = (uint32_t)cpu;
  state->aux[1] = 1;
  state->value[0] = OK;
  state->task_completed[0]++;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

int bk7258_ap_smp_blcy_selftest(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_advanced_state_s *state = bk7258_ap_blcy_state();
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
  volatile struct bk7258_ap_ipi_state_s *ipi = bk7258_ap_ipi_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  uint32_t elapsed;
  int ret;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_ADV_TIMEOUT_MS;
    }

  /* Validate N8-C4 prerequisites. */

  if (smp->magic != BK7258_AP_SMP_STATE_MAGIC ||
      smp->version != BK7258_AP_SMP_STATE_VERSION ||
      smp->size != sizeof(struct bk7258_ap_smp_state_s) ||
      smp->generation != bk7258_ap_boot_state()->generation ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != 0x3u ||
      ipi->magic != BK7258_AP_IPI_STATE_MAGIC ||
      ipi->version != BK7258_AP_IPI_STATE_VERSION ||
      ipi->size != sizeof(struct bk7258_ap_ipi_state_s) ||
      ipi->generation != bk7258_ap_boot_state()->generation ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != 0x3u)
    {
      return -EAGAIN;
    }

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_AP_BLCY_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = bk7258_ap_boot_state()->generation;
  state->state = BK7258_AP_BLCY_STATE_INITIALIZING;
  state->requested = 1;
  state->value[0] = INT32_MIN;
  state->value[1] = INT32_MIN;
  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_AP_BLCY_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

  /* Scheduler-online CPU2 stop must remain fail-closed.  This call is made
   * before the quiesce callback is dispatched and must have no hardware side
   * effects in scheduler-online mode.
   */

  ret = bk7258_ap_smp_secondary_stop(timeout_ms);
  state->value[1] = (int32_t)ret;
  __asm volatile ("dmb sy" ::: "memory");
  if (ret != -ENOTSUP)
    {
      blcy_fail(state, BK7258_AP_BLCY_ERROR_STOP_GATE);
      return -EIO;
    }

  state->smp_tx0_before = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_before = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_before = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_before = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_before = cpu2->smp_call_requests;
  state->state = BK7258_AP_BLCY_STATE_RUNNING;
  __asm volatile ("dmb sy" ::: "memory");

  nxsched_smp_call_init(&g_blcy_quiesce_call, blcy_quiesce_callback,
                        (FAR void *)state);
  ret = nxsched_smp_call_single_async(BK7258_AP_SECONDARY_CPU,
                                      &g_blcy_quiesce_call);
  if (ret < 0)
    {
      return blcy_abort(state, BK7258_AP_BLCY_ERROR_CALL, ret);
    }

  /* Wait until CPU1 has entered the bounded scheduler-quiesce callback. */

  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->task_started[0] == 1 && state->sequence[0] == 1 &&
          state->aux[0] == 1 &&
          state->task_cpu[0] == BK7258_AP_SECONDARY_CPU)
        {
          break;
        }

      if (state->state == BK7258_AP_BLCY_STATE_FAILED)
        {
          return blcy_abort(state, state->error, -EIO);
        }

      up_mdelay(1);
    }

  if (elapsed == timeout_ms)
    {
      return blcy_abort(state, BK7258_AP_BLCY_ERROR_QUIESCE_TIMEOUT,
                        -ETIMEDOUT);
    }

  /* CPU1 remains NuttX-online while its scheduler call handler is held. */

  __asm volatile ("dmb sy" ::: "memory");
  if (smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != 0x3u ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != 0x3u)
    {
      return blcy_abort(state, BK7258_AP_BLCY_ERROR_BAD_STATE, -EIO);
    }

  state->sequence[1] = 1;
  __asm volatile ("dsb sy; sev" ::: "memory");

  /* Wait until the callback resumes and exits on CPU1. */

  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->task_completed[0] == 1 && state->aux[1] == 1 &&
          state->task_cpu[1] == BK7258_AP_SECONDARY_CPU)
        {
          break;
        }

      if (state->state == BK7258_AP_BLCY_STATE_FAILED)
        {
          return blcy_abort(state, state->error, -EIO);
        }

      up_mdelay(1);
    }

  if (elapsed == timeout_ms)
    {
      return blcy_abort(state, BK7258_AP_BLCY_ERROR_RESUME_TIMEOUT,
                        -ETIMEDOUT);
    }

  __asm volatile ("dmb sy" ::: "memory");
  state->smp_tx0_after = smp->tx_count[BK7258_AP_PRIMARY_CPU];
  state->smp_rx1_after = smp->rx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_tx1_after = smp->tx_count[BK7258_AP_SECONDARY_CPU];
  state->smp_rx0_after = smp->rx_count[BK7258_AP_PRIMARY_CPU];
  state->calls_after = cpu2->smp_call_requests;
  __asm volatile ("dmb sy" ::: "memory");

  if (state->state == BK7258_AP_BLCY_STATE_FAILED)
    {
      return -EIO;
    }

  if (state->smp_tx0_after != state->smp_tx0_before + 1 ||
      state->smp_rx1_after != state->smp_rx1_before + 1 ||
      state->smp_tx1_after != state->smp_tx1_before ||
      state->smp_rx0_after != state->smp_rx0_before ||
      state->calls_after != state->calls_before + 1 ||
      state->requested != 1 || state->task_started[0] != 1 ||
      state->task_completed[0] != 1 || state->sequence[0] != 1 ||
      state->sequence[1] != 1 || state->aux[0] != 1 ||
      state->aux[1] != 1 || state->value[0] != OK ||
      state->value[1] != -ENOTSUP ||
      state->task_cpu[0] != BK7258_AP_SECONDARY_CPU ||
      state->task_cpu[1] != BK7258_AP_SECONDARY_CPU ||
      smp->state != BK7258_AP_SMP_STATE_PASSED ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != 0x3u ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      cpu2->online_mask != 0x3u)
    {
      blcy_fail(state, BK7258_AP_BLCY_ERROR_COUNT_MISMATCH);
      return -EIO;
    }

  state->completed = 1;
  state->error = BK7258_AP_BLCY_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_BLCY_STATE_PASSED;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

#endif /* CONFIG_BK7258_AP_SMP_LIFECYCLE_QUIESCE */

#endif /* any advanced config enabled */
