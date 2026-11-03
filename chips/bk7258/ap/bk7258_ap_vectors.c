/****************************************************************************
 * chips/bk7258/ap/bk7258_ap_vectors.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Physical CPU1 executes this AP-local logical-core-0 vector table.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_kernel_compat.h"

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/sched.h>

#include <arch/chip/bk7258_amp.h>

#include "arm_internal.h"
#include "nvic.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SCB_CFSR          (*(volatile uint32_t *)0xe000ed28u)
#define BK7258_SCB_HFSR          (*(volatile uint32_t *)0xe000ed2cu)
#define BK7258_SCB_MMFAR         (*(volatile uint32_t *)0xe000ed34u)
#define BK7258_SCB_BFAR          (*(volatile uint32_t *)0xe000ed38u)

#define BK7258_EXCEPTION_NMI              2u
#define BK7258_EXCEPTION_HARDFAULT        3u
#define BK7258_EXCEPTION_FRAME_WORDS      8u
#define BK7258_EXC_RETURN_THREAD_MODE     (1u << 3)
#define BK7258_EXC_RETURN_BASIC_FRAME     (1u << 4)
#define BK7258_FAULT_INVALID_VALUE        UINT32_MAX

/* Match the common ARM vector contract: Thread mode starts on the IDLE
 * stack, while arm_initialize_stack() moves handler mode to the dedicated
 * interrupt stack before entering __start().
 */

#define BK7258_AP_IDLE_STACK              \
  (_ebss + CONFIG_IDLETHREAD_STACKSIZE)

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

extern void exception_common(void);
extern void __start(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_ap_reset_entry(void)
{
  /* SMP stack selection calls up_cpu_index() before __start().  Publish the
   * AP-local primary ID before arm_initialize_stack() asks for CPU0's
   * interrupt stack.  The AP-UP path writes the same value and is unchanged.
   */

  *(volatile uint32_t *)BK7258_LOCAL_CORE_ID_ADDR = 0;
  __asm volatile ("dmb sy" ::: "memory");

  /* Follow the common ARM reset wrapper before entering board __start:
   * preserve the reset stack as PSP for the IDLE thread, switch MSP to the
   * dedicated interrupt stack, and terminate the backtrace chain at LR=0.
   */

  arm_initialize_stack();

  __asm volatile ("mov lr, %0\n\t"
                  "bx %1\n\t"
                  :
                  : "r"(0), "r"(__start));
}

static inline void bk7258_ap_fault_doorbell(uint32_t event,
                                            uint32_t error)
{
  volatile uint32_t *mbox =
    (volatile uint32_t *)(uintptr_t)BK7258_MBOX1_BASE;
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  state->state      = BK7258_AP_STATE_FAILED;
  state->error      = error;
  state->last_event = event;
  __asm volatile ("dmb sy" ::: "memory");
  up_clean_dcache((uintptr_t)state,
                  (uintptr_t)state + sizeof(*state));
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  mbox[BK7258_MBOX_SENDER_OFFSET / 4] = 1u << 1;
  mbox[BK7258_MBOX_RECEIVER_OFFSET / 4] = 1u << 0;
  mbox[BK7258_MBOX_PARAM0_OFFSET / 4] = BK7258_AP_DOORBELL_MAGIC;
  mbox[BK7258_MBOX_PARAM1_OFFSET / 4] = event;
  mbox[BK7258_MBOX_PARAM2_OFFSET / 4] = state->generation;
  mbox[BK7258_MBOX_PARAM3_OFFSET / 4] = state->state;
  __asm volatile ("dmb sy" ::: "memory");
  mbox[BK7258_MBOX_READY_OFFSET / 4] = BK7258_MBOX_BOX0_BIT;
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

static void __attribute__((noinline, noreturn, used))
bk7258_ap_fault_handler(uint32_t *stack, uint32_t exc_return,
                        uint32_t exception)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  volatile struct bk7258_ap_fault_state_s *fault = bk7258_ap_fault_state();
  uintptr_t frame_addr = (uintptr_t)stack;
  const volatile uint32_t *frame;
  uint32_t error;
  uint32_t stacked_r0 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_r1 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_r2 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_r3 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_r12 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_lr = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_pc = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_xpsr = BK7258_FAULT_INVALID_VALUE;
  uint32_t hfsr = BK7258_SCB_HFSR;
  uint32_t cfsr = BK7258_SCB_CFSR;
  uint32_t mmfar = BK7258_SCB_MMFAR;
  uint32_t bfar = BK7258_SCB_BFAR;

  /* ARMv8-M stacks R0..xPSR first; an extended exception frame appends
   * S0..S15/FPSCR after those eight words.  The raw SP therefore
   * always addresses the basic frame.  This also matches NuttX arm_m/irq.h.
   */

  if ((frame_addr & (sizeof(uint32_t) - 1u)) == 0 &&
      frame_addr >= BK7258_AP_RAM_BASE &&
      frame_addr <= BK7258_SHARED_RAM_BASE -
                    BK7258_EXCEPTION_FRAME_WORDS * sizeof(uint32_t))
    {
      frame = (const volatile uint32_t *)frame_addr;
      stacked_r0   = frame[0];
      stacked_r1   = frame[1];
      stacked_r2   = frame[2];
      stacked_r3   = frame[3];
      stacked_r12  = frame[4];
      stacked_lr   = frame[5];
      stacked_pc   = frame[6];
      stacked_xpsr = frame[7];
    }

  if (exception == BK7258_EXCEPTION_NMI)
    {
      error = BK7258_AP_ERROR_NMI;
    }
  else if (exception == BK7258_EXCEPTION_HARDFAULT)
    {
      error = BK7258_AP_ERROR_HARDFAULT;
    }
  else
    {
      error = BK7258_AP_ERROR_BAD_BOOT_STATE;
    }

  /* Preserve the four-word quick dump at 0x2809f070. */

  state->reserved[0] = exception;
  state->reserved[1] = hfsr;
  state->reserved[2] = cfsr;
  state->reserved[3] = stacked_pc;

  /* Publish the complete record at 0x2809f080.  Magic is written last so a
   * debugger never mistakes a partially populated record for a valid one.
   */

  fault->magic         = 0;
  fault->version       = BK7258_AP_FAULT_STATE_VERSION;
  fault->size          = sizeof(*fault);
  fault->generation    = state->generation;
  fault->exception     = exception;
  fault->error         = error;
  fault->exc_return    = exc_return;
  fault->stack_pointer = (uint32_t)(uintptr_t)stack;
  fault->hfsr          = hfsr;
  fault->cfsr          = cfsr;
  fault->mmfar         = mmfar;
  fault->bfar          = bfar;
  fault->stacked_r0    = stacked_r0;
  fault->stacked_r1    = stacked_r1;
  fault->stacked_r2    = stacked_r2;
  fault->stacked_r3    = stacked_r3;
  fault->stacked_r12   = stacked_r12;
  fault->stacked_lr    = stacked_lr;
  fault->stacked_pc    = stacked_pc;
  fault->stacked_xpsr  = stacked_xpsr;
  __asm volatile ("dmb sy" ::: "memory");
  fault->magic = BK7258_AP_FAULT_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");
  up_clean_dcache((uintptr_t)fault,
                  (uintptr_t)fault + sizeof(*fault));
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  bk7258_ap_fault_doorbell(BK7258_AP_EVENT_FAILED, error);

  for (; ; )
    {
      __asm volatile ("wfe");
    }
}

static void __attribute__((naked, noreturn, used))
bk7258_ap_fault_entry(void)
{
  __asm volatile
    (
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "mov r1, lr\n"
      "mrs r2, ipsr\n"
      "b bk7258_ap_fault_handler\n"
    );
}

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE

/* The BK7258 STAR core can return NULL from the common arm_doirq() no-switch
 * path after nxsched_resume_scheduler() clears the selected TCB context.
 * CPU0 already carries this workaround.  N8-C1 needs the same protection on
 * the AP image, but the bookkeeping must be per logical CPU because both AP
 * cores can dispatch mailbox and scheduler interrupts concurrently.
 */

static volatile uint32_t
  g_bk7258_ap_doirq_active[CONFIG_SMP_NCPUS];
static volatile uint32_t
  g_bk7258_ap_doirq_resume_regs[CONFIG_SMP_NCPUS];

static int bk7258_ap_doirq_cpu(void)
{
  int cpu = up_cpu_index();

  if (cpu < 0 || cpu >= CONFIG_SMP_NCPUS)
    {
      return -1;
    }

  return cpu;
}

static void __attribute__((noreturn)) bk7258_ap_doirq_fail(int cpu)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();

  state->error = BK7258_AP_ERROR_BAD_BOOT_STATE;
  state->last_event = BK7258_AP_EVENT_FAILED;
  state->state = BK7258_AP_STATE_FAILED;

  if (cpu == 1 &&
      cpu2->magic == BK7258_CPU2_PROBE_STATE_MAGIC &&
      cpu2->version == BK7258_CPU2_PROBE_STATE_VERSION &&
      cpu2->size == sizeof(*cpu2))
    {
      cpu2->error = BK7258_CPU2_PROBE_ERROR_BAD_BOOT_STATE;
      cpu2->state = BK7258_CPU2_PROBE_STATE_FAILED;
    }

  if (smp->magic == BK7258_AP_SMP_STATE_MAGIC &&
      smp->version == BK7258_AP_SMP_STATE_VERSION &&
      smp->size == sizeof(*smp))
    {
      smp->error = BK7258_AP_SMP_ERROR_BAD_STATE;
      smp->state = BK7258_AP_SMP_STATE_FAILED;
    }

  __asm volatile ("dmb sy; cpsid i; dsb sy; isb sy" ::: "memory");
  for (; ; )
    {
      __asm volatile ("wfe");
    }
}

extern void __real_nxsched_resume_scheduler(struct tcb_s *tcb);

void __wrap_nxsched_resume_scheduler(struct tcb_s *tcb)
{
  int cpu = bk7258_ap_doirq_cpu();

  if (cpu >= 0 && g_bk7258_ap_doirq_active[cpu] != 0)
    {
      uint32_t *regs = tcb != NULL ? tcb->xcp.regs : NULL;

      if (regs != NULL &&
          (regs[REG_EXC_RETURN] & BK7258_EXC_RETURN_THREAD_MODE) != 0)
        {
          regs[REG_CONTROL] |= 1u << 1; /* CONTROL.SPSEL */
#ifdef CONFIG_ARCH_FPU
          if ((regs[REG_EXC_RETURN] & BK7258_EXC_RETURN_BASIC_FRAME) == 0)
            {
              regs[REG_CONTROL] |= 1u << 2; /* CONTROL.FPCA */
            }
#endif
        }

      g_bk7258_ap_doirq_resume_regs[cpu] =
        (uint32_t)(uintptr_t)regs;
      __asm volatile ("dmb sy" ::: "memory");
    }

  __real_nxsched_resume_scheduler(tcb);
}

extern uint32_t *__real_arm_doirq(int irq, uint32_t *regs);

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRACE
extern const volatile uint32_t *bk7258_rptun_native_lock_word(void);
extern void __real___cyg_profile_func_enter(void *function, void *caller);
extern void __real___cyg_profile_func_exit(void *function, void *caller);

static const volatile uint32_t *g_bknative_trace_lock;
static volatile uint32_t g_bknative_trace_meta[4];
static volatile uint32_t g_bknative_trace_busy[2];
static volatile uint32_t g_bknative_trace_calls[2][64][8];

static void bknative_trace_call(void *function, void *caller, uint32_t event,
                                uint32_t reg4, uint32_t reg5)
{
  unsigned int cpu;
  uint32_t sequence;
  uint32_t word;
  uint32_t mask;
  uint32_t context;
  volatile uint32_t *row;

  if (g_bknative_trace_meta[0] != 1 || g_bknative_trace_lock == NULL)
    {
      return;
    }

  cpu = *(volatile uint32_t *)BK7258_LOCAL_CORE_ID_ADDR;
  if (cpu > 1 || g_bknative_trace_busy[cpu] != 0)
    {
      return;
    }

  g_bknative_trace_busy[cpu] = 1;
  sequence = g_bknative_trace_meta[1 + cpu]++;
  word = *g_bknative_trace_lock;
  __asm volatile ("mrs %0, basepri" : "=r" (mask));
  __asm volatile ("mrs %0, ipsr" : "=r" (context));
  mask |= context << 8;
  __asm volatile ("mrs %0, primask" : "=r" (context));
  mask |= context << 20;
  row = g_bknative_trace_calls[cpu][sequence % 64];
  row[0] = sequence;
  row[1] = event;
  row[2] = (uintptr_t)function;
  row[3] = (uintptr_t)caller;
  row[4] = word;
  row[5] = mask;
  row[6] = reg4;
  row[7] = reg5;
  __asm volatile ("dmb sy" ::: "memory");
  if (word == 0xffff0000u)
    {
      g_bknative_trace_meta[3] = cpu + 1;
      g_bknative_trace_meta[0] = 2;
    }

  g_bknative_trace_busy[cpu] = 0;
}

static void __attribute__((noinline, used))
bknative_trace_enter(void *function, void *caller, uint32_t reg4,
                      uint32_t reg5)
{
  bknative_trace_call(function, caller, 1, reg4, reg5);
  __real___cyg_profile_func_enter(function, caller);
}

static void __attribute__((noinline, used))
bknative_trace_exit(void *function, void *caller, uint32_t reg4,
                     uint32_t reg5)
{
  bknative_trace_call(function, caller, 2, reg4, reg5);
  __real___cyg_profile_func_exit(function, caller);
}

void __attribute__((naked))
__wrap___cyg_profile_func_enter(void *function, void *caller)
{
  __asm volatile ("mov r2, r4\n"
                  "mov r3, r5\n"
                  "b bknative_trace_enter\n");
}

void __attribute__((naked))
__wrap___cyg_profile_func_exit(void *function, void *caller)
{
  __asm volatile ("mov r2, r4\n"
                  "mov r3, r5\n"
                  "b bknative_trace_exit\n");
}

void bk7258_native_trace_enable(int enable)
{
  g_bknative_trace_lock = bk7258_rptun_native_lock_word();
  g_bknative_trace_meta[0] = enable && g_bknative_trace_lock != NULL ? 1 : 0;
}

static volatile uint32_t g_bknative_irq_sample[2][8];

int bk7258_native_irq_sample_enable(int enable)
{
  return up_prioritize_irq(NVIC_IRQ_SYSTICK,
                            enable ? 0x40 : NVIC_SYSH_PRIORITY_DEFAULT);
}
#endif

uint32_t *__wrap_arm_doirq(int irq, uint32_t *regs)
{
  uint32_t basepri = NVIC_SYSH_DISABLE_PRIORITY;
  int cpu = bk7258_ap_doirq_cpu();

  if (cpu < 0)
    {
      bk7258_ap_doirq_fail(cpu);
    }

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRACE
  if (irq == NVIC_IRQ_SYSTICK)
    {
      volatile uint32_t *sample = g_bknative_irq_sample[cpu];
      uint32_t primask;

      /* Diagnostic-only high-priority sampling. Never re-enter the kernel
       * from a masked or nested context. Losing a tick in this diagnostic
       * build is intentional; this is not a scheduler repair.
       */

      __asm volatile ("mrs %0, primask" : "=r" (primask));
      sample[0]++;
      sample[1] = regs[REG_BASEPRI];
      sample[2] = primask;
      sample[3] = regs[REG_PC];
      sample[4] = regs[REG_XPSR];
      sample[5] = regs[REG_EXC_RETURN];
      sample[6] = g_bk7258_ap_doirq_active[cpu];
      __asm volatile ("dmb sy" ::: "memory");
      sample[0]++;

      if (regs[REG_BASEPRI] != 0 || (regs[REG_XPSR] & 0x1ffu) != 0 ||
          g_bk7258_ap_doirq_active[cpu] != 0)
        {
          return regs;
        }
    }
#endif

  /* exception_common restores the original BASEPRI from the saved frame. */

  __asm volatile
    (
      "msr basepri, %0\n"
      "dsb sy\n"
      "isb sy\n"
      :
      : "r" (basepri)
      : "memory"
    );

  g_bk7258_ap_doirq_resume_regs[cpu] = 0;
  g_bk7258_ap_doirq_active[cpu] = 1;
  __asm volatile ("dmb sy" ::: "memory");

  regs = __real_arm_doirq(irq, regs);

  g_bk7258_ap_doirq_active[cpu] = 0;
  __asm volatile ("dmb sy" ::: "memory");

  if (regs == NULL)
    {
      regs = (uint32_t *)(uintptr_t)
        g_bk7258_ap_doirq_resume_regs[cpu];
    }

  if (regs == NULL)
    {
      bk7258_ap_doirq_fail(cpu);
    }

  return regs;
}
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

__attribute__((section(".vectors_core0"), used, aligned(4)))
const void *const __vector_core0_table[80] =
{
  [0]       = (void *)BK7258_AP_IDLE_STACK,
  [1]       = (void *)bk7258_ap_reset_entry,
  [2 ... 3] = &bk7258_ap_fault_entry,
  [4 ... 79] = &exception_common
};

/* NuttX ARM common code looks up _vectors.  Keep the official AP-local name
 * as the storage symbol and provide _vectors as an exact alias.
 */

extern const void *const _vectors[80]
  __attribute__((alias("__vector_core0_table")));
