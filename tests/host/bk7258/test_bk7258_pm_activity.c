/****************************************************************************
 * tests/host/bk7258/test_bk7258_pm_activity.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bk7258_pm_activity.h"

int main(void)
{
  struct bk7258_pm_activity_s activity = {0};
  struct bk7258_pm_activity_s saved;

  assert(bk7258_pm_activity_idle(&activity));
  assert(!bk7258_pm_activity_idle(NULL));

  assert(bk7258_pm_activity_vote(&activity, 0, 0) == 0);
  assert(activity.awake_low == 1u);
  assert(activity.awake_high == 0u);
  assert(!bk7258_pm_activity_idle(&activity));

  assert(bk7258_pm_activity_vote(&activity, 37, 0) == 0);
  assert(activity.awake_low == 1u);
  assert(activity.awake_high == (1u << 5));

  assert(bk7258_pm_activity_vote(&activity, 0, 0) == 0);
  assert(activity.awake_low == 1u);
  assert(bk7258_pm_activity_vote(&activity, 0, 1) == 0);
  assert(activity.awake_low == 0u);
  assert(!bk7258_pm_activity_idle(&activity));

  assert(bk7258_pm_activity_vote(&activity, 37, 1) == 0);
  assert(bk7258_pm_activity_idle(&activity));

  saved = activity;
  assert(bk7258_pm_activity_vote(NULL, 0, 0) == -EINVAL);
  assert(bk7258_pm_activity_vote(&activity,
                                 BK7258_PM_SDK_SLEEP_MODULE_COUNT,
                                 0) == -EINVAL);
  assert(bk7258_pm_activity_vote(&activity, 0, 2) == -EINVAL);
  assert(activity.awake_low == saved.awake_low);
  assert(activity.awake_high == saved.awake_high);

  puts("bk7258 PM activity tests: PASS");
  return 0;
}
