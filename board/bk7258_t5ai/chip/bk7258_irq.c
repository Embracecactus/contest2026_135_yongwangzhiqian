/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_irq.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, Cortex-M33) NVIC interrupt support for NuttX N2.
 *
 * Standard Cortex-M NVIC glue, modelled on nuttx/arch/arm/src/mps/mps_irq.c.
 * Provides up_irqinitialize() / up_enable_irq() / up_disable_irq() /
 * up_prioritize_irq() / arm_ack_irq() that the common armv8-m dispatch
 * code (arm_doirq.c, arm_exception.S) expects.  The BK7258 exposes 48
 * external IRQs (NR_IRQS=48 -> vector slots [16..63]); the chip-provided
 * vector table (bk7258_vectors.c) routes SysTick + all external IRQs to
 * exception_direct and the system exceptions to exception_common, so once
 * up_irqinitialize() runs, irq_attach()/irq_dispatch() work normally.
 *
 * System-exception IRQ numbers use the canonical arm_m names from nvic.h
 * (NVIC_IRQ_HARDFAULT, NVIC_IRQ_SVCALL, NVIC_IRQ_PENDSV, ...) which equal
 * the BK7258_IRQ_* values in chip/include/irq.h.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <arch/irq.h>

#include "ram_vectors.h"
#include "arm_internal.h"
#include "nvic.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NVIC enable/clear register bank stride. */

#define NVIC_ENA_OFFSET         (0)
#define NVIC_CLRENA_OFFSET      (NVIC_IRQ0_31_CLEAR - NVIC_IRQ0_31_ENABLE)

/* Default priority packed four-per-register (one stroke for all 4 slots). */

#define DEFPRIORITY32           (NVIC_SYSH_PRIORITY_DEFAULT << 24 | \
                                 NVIC_SYSH_PRIORITY_DEFAULT << 16 | \
                                 NVIC_SYSH_PRIORITY_DEFAULT << 8  | \
                                 NVIC_SYSH_PRIORITY_DEFAULT)

/* UART1 MMIO for the boot-trace marker pushed at the top of up_irqinitialize().
 * Freestanding polled putc (polls fifo_status.bit20, writes fifo_port);
 * identical to start.c::bk7258_early_putc and vectors.c::bk7258_fault_putc.
 * Local to this translation unit so it introduces no new linkage dependency.
 */

#define BK7258_IRQ_UART1_FSTAT  (*(volatile uint32_t *)0x45830018u)
#define BK7258_IRQ_UART1_FPORT  (*(volatile uint32_t *)0x4583001Cu)
#define BK7258_IRQ_UART1_READY  (1u << 20)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Bare MMIO single-byte marker.  Emits 'I' at function entry of
 * up_irqinitialize() so board-side observation can confirm the NVIC bring-up
 * was reached during the nx_start() walk.
 */

static void bk7258_irq_diag_putc(unsigned char c)
{
  while ((BK7258_IRQ_UART1_FSTAT & BK7258_IRQ_UART1_READY) == 0)
    {
    }

  BK7258_IRQ_UART1_FPORT = (uint32_t)(c & 0xffu);
}

/****************************************************************************
 * Name: bk7258_prioritize_syscall
 *
 * Description:
 *   Set the priority of the SVCall exception (system handler 11).  Needed
 *   internally even when CONFIG_ARCH_IRQPRIO is off.
 *
 ****************************************************************************/

static inline void bk7258_prioritize_syscall(int priority)
{
  uint32_t regval;

  regval  = getreg32(NVIC_SYSH8_11_PRIORITY);
  regval &= ~NVIC_SYSH_PRIORITY_PR11_MASK;
  regval |= (priority << NVIC_SYSH_PRIORITY_PR11_SHIFT);
  putreg32(regval, NVIC_SYSH8_11_PRIORITY);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_prioritize_irq
 *
 * Description:
 *   Set the priority of an IRQ.
 *
 ****************************************************************************/

int up_prioritize_irq(int irq, int priority)
{
  uint32_t regaddr;
  uint32_t regval;
  int shift;

  DEBUGASSERT(irq >= 0 && irq < NR_IRQS &&
              (unsigned)priority <= NVIC_SYSH_PRIORITY_MIN);

  if (irq < 16)
    {
      /* NVIC_SYSH_PRIORITY() maps {0..15} to one of the priority registers
       * (0-3 are invalid as raw indices; they are remapped internally).
       */

      regaddr = NVIC_SYSH_PRIORITY(irq);
      irq    -= 4;
    }
  else
    {
      /* NVIC_IRQ_PRIORITY() maps {0..} to the external priority registers. */

      irq    -= 16;
      regaddr = NVIC_IRQ_PRIORITY(irq);
    }

  regval  = getreg32(regaddr);
  shift   = ((irq & 3) << 3);
  regval &= ~(0xff << shift);
  regval |= (priority << shift);
  putreg32(regval, regaddr);

  return OK;
}

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   Bring the NVIC up to a known, ready state.  Called from up_initialize().
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  uint32_t regaddr;
  int num_priority_registers;
  int i;

  /* Boot-trace marker: reached up_irqinitialize() inside nx_start(). */

  bk7258_irq_diag_putc('I');

  /* Disable all external interrupts. */

  for (i = 0; i < NR_IRQS - BK7258_IRQ_FIRST; i += 32)
    {
      putreg32(0xffffffff, NVIC_IRQ_CLEAR(i));
    }

  /* Point VTOR at our flash-resident vector table (also set in __start;
   * redundant here but deterministic).
   */

  putreg32((uint32_t)_vectors, NVIC_VECTAB);

#ifdef CONFIG_ARCH_RAMVECTORS
  arm_ramvec_initialize();
#endif

  /* Default-prioritise the system exceptions. */

  putreg32(DEFPRIORITY32, NVIC_SYSH4_7_PRIORITY);
  putreg32(DEFPRIORITY32, NVIC_SYSH8_11_PRIORITY);
  putreg32(DEFPRIORITY32, NVIC_SYSH12_15_PRIORITY);

  /* The NVIC ICTR register (bits 0-4) holds (number of IRQ lines / 32) - 1,
   * which also tells us how many priority registers to default-fill.
   */

  num_priority_registers = (getreg32(NVIC_ICTR) + 1) * 8;

  regaddr = NVIC_IRQ0_3_PRIORITY;
  while (num_priority_registers--)
    {
      putreg32(DEFPRIORITY32, regaddr);
      regaddr += 4;
    }

  /* Attach the SVCall and HardFault handlers used by the scheduler. */

  irq_attach(NVIC_IRQ_SVCALL, arm_svcall, NULL);
  irq_attach(NVIC_IRQ_HARDFAULT, arm_hardfault, NULL);

  /* PendSV runs at the lowest priority so it never preempts device IRQs. */

  up_prioritize_irq(NVIC_IRQ_PENDSV, NVIC_SYSH_PRIORITY_MIN);
  bk7258_prioritize_syscall(NVIC_SYSH_SVCALL_PRIORITY);

  /* Enable the MemManage fault if the MPU is in use. */

#ifdef CONFIG_ARM_MPU
  irq_attach(NVIC_IRQ_MEMFAULT, arm_memfault, NULL);
  up_enable_irq(NVIC_IRQ_MEMFAULT);
#endif

#ifndef CONFIG_SUPPRESS_INTERRUPTS

  /* Colour the interrupt stack (writes a sentinel pattern) and unmask IRQs. */

  arm_color_intstack();
  up_irq_enable();
#endif
}

/****************************************************************************
 * Name: bk7258_irqinfo
 *
 * Description:
 *   Map a NuttX IRQ number to the (register, bit) pair that enables/disables
 *   it in the NVIC, honouring the enable/clear bank offset for external IRQs
 *   and the SYSHCON/SYSTICK control bits for the handful of manageable
 *   system exceptions.
 *
 ****************************************************************************/

static int bk7258_irqinfo(int irq, uintptr_t *regaddr, uint32_t *bit,
                          uintptr_t offset)
{
  int n;

  DEBUGASSERT(irq >= NVIC_IRQ_NMI && irq < NR_IRQS);

  /* External device interrupt? */

  if (irq >= BK7258_IRQ_FIRST)
    {
      n        = irq - BK7258_IRQ_FIRST;
      *regaddr = NVIC_IRQ_ENABLE(n) + offset;
      *bit     = (uint32_t)0x1 << (n & 0x1f);
    }

  /* Processor exception -- only a few can be enabled/disabled. */

  else
    {
      *regaddr = NVIC_SYSHCON;
      if (irq == NVIC_IRQ_MEMFAULT)
        {
          *bit = NVIC_SYSHCON_MEMFAULTENA;
        }
      else if (irq == NVIC_IRQ_BUSFAULT)
        {
          *bit = NVIC_SYSHCON_BUSFAULTENA;
        }
      else if (irq == NVIC_IRQ_USAGEFAULT)
        {
          *bit = NVIC_SYSHCON_USGFAULTENA;
        }
      else if (irq == NVIC_IRQ_SYSTICK)
        {
          *regaddr = NVIC_SYSTICK_CTRL;
          *bit = NVIC_SYSTICK_CTRL_ENABLE;
        }
      else
        {
          return -EINVAL;  /* Invalid or unsupported exception */
        }
    }

  return OK;
}

/****************************************************************************
 * Name: up_disable_irq
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  uintptr_t regaddr;
  uint32_t  regval;
  uint32_t  bit;

  if (bk7258_irqinfo(irq, &regaddr, &bit, NVIC_CLRENA_OFFSET) == 0)
    {
      if (irq >= BK7258_IRQ_FIRST)
        {
          /* External: write 1 to the Clear-Enable bank bit. */

          putreg32(bit, regaddr);
        }
      else
        {
          /* System exception: clear the SYSHCON / SYSTICK bit. */

          regval  = getreg32(regaddr);
          regval &= ~bit;
          putreg32(regval, regaddr);
        }
    }
}

/****************************************************************************
 * Name: up_enable_irq
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  uintptr_t regaddr;
  uint32_t  regval;
  uint32_t  bit;

  if (bk7258_irqinfo(irq, &regaddr, &bit, NVIC_ENA_OFFSET) == 0)
    {
      if (irq >= BK7258_IRQ_FIRST)
        {
          /* External: write 1 to the Set-Enable bank bit. */

          putreg32(bit, regaddr);
        }
      else
        {
          /* System exception: set the SYSHCON / SYSTICK bit. */

          regval  = getreg32(regaddr);
          regval |= bit;
          putreg32(regval, regaddr);
        }
    }
}

/****************************************************************************
 * Name: arm_ack_irq
 *
 * Description:
 *   Acknowledge an IRQ.  Cortex-M NVIC IRQs are auto-acknowledged on
 *   exception entry, so there is nothing to do here (the stub is required
 *   by arm_doirq.c).
 *
 ****************************************************************************/

void arm_ack_irq(int irq)
{
}
