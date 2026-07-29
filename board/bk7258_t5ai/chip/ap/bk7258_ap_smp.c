/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/
 * bk7258_ap_smp.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N8-B1/N8-B2 physical CPU2 secondary bootstrap.  NuttX creates the logical
 * CPU1 IDLE TCB and calls up_cpu_start(), but CPU2 remains parked before
 * nx_idle_trampoline().  N8-B2 optionally enables only the SDK-wrapped
 * bidirectional mailbox IPI path while the scheduler stays on logical CPU0.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/arch.h>
#include <nuttx/sched.h>
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
#  error N8-C1 must keep ordinary tasks restricted to AP logical CPU0
#endif

#if defined(CONFIG_BK7258_AP_SMP_SCHED_ONLINE) && \
    !defined(CONFIG_BK7258_AP_IPI)
#  error BK7258 scheduler-online mode requires the SDK-wrapped AP IPI
#endif

#define BK7258_SCB_VTOR             (*(volatile uint32_t *)0xe000ed08u)
#define BK7258_SCB_CFSR             (*(volatile uint32_t *)0xe000ed28u)
#define BK7258_SCB_HFSR             (*(volatile uint32_t *)0xe000ed2cu)
#define BK7258_SCB_CPACR            (*(volatile uint32_t *)0xe000ed88u)
#define BK7258_FPU_FPCCR            (*(volatile uint32_t *)0xe000ef34u)

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

/****************************************************************************
 * External Function Prototypes / Data
 ****************************************************************************/

extern void exception_common(void);
extern const void *const
  __vector_core1_table[BK7258_CPU2_VECTOR_COUNT];

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
static struct smp_call_data_s g_bk7258_ap_smp_forward_call;
static struct smp_call_data_s g_bk7258_ap_smp_reverse_call;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_cpu2_fpu_initialize(void)
{
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
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  uint32_t current;
  uint32_t elapsed;

  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
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

      up_mdelay(1);
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
  state->online_mask = BK7258_AP_ONLINE_MASK;
  state->secondary_ready = 1;

#ifdef CONFIG_SCHED_INSTRUMENTATION
  sched_note_cpu_started(this_task());
#endif

  bk7258_ap_ipi_mark_scheduler_online();
  __asm volatile ("cpsie i; dsb sy; isb sy" ::: "memory");
  state->state = BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE;
  __asm volatile ("dmb sy; sev" ::: "memory");

  nx_idle_trampoline();

  bk7258_cpu2_fail(BK7258_CPU2_PROBE_ERROR_BAD_BOOT_STATE);
  for (; ; )
    {
      __asm volatile ("wfe");
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
