/****************************************************************************
 * arch/arm/include/bk7258/irq.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stage A1 IRQ definitions for the Beken BK7258 (Cortex-M33).
 *
 * The vector table in bk7258_vectors.c has 80 entries:
 *
 *   [0]      initial MSP                       (offset 0x000)
 *   [1]      __start                           (offset 0x004)
 *   [2..15]  14 system exception slots         (offset 0x008 .. 0x03c)
 *   [16..63] 48 lower external IRQ slots       (offset 0x040 .. 0x0fc)
 *   [64..65] BK7236 app magic "BK7236\0\0"     (offset 0x100 .. 0x104)
 *   [66..79] 16 upper external IRQ slots       (offset 0x108 .. 0x13c)
 *
 * NR_IRQS = 80 makes ARMV8M_PERIPHERAL_INTERRUPTS = 64, so the standard
 * NuttX vector layout expects exactly slots [16..79] for IRQs.  The two
 * app-magic words at [64]/[65] occupy logical IRQ numbers 48 and 49 and
 * are runtime-repaired via arm_ramvec_attach after VTOR switches to RAM.
 * This is exactly the layout the T5-AI Tier-1 bootloader
 * validates (see docs/bk7258-t5ai/probe/probe.c).
 *
 * A1 enables interrupts; the common ARMv8-M code needs the NVIC priority
 * macros below at compile time, so they are defined here.
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

/* 64 external interrupts -> vector slots [16..79]; magic at [64]/[65].
 * Slots [64]/[65] correspond to external IRQ indices 48/49 but are logical
 * NuttX IRQs 64/65 (ETHERNET/SCALE0).  They hold boot magic in flash and
 * are runtime-repaired to exception_common after VTOR switches to RAM.
 */

#define BK7258_IRQ_FIRST                16
#define BK7258_EXTERNAL_IRQS            64
#define NR_IRQS                         (BK7258_IRQ_FIRST + BK7258_EXTERNAL_IRQS)

/* Cortex-M system exception vector numbers (offset by 16 from IRQ number).
 * Provided here so chip.h / board code can name them; mirror the values in
 * nuttx/arch/arm/src/arm_m/nvic.h.
 */

#define BK7258_IRQ_NMI                  (2)
#define BK7258_IRQ_HARDFAULT            (3)
#define BK7258_IRQ_SVCALL               (11)
#define BK7258_IRQ_PENDSV               (14)
#define BK7258_IRQ_SYSTICK              (15)

/* UART1 peripheral sits at chip IRQ 15, which maps to NuttX vector slot
 * [16 + 15] = [31].  The slot is already wired to exception_common in
 * bk7258_vectors.c, so the vector table itself needs no change.
 */

#define BK7258_IRQ_UART1                (BK7258_IRQ_FIRST + 15) /* logical 31 */

/* Anchor IRQ names for the upper external range.  These logical IRQ
 * numbers occupy vector slots [64]/[65] which are also the boot-magic
 * slots; runtime repair via arm_ramvec_attach restores exception_common
 * after VTOR switches to RAM.
 */

#define BK7258_IRQ_ETHERNET             (BK7258_IRQ_FIRST + 48) /* logical 64 */
#define BK7258_IRQ_SCALE0               (BK7258_IRQ_FIRST + 49) /* logical 65 */
#define BK7258_IRQ_MAILBOX              (BK7258_IRQ_FIRST + 63) /* logical 79 */

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

/****************************************************************************
 * CP and the board-verified AP-UP fallback remain independent UP kernels.
 * CONFIG_SMP is permitted only for the explicit N8-B AP bootstrap config.
 ****************************************************************************/

#if defined(CONFIG_SMP) && \
    (!defined(CONFIG_BK7258_AP_CORE) || \
     !defined(CONFIG_BK7258_AP_SMP_BOOTSTRAP))
#  error "BK7258 SMP is permitted only for the N8-B AP bootstrap config"
#endif

/****************************************************************************
 * Boot-magic structural constants (image byte offsets, not vector indices).
 *
 * The Tier-1 bootloader validates a 64-bit app magic "BK7236\0\0" at image
 * byte offsets 0x100 and 0x104.  These offsets are determined by the
 * original 48-external-IRQ layout: (16 system + 48 external) * 4 = 0x100.
 * They do NOT change when external IRQs expand from 48 to 64 in A1,
 * because the magic always occupies vector slots [64] and [65].
 ****************************************************************************/

#define BK7258_MAGIC_BOOT0_OFFSET       0x100
#define BK7258_MAGIC_BOOT1_OFFSET       0x104
#define BK7258_MAGIC_BOOT_SIZE          8

#endif /* __ARCH_ARM_INCLUDE_BK7258_IRQ_H */
