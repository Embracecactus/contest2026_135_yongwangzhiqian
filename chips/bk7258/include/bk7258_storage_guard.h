/****************************************************************************
 * chips/bk7258/include/bk7258_storage_guard.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 persistent-storage transaction guard.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STORAGE_GUARD_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STORAGE_GUARD_H

#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

enum bk7258_storage_guard_e
{
  BK7258_STORAGE_GUARD_DATA = 0,
  BK7258_STORAGE_GUARD_RADIO,
  BK7258_STORAGE_GUARD_OTA_STAGE_PRIMARY,
  BK7258_STORAGE_GUARD_OTA_STAGE_SECONDARY,
  BK7258_STORAGE_GUARD_OTA_CONFIRM_PRIMARY,
  BK7258_STORAGE_GUARD_OTA_CONFIRM_SECONDARY,
  BK7258_STORAGE_GUARD_RESET_MARKER,
  BK7258_STORAGE_GUARD_COUNT
};

#ifdef __cplusplus
extern "C"
{
#endif

/* Serialize a complete persistent-storage transaction.  Write permission is
 * scoped to the current task, region and board-supplied immutable topology.
 * Same-task nesting is permitted only for the same region and access mode.
 */

int bk7258_storage_guard_lock(enum bk7258_storage_guard_e guard,
                              bool write_access, uint32_t timeout_ms);
void bk7258_storage_guard_unlock(void);
bool bk7258_storage_guard_write_authorized(uint32_t address,
                                            uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STORAGE_GUARD_H */
