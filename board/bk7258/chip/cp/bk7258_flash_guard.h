/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_flash_guard.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-only serialization and SDK partition-permission guard for raw Flash.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_GUARD_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_GUARD_H

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
  BK7258_FLASH_GUARD_DATA = 1,
  BK7258_FLASH_GUARD_OTA_STAGING = 2,
  BK7258_FLASH_GUARD_OTA_METADATA = 3,
  BK7258_FLASH_GUARD_OTA_N17_METADATA = 4
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Serialize every team-owned CP Flash transaction.  A zero timeout preserves
 * the existing LittleFS wait-forever behavior; OTA staging always supplies a
 * finite timeout.  write_access must be false for read-only transactions.
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

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_FLASH_GUARD_H */
