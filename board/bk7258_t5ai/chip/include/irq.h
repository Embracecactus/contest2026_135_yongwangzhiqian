/****************************************************************************
 * arch/arm/include/bk7258/irq.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stage N1 minimal IRQ definitions for the Beken BK7258 (Cortex-M33).
 *
 * The vector table in bk7258_vectors.c has 66 entries:
 *
 *   [0]      initial MSP                       (offset 0x000)
 *   [1]      __start                           (offset 0x004)
 *   [2..15]  14 system exception slots         (offset 0x008 .. 0x03c)
 *   [16..63] 48 external IRQ slots             (offset 0x040 .. 0x0fc)
 *   [64..65] BK7236 app magic "BK7236\0\0"     (offset 0x100 .. 0x104)
 *
 * NR_IRQS = 48 makes ARMV8M_PERIPHERAL_INTERRUPTS = 48, so the standard
 * NuttX vector layout expects exactly slots [16..63] for IRQs, leaving the
 * magic words at [64]/[65] as appended image bytes that NuttX never
 * indexes.  This is exactly the layout the T5-AI Tier-1 bootloader
 * validates (see docs/bk7258-t5ai/probe/probe.c).
 *
 * N1 never enables interrupts, but the common ARMv8-M code still needs the
 * NVIC priority macros below at compile time, so they are defined here.
 * This header is reached via <arch/chip/irq.h> which <arch/irq.h> includes
 * BEFORE <arch/arm_m/irq.h> (and hence before nuttx/include/arch/arm_m/
 * nvicpri.h), so the priority values are visible when nvicpri.h derives
 * NVIC_SYSH_{MAXNORMAL,HIGH,DISABLE,SVCALL}_PRIORITY from them.
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_IRQ_H
#define __ARCH_ARM_INCLUDE_BK7258_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 48 external interrupts -> vector slots [16..63]; magic at [64]/[65]. */

#define NR_IRQS                         48

/* Cortex-M system exception vector numbers (offset by 16 from IRQ number).
 * Provided here so chip.h / board code can name them; mirror the values in
 * nuttx/arch/arm/src/arm_m/nvic.h.
 */

#define BK7258_IRQ_NMI                  (2)
#define BK7258_IRQ_HARDFAULT            (3)
#define BK7258_IRQ_SVCALL               (11)
#define BK7258_IRQ_PENDSV               (14)
#define BK7258_IRQ_SYSTICK              (15)
#define BK7258_IRQ_FIRST                (16)

/* UART1 peripheral sits at chip IRQ 15, which maps to NuttX vector slot
 * [16 + 15] = [31].  The slot is already wired to exception_direct in
 * bk7258_vectors.c, so the vector table itself needs no change.
 */

#define BK7258_IRQ_UART1                (16 + 15)   /* = 31 */

/* NVIC priority encoding for the Cortex-M33 core.  The core implements
 * priority bits [7:5] (3 bits -> 8 priority levels, top 5 bits
 * implemented).  Values are identical to every other stock Cortex-M3/4/33
 * in NuttX (see nuttx/include/arch/mps/chip.h); the BK7258 does not add
 * anything special here.
 *
 *   MAX     = 0x00  (highest, exception entry uses this)
 *   DEFAULT = 0x80  (midpoint, used by up_irq_save/disable via BASEPRI)
 *   MIN     = 0xf0  (lowest)
 *   STEP    = 0x10  (16 between adjacent levels)
 */

#define NVIC_SYSH_PRIORITY_MIN          0xf0
#define NVIC_SYSH_PRIORITY_DEFAULT      0x80
#define NVIC_SYSH_PRIORITY_MAX          0x00
#define NVIC_SYSH_PRIORITY_STEP         0x10

#endif /* __ARCH_ARM_INCLUDE_BK7258_IRQ_H */
