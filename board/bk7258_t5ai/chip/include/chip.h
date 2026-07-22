/****************************************************************************
 * arch/arm/include/bk7258/chip.h  (exposed via nuttx/include/arch/chip)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Public chip-level include for the Beken BK7258 (Cortex-M33) Stage A1
 * port.  Reached by the rest of NuttX through <arch/chip/chip.h> (the
 * nuttx/include/arch/chip symlink points at this directory).
 *
 * We only need to expose ARMV8M_PERIPHERAL_INTERRUPTS (the external IRQ
 * count, not the total logical count) so the common ARMv8-M code can size
 * its per-IRQ arrays and compute ARM_VECTAB_SIZE.
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_CHIP_H
#define __ARCH_ARM_INCLUDE_BK7258_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/chip/irq.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef ARMV8M_PERIPHERAL_INTERRUPTS
#  define ARMV8M_PERIPHERAL_INTERRUPTS    BK7258_EXTERNAL_IRQS
#elif ARMV8M_PERIPHERAL_INTERRUPTS != BK7258_EXTERNAL_IRQS
#  error "ARMV8M_PERIPHERAL_INTERRUPTS must equal BK7258_EXTERNAL_IRQS (64)"
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_CHIP_H */
