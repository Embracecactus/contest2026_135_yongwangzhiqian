/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host tests for the AP Supervisor recovery policy: lifetime counters,
 * rolling retry window, lockout and the absence of a healthy-interval reset.
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>

#include "bk7258_ap_recovery_policy.h"

#define WINDOW_MS   600000u
#define BUDGET      3u
#define LIFETIME    6u

static void test_product_never_restarts(void)
{
  struct bk7258_ap_recovery_policy_s policy;
  unsigned int i;

  bk7258_ap_recovery_policy_init(&policy, 0u, 0u, 0u);
  for (i = 0; i < 5u; i++)
    {
      assert(bk7258_ap_recovery_on_fault(&policy, i * 1000u, false) ==
             BK7258_AP_RECOVERY_NONE);
    }

  assert(policy.lifetime_faults == 5u);
  assert(policy.lifetime_auto_attempts == 0u);
  assert(policy.lockouts == 0u);
}

static void test_window_lockout(void)
{
  struct bk7258_ap_recovery_policy_s policy;
  unsigned int i;

  bk7258_ap_recovery_policy_init(&policy, WINDOW_MS, BUDGET, LIFETIME);
  for (i = 0; i < BUDGET; i++)
    {
      assert(bk7258_ap_recovery_on_fault(&policy, 1000u + i, true) ==
             BK7258_AP_RECOVERY_RESTART);
      bk7258_ap_recovery_on_result(&policy, true, false);
    }

  assert(bk7258_ap_recovery_window_count(&policy, 2000u) == BUDGET);
  assert(bk7258_ap_recovery_on_fault(&policy, 2000u, true) ==
         BK7258_AP_RECOVERY_LOCKOUT);
  assert(policy.lockouts == 1u);
  assert(policy.lifetime_faults == BUDGET + 1u);
  assert(policy.lifetime_recoveries == BUDGET);

  /* Faults after the first successful restart are recurring evidence. */

  assert(policy.recurring_faults == BUDGET);
}

static void test_healthy_interval_keeps_evidence(void)
{
  struct bk7258_ap_recovery_policy_s policy;

  bk7258_ap_recovery_policy_init(&policy, WINDOW_MS, BUDGET, LIFETIME);
  assert(bk7258_ap_recovery_on_fault(&policy, 0u, true) ==
         BK7258_AP_RECOVERY_RESTART);
  bk7258_ap_recovery_on_result(&policy, true, false);

  /* 60 s of health is no longer a budget reset: the start remains inside
   * the window and every lifetime counter is retained.
   */

  assert(bk7258_ap_recovery_window_count(&policy, 61000u) == 1u);
  assert(policy.lifetime_faults == 1u);
  assert(policy.lifetime_recoveries == 1u);

  /* Only time beyond the window decays the rolling budget. */

  assert(bk7258_ap_recovery_window_count(&policy, WINDOW_MS) == 0u);
  assert(policy.lifetime_faults == 1u);
}

static void test_lifetime_cap_prevents_slow_loop(void)
{
  struct bk7258_ap_recovery_policy_s policy;
  uint32_t now = 0u;
  unsigned int i;

  bk7258_ap_recovery_policy_init(&policy, WINDOW_MS, BUDGET, LIFETIME);
  for (i = 0; i < LIFETIME; i++)
    {
      now += WINDOW_MS + 1u;
      assert(bk7258_ap_recovery_on_fault(&policy, now, true) ==
             BK7258_AP_RECOVERY_RESTART);
      bk7258_ap_recovery_on_result(&policy, true, false);
    }

  now += WINDOW_MS + 1u;
  assert(bk7258_ap_recovery_on_fault(&policy, now, true) ==
         BK7258_AP_RECOVERY_LOCKOUT);
  assert(policy.lifetime_auto_attempts == LIFETIME);
  assert(policy.recurring_faults == LIFETIME);
}

static void test_manual_recovery_clears_only_budget(void)
{
  struct bk7258_ap_recovery_policy_s policy;
  unsigned int i;

  bk7258_ap_recovery_policy_init(&policy, WINDOW_MS, BUDGET, LIFETIME);
  for (i = 0; i < BUDGET; i++)
    {
      (void)bk7258_ap_recovery_on_fault(&policy, i, true);
    }

  assert(bk7258_ap_recovery_on_fault(&policy, 10u, true) ==
         BK7258_AP_RECOVERY_LOCKOUT);
  bk7258_ap_recovery_on_result(&policy, true, true);
  assert(bk7258_ap_recovery_window_count(&policy, 11u) == 0u);
  assert(policy.lifetime_faults == BUDGET + 1u);
  assert(policy.lifetime_auto_attempts == BUDGET);
  assert(policy.lockouts == 1u);
}

static void test_failed_restart_is_not_recovery(void)
{
  struct bk7258_ap_recovery_policy_s policy;

  bk7258_ap_recovery_policy_init(&policy, WINDOW_MS, BUDGET, LIFETIME);
  (void)bk7258_ap_recovery_on_fault(&policy, 0u, true);
  bk7258_ap_recovery_on_result(&policy, false, false);
  assert(policy.lifetime_recoveries == 0u);
  (void)bk7258_ap_recovery_on_fault(&policy, 1u, true);
  assert(policy.recurring_faults == 0u);
}

static void test_window_wraparound(void)
{
  struct bk7258_ap_recovery_policy_s policy;

  bk7258_ap_recovery_policy_init(&policy, WINDOW_MS, BUDGET, LIFETIME);
  assert(bk7258_ap_recovery_on_fault(&policy, UINT32_MAX - 10u, true) ==
         BK7258_AP_RECOVERY_RESTART);
  assert(bk7258_ap_recovery_window_count(&policy, 100u) == 1u);
}

int main(void)
{
  test_product_never_restarts();
  test_window_lockout();
  test_healthy_interval_keeps_evidence();
  test_lifetime_cap_prevents_slow_loop();
  test_manual_recovery_clears_only_budget();
  test_failed_restart_is_not_recovery();
  test_window_wraparound();
  puts("bk7258 AP recovery policy test: PASS");
  return 0;
}
