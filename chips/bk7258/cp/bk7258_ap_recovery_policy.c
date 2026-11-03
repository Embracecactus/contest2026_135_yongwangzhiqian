/****************************************************************************
 * chips/bk7258/cp/bk7258_ap_recovery_policy.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded automatic-recovery policy for the AP Supervisor.  A restart is
 * permitted only while fewer than max_in_window automatic restarts started
 * within window_ms and the per-boot lifetime_max is not reached.  Neither
 * bound is reset by a healthy interval, so a fault that recurs just after
 * each stable period still reaches lockout instead of forming a reset loop.
 ****************************************************************************/

#include <string.h>

#include "bk7258_ap_recovery_policy.h"

static uint32_t bk7258_ap_recovery_saturating_inc(uint32_t value)
{
  return value == UINT32_MAX ? value : value + 1u;
}

void bk7258_ap_recovery_policy_init(
  struct bk7258_ap_recovery_policy_s *policy, uint32_t window_ms,
  uint32_t max_in_window, uint32_t lifetime_max)
{
  memset(policy, 0, sizeof(*policy));
  policy->window_ms = window_ms;
  policy->max_in_window = max_in_window > BK7258_AP_RECOVERY_HISTORY ?
                          BK7258_AP_RECOVERY_HISTORY : max_in_window;
  policy->lifetime_max = lifetime_max;
}

uint32_t bk7258_ap_recovery_window_count(
  const struct bk7258_ap_recovery_policy_s *policy, uint32_t now_ms)
{
  uint32_t count = 0;
  uint32_t index;

  for (index = 0; index < policy->history_count; index++)
    {
      /* Unsigned subtraction keeps the window correct across wrap. */

      if ((uint32_t)(now_ms - policy->history[index]) < policy->window_ms)
        {
          count++;
        }
    }

  return count;
}

enum bk7258_ap_recovery_decision_e bk7258_ap_recovery_on_fault(
  struct bk7258_ap_recovery_policy_s *policy, uint32_t now_ms,
  bool auto_enabled)
{
  uint32_t index;

  policy->lifetime_faults =
    bk7258_ap_recovery_saturating_inc(policy->lifetime_faults);
  if (policy->recovered_once)
    {
      policy->recurring_faults =
        bk7258_ap_recovery_saturating_inc(policy->recurring_faults);
    }

  if (!auto_enabled || policy->max_in_window == 0u)
    {
      return BK7258_AP_RECOVERY_NONE;
    }

  if (bk7258_ap_recovery_window_count(policy, now_ms) >=
        policy->max_in_window ||
      policy->lifetime_auto_attempts >= policy->lifetime_max)
    {
      policy->lockouts = bk7258_ap_recovery_saturating_inc(policy->lockouts);
      return BK7258_AP_RECOVERY_LOCKOUT;
    }

  /* Keep the newest max_in_window start times; older entries are outside
   * any decision that can still be affected by the window.
   */

  if (policy->history_count < policy->max_in_window)
    {
      policy->history[policy->history_count++] = now_ms;
    }
  else
    {
      for (index = 1; index < policy->history_count; index++)
        {
          policy->history[index - 1u] = policy->history[index];
        }

      policy->history[policy->history_count - 1u] = now_ms;
    }

  policy->lifetime_auto_attempts =
    bk7258_ap_recovery_saturating_inc(policy->lifetime_auto_attempts);
  return BK7258_AP_RECOVERY_RESTART;
}

void bk7258_ap_recovery_on_result(
  struct bk7258_ap_recovery_policy_s *policy, bool success, bool manual)
{
  if (!success)
    {
      return;
    }

  policy->lifetime_recoveries =
    bk7258_ap_recovery_saturating_inc(policy->lifetime_recoveries);
  policy->recovered_once = true;
  if (manual)
    {
      policy->history_count = 0u;
    }
}
