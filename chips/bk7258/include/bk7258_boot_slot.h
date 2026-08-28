/****************************************************************************
 * chips/bk7258/include/bk7258_boot_slot.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BOOT_SLOT_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BOOT_SLOT_H

#include <nuttx/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_BOOT_SLOT_MAP_VERSION 1u

enum bk7258_boot_slot_e
{
  BK7258_BOOT_SLOT_INVALID = -1,
  BK7258_BOOT_SLOT_PRIMARY = 0,
  BK7258_BOOT_SLOT_SECONDARY = 1
};

/* The expected remap values are generated from the selected product layout.
 * The chip decoder owns the fixed BK7258 register addresses and consumes only
 * this board-provided layout snapshot.
 */

struct bk7258_boot_slot_map_s
{
  uint16_t version;
  uint16_t size;
  uint32_t secondary_begin;
  uint32_t secondary_end;
  uint32_t secondary_offset;
};

/* Return the physical CP/AP pair selected by BL2.  A secondary result is
 * accepted only when every retained remap register matches the generated
 * layout contract; an unexpected enabled remap fails closed. */

int bk7258_boot_active_slot(FAR const struct bk7258_boot_slot_map_s *map,
                            FAR enum bk7258_boot_slot_e *slot);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BOOT_SLOT_H */
