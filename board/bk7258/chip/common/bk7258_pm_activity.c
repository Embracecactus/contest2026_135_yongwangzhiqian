/****************************************************************************
 * board/bk7258/chip/common/bk7258_pm_activity.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bk7258_pm_activity.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_pm_activity_vote(struct bk7258_pm_activity_s *activity,
                            uint32_t module, uint32_t sleep_state)
{
  uint32_t bit;

  if (activity == NULL || module >= BK7258_PM_SDK_SLEEP_MODULE_COUNT ||
      sleep_state > 1u)
    {
      return -EINVAL;
    }

  bit = 1u << (module & 31u);
  if (module < 32u)
    {
      if (sleep_state == 0u)
        {
          activity->awake_low |= bit;
        }
      else
        {
          activity->awake_low &= ~bit;
        }
    }
  else
    {
      if (sleep_state == 0u)
        {
          activity->awake_high |= bit;
        }
      else
        {
          activity->awake_high &= ~bit;
        }
    }

  return 0;
}

bool bk7258_pm_activity_idle(
  const struct bk7258_pm_activity_s *activity)
{
  return activity != NULL && activity->awake_low == 0u &&
         activity->awake_high == 0u;
}
