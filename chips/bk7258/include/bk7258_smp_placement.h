/****************************************************************************
 * chips/bk7258/include/bk7258_smp_placement.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Placement contract for objects that take part in AP cross-core
 * synchronization.  LDREX/STREX-based NuttX spinlocks, rspinlocks, atomic
 * semaphore counts and mutex holders must never live in the PSRAM aperture.
 * Static cross-core locks are also placed explicitly in the official AP SMP
 * lock window instead of depending only on a linker-script name glob.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SMP_PLACEMENT_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SMP_PLACEMENT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Official BK7258 AP SMP lock window (boards/bk7258/common/scripts/
 * ld_ap.script region SPINLOCK) and the PSRAM aperture mapped by MPU
 * region 6.  Both are fixed SoC/ABI facts; do not derive them per board.
 */

#define BK7258_SMP_LOCK_WINDOW_BASE   0x28000000u
#define BK7258_SMP_LOCK_WINDOW_END    0x28010000u
#define BK7258_SMP_PSRAM_APERTURE_BASE 0x60000000u
#define BK7258_SMP_PSRAM_APERTURE_END  0x64000000u

/* Only the AP SMP image has a dedicated lock window.  The zero-initialized
 * variant uses a ".bss." prefixed input section so every other linker script
 * (CP, AP-UP, host tests) still collects it through its ordinary
 * "*(.bss .bss.*)" rule instead of creating an orphan section.  The AP SMP
 * script matches this exact name in .spinlock_bss before the blanket .bss
 * rule.  The initialized variant is restricted to the AP SMP image because
 * only ld_ap.script copies ".sram_spinlock_section" from Flash.
 */

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
#  define BK7258_SMP_LOCK_BSS \
     __attribute__((section(".bss.bk7258_smp_lock")))
#  define BK7258_SMP_LOCK_DATA \
     __attribute__((section(".sram_spinlock_section")))
#  define BK7258_SMP_PLACEMENT_ENFORCED 1
#else
#  define BK7258_SMP_LOCK_BSS
#  define BK7258_SMP_LOCK_DATA
#  define BK7258_SMP_PLACEMENT_ENFORCED 0
#endif

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline bool bk7258_smp_addr_is_psram(const volatile void *object)
{
  uintptr_t address = (uintptr_t)object;

  return address >= BK7258_SMP_PSRAM_APERTURE_BASE &&
         address < BK7258_SMP_PSRAM_APERTURE_END;
}

static inline bool bk7258_smp_addr_in_lock_window(const volatile void *object)
{
  uintptr_t address = (uintptr_t)object;

  return address >= BK7258_SMP_LOCK_WINDOW_BASE &&
         address < BK7258_SMP_LOCK_WINDOW_END;
}

/* A cross-core synchronization object is acceptable only outside PSRAM.
 * Non-SMP roles keep the same check so the CP/AP-UP builds share one rule.
 */

static inline bool bk7258_smp_object_placement_valid(
  const volatile void *object)
{
  return object != NULL && !bk7258_smp_addr_is_psram(object);
}

/* Diagnostic assertion for kernel/SMP synchronization objects.  It is a
 * DEBUGASSERT, so product images without CONFIG_DEBUG_ASSERTIONS keep their
 * existing behavior while debug/diagnostic images fail at the allocation
 * site that produced the misplaced object.
 */

#define BK7258_SMP_ASSERT_PLACEMENT(object) \
  DEBUGASSERT(bk7258_smp_object_placement_valid(object))

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SMP_PLACEMENT_H */
