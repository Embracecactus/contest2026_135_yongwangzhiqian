/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/ap/
 * bk7258_cpu2_probe.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N8-A physical CPU2 startup probe.  Physical CPU1 launches this freestanding
 * AP-local logical-core-1 vector without enabling NuttX SMP on CPU2 yet.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>

#include <arch/chip/bk7258_amp.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SCB_VTOR             (*(volatile uint32_t *)0xe000ed08u)
#define BK7258_SCB_CFSR             (*(volatile uint32_t *)0xe000ed28u)
#define BK7258_SCB_HFSR             (*(volatile uint32_t *)0xe000ed2cu)

#define BK7258_CPU2_LOCAL_CORE_ID   1u
#define BK7258_CPU2_PHYSICAL_ID     2u
#define BK7258_CPU2_VECTOR_COUNT    80u
#define BK7258_CPU2_FRAME_WORDS     8u
#define BK7258_CPU2_INVALID_VALUE   UINT32_MAX

/****************************************************************************
 * External Data
 ****************************************************************************/

extern const void *const
  __vector_core1_table[BK7258_CPU2_VECTOR_COUNT];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

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
          return 0;
        }

      if (current == BK7258_CPU2_PROBE_STATE_FAILED)
        {
          return -EIO;
        }

      up_mdelay(1);
    }

  return -ETIMEDOUT;
}

static void bk7258_cpu2_probe_fail(uint32_t error)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();

  state->error = error;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_CPU2_PROBE_STATE_FAILED;
  __asm volatile ("dmb sy" ::: "memory");
}

static void __attribute__((noinline, noreturn, used))
bk7258_cpu2_probe_main(void)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  uint32_t control;
  uint32_t msp;

  __asm volatile ("mrs %0, msp" : "=r" (msp));
  __asm volatile ("mrs %0, control" : "=r" (control));

  state->local_core_id  = BK7258_CPU2_LOCAL_CORE_ID;
  state->physical_core_id = BK7258_CPU2_PHYSICAL_ID;
  state->runtime_vtor   = BK7258_SCB_VTOR;
  state->runtime_msp    = msp;
  state->control        = control;
  __asm volatile ("dmb sy" ::: "memory");

  if (state->magic != BK7258_CPU2_PROBE_STATE_MAGIC ||
      state->version != BK7258_CPU2_PROBE_STATE_VERSION ||
      state->size != sizeof(struct bk7258_cpu2_probe_state_s))
    {
      bk7258_cpu2_probe_fail(BK7258_CPU2_PROBE_ERROR_BAD_BOOT_STATE);
      goto parked;
    }

  if (*(volatile uint32_t *)BK7258_LOCAL_CORE_ID_ADDR !=
        BK7258_CPU2_LOCAL_CORE_ID ||
      state->physical_core_id != BK7258_CPU2_PHYSICAL_ID)
    {
      bk7258_cpu2_probe_fail(BK7258_CPU2_PROBE_ERROR_BAD_CORE_ID);
      goto parked;
    }

  if (state->runtime_vtor !=
      (uint32_t)(uintptr_t)__vector_core1_table)
    {
      bk7258_cpu2_probe_fail(BK7258_CPU2_PROBE_ERROR_BAD_VTOR);
      goto parked;
    }

  if (msp <= BK7258_CPU2_PROBE_STACK_BASE ||
      msp > BK7258_CPU2_PROBE_STACK_TOP)
    {
      bk7258_cpu2_probe_fail(BK7258_CPU2_PROBE_ERROR_BAD_MSP);
      goto parked;
    }

  state->command = BK7258_CPU2_PROBE_COMMAND_NONE;
  state->error = BK7258_CPU2_PROBE_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_CPU2_PROBE_STATE_READY;
  __asm volatile ("dmb sy" ::: "memory");

  for (; ; )
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (state->command == BK7258_CPU2_PROBE_COMMAND_STOP)
        {
          state->state = BK7258_CPU2_PROBE_STATE_STOPPING;
          __asm volatile ("dmb sy" ::: "memory");
          state->command = BK7258_CPU2_PROBE_COMMAND_NONE;
          __asm volatile ("dmb sy" ::: "memory");
          state->state = BK7258_CPU2_PROBE_STATE_STOPPED;
          __asm volatile ("dmb sy" ::: "memory");
          goto parked;
        }

      state->heartbeat++;
      __asm volatile ("dmb sy; wfe" ::: "memory");
    }

parked:
  __asm volatile ("cpsid i; dsb sy; isb sy" ::: "memory");
  for (; ; )
    {
      __asm volatile ("wfe");
    }
}

static void __attribute__((naked, noreturn, used))
bk7258_cpu2_probe_reset(void)
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
      "b bk7258_cpu2_probe_main\n"
    );
}

static void __attribute__((noinline, noreturn, used))
bk7258_cpu2_probe_fault_handler(uint32_t *stack, uint32_t exception)
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
      frame >= BK7258_CPU2_PROBE_STACK_BASE &&
      frame <= BK7258_CPU2_PROBE_STACK_TOP -
               BK7258_CPU2_FRAME_WORDS * sizeof(uint32_t))
    {
      const volatile uint32_t *regs =
        (const volatile uint32_t *)frame;

      state->fault_lr = regs[5];
      state->fault_pc = regs[6];
      state->fault_xpsr = regs[7];
    }

  if (exception == 2u)
    {
      error = BK7258_CPU2_PROBE_ERROR_NMI;
    }
  else
    {
      error = BK7258_CPU2_PROBE_ERROR_HARDFAULT;
    }

  bk7258_cpu2_probe_fail(error);

  for (; ; )
    {
      __asm volatile ("wfe");
    }
}

static void __attribute__((naked, noreturn, used))
bk7258_cpu2_probe_fault_entry(void)
{
  __asm volatile
    (
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "mrs r1, ipsr\n"
      "b bk7258_cpu2_probe_fault_handler\n"
    );
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_cpu2_probe_start(uint32_t timeout_ms)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  volatile uint32_t *control = bk7258_cpu2_control();
  uint32_t generation = bk7258_ap_boot_state()->generation;
  uint32_t value;
  int ret;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_CPU2_PROBE_TIMEOUT_MS;
    }

  bk7258_cpu2_hold_reset(state);

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
  state->initial_msp = BK7258_CPU2_PROBE_STACK_TOP;

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

  value |= BK7258_SYS_CPU2_RESET;
  *control = value;
  state->control_after = value;
  __asm volatile ("dsb sy; sev" ::: "memory");

  ret = bk7258_cpu2_wait(BK7258_CPU2_PROBE_STATE_READY, timeout_ms);
  if (ret < 0)
    {
      bk7258_cpu2_hold_reset(state);
      if (state->state != BK7258_CPU2_PROBE_STATE_FAILED)
        {
          state->error = BK7258_CPU2_PROBE_ERROR_TIMEOUT;
          state->state = BK7258_CPU2_PROBE_STATE_FAILED;
          __asm volatile ("dmb sy" ::: "memory");
        }
    }

  return ret;
}

int bk7258_cpu2_probe_stop(uint32_t timeout_ms)
{
  volatile struct bk7258_cpu2_probe_state_s *state =
    bk7258_cpu2_probe_state();
  int ret = 0;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_CPU2_PROBE_STOP_TIMEOUT_MS;
    }

  if (state->magic != BK7258_CPU2_PROBE_STATE_MAGIC ||
      state->version != BK7258_CPU2_PROBE_STATE_VERSION)
    {
      ret = -EIO;
    }
  else if (state->state == BK7258_CPU2_PROBE_STATE_READY)
    {
      state->command = BK7258_CPU2_PROBE_COMMAND_STOP;
      __asm volatile ("dmb sy; sev" ::: "memory");
      ret = bk7258_cpu2_wait(BK7258_CPU2_PROBE_STATE_STOPPED,
                             timeout_ms);
    }
  else if (state->state == BK7258_CPU2_PROBE_STATE_FAILED)
    {
      ret = -EIO;
    }

  bk7258_cpu2_hold_reset(state);

  if (ret < 0 && state->state != BK7258_CPU2_PROBE_STATE_FAILED)
    {
      state->error = ret == -ETIMEDOUT ?
        BK7258_CPU2_PROBE_ERROR_TIMEOUT :
        BK7258_CPU2_PROBE_ERROR_BAD_BOOT_STATE;
      __asm volatile ("dmb sy" ::: "memory");
      state->state = BK7258_CPU2_PROBE_STATE_FAILED;
      __asm volatile ("dmb sy" ::: "memory");
    }

  return ret;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

__attribute__((section(".vectors_core1"), used, aligned(512)))
const void *const __vector_core1_table[BK7258_CPU2_VECTOR_COUNT] =
{
  [0] = (void *)BK7258_CPU2_PROBE_STACK_TOP,
  [1] = (void *)bk7258_cpu2_probe_reset,
  [2 ... 79] = &bk7258_cpu2_probe_fault_entry
};
