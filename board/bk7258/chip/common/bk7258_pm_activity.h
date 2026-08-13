/****************************************************************************
 * board/bk7258/chip/common/bk7258_pm_activity.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small role-neutral model of the v3.1.1.9 SDK sleep-vote set-state API.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_COMMON_BK7258_PM_ACTIVITY_H
#define __BOARD_BK7258_CHIP_COMMON_BK7258_PM_ACTIVITY_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CP and AP v3.1.1.9 pm.h define the same contiguous sleep-module ABI,
 * I2C1=0 through ENET=37.  Keep the raw IDs private to the compatibility
 * wrapper; NuttX drivers continue to use board-owned clock/frequency IDs.
 */

#define BK7258_PM_SDK_SLEEP_MODULE_COUNT 38u

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_pm_activity_s
{
  uint32_t awake_low;
  uint32_t awake_high;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_pm_activity_vote(struct bk7258_pm_activity_s *activity,
                            uint32_t module, uint32_t sleep_state);
bool bk7258_pm_activity_idle(
  const struct bk7258_pm_activity_s *activity);

#endif /* __BOARD_BK7258_CHIP_COMMON_BK7258_PM_ACTIVITY_H */
