/****************************************************************************
 * arch/arm/src/bk7258/chip.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Chip-internal include for the Beken BK7258 (Cortex-M33) Stage N1 port.
 *
 * Reached two ways:
 *   - by nuttx/arch/arm/src/common/arm_internal.h via #include "chip.h"
 *     (resolved through -I $(CHIP_DIR), i.e. this directory),
 *   - directly by bk7258_vectors.c / bk7258_start.c via #include "chip.h".
 *
 * For a CUSTOM NuttX chip the public chip headers live under
 * chip/include/ and are exposed to the rest of NuttX through the
 * nuttx/include/arch/chip symlink.  We therefore pull <arch/chip/irq.h>
 * (NOT <arch/bk7258/irq.h>) to get NR_IRQS.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_H
#define __ARCH_ARM_SRC_BK7258_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/chip/irq.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Required by nuttx/arch/arm/src/arm_m/{ram_vectors.h, arm_vectors.c} and
 * friends so the generic exception-handling code knows how many external
 * IRQs exist.  Even though N1 provides its own vector table, the common
 * ARMv8-M source set (arm_doirq.c, arm_initialstate.c, ...) still
 * references this macro.
 */

#define ARMV8M_PERIPHERAL_INTERRUPTS    NR_IRQS

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_H */
