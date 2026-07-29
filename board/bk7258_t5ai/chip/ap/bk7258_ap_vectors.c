/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_ap_vectors.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Physical CPU1 executes this AP-local logical-core-0 vector table.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

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
#define BK7258_EXCEPTION_FP_FRAME_WORDS   18u
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

  /* Hardware places the basic frame after the optional FP frame. */

  if ((exc_return & BK7258_EXC_RETURN_BASIC_FRAME) == 0)
    {
      frame_addr += BK7258_EXCEPTION_FP_FRAME_WORDS * sizeof(uint32_t);
    }

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

uint32_t *__wrap_arm_doirq(int irq, uint32_t *regs)
{
  uint32_t basepri = NVIC_SYSH_DISABLE_PRIORITY;
  int cpu = bk7258_ap_doirq_cpu();

  if (cpu < 0)
    {
      bk7258_ap_doirq_fail(cpu);
    }

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
