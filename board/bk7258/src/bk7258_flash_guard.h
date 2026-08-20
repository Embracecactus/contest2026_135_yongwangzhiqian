/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_flash_guard.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned serialization and partition-permission guard for CP raw Flash.
 ****************************************************************************/

#ifndef __BOARDS_ARM_BK7258_SRC_BK7258_FLASH_GUARD_H
#define __BOARDS_ARM_BK7258_SRC_BK7258_FLASH_GUARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_flash_guard_owner_e
{
  BK7258_FLASH_GUARD_NONE = 0,
  BK7258_FLASH_GUARD_DATA = 1
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Serialize every team-owned CP persistent-data transaction.  A zero timeout
 * preserves the filesystem wait-forever behavior.
 * The SDK permission wrapper grants writes only to the calling PID, owner
 * kind and exact frozen range held by this guard.
 */

int bk7258_flash_guard_lock(enum bk7258_flash_guard_owner_e owner,
                            bool write_access, uint32_t timeout_ms);
void bk7258_flash_guard_unlock(void);

/* Called only by the project-owned SDK partition wrapper.  This is a narrow
 * capability check, not a general address permission API: it succeeds only
 * while the current task owns the guard for the exact generated range.
 */

bool bk7258_flash_guard_write_authorized(uint32_t addr, uint32_t size);

#endif /* __BOARDS_ARM_BK7258_SRC_BK7258_FLASH_GUARD_H */
