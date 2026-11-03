/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/arch/chip/bk7258_smp_placement.h
 *
 * Host shim for the AP SMP lock-placement contract.  Host fixtures have no
 * BK7258 lock window or PSRAM aperture, so the section tags expand to
 * nothing and every host object is treated as validly placed.
 ****************************************************************************/

#ifndef __MOCK_ARCH_CHIP_BK7258_SMP_PLACEMENT_H
#define __MOCK_ARCH_CHIP_BK7258_SMP_PLACEMENT_H

#include <stdbool.h>

#define BK7258_SMP_LOCK_BSS
#define BK7258_SMP_LOCK_DATA
#define BK7258_SMP_PLACEMENT_ENFORCED 0

static inline bool bk7258_smp_object_placement_valid(
  const volatile void *object)
{
  return object != 0;
}

#define BK7258_SMP_ASSERT_PLACEMENT(object) ((void)(object))

#endif /* __MOCK_ARCH_CHIP_BK7258_SMP_PLACEMENT_H */
