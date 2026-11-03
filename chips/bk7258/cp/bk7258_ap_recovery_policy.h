/****************************************************************************
 * chips/bk7258/cp/bk7258_ap_recovery_policy.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure retry/lockout and evidence-counter policy for the CP-owned AP
 * Supervisor.  It has no NuttX or SDK dependency so the host can exercise
 * the exact policy compiled into the CP image.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CP_BK7258_AP_RECOVERY_POLICY_H
#define __ARCH_ARM_SRC_BK7258_CP_BK7258_AP_RECOVERY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* Upper bound for the per-window automatic-recovery budget.  Matches the
 * Kconfig range of BK7258_AP_SUPERVISOR_MAX_RECOVERIES.
 */

#define BK7258_AP_RECOVERY_HISTORY 10u

enum bk7258_ap_recovery_decision_e
{
  BK7258_AP_RECOVERY_NONE = 0,     /* automatic recovery not permitted */
  BK7258_AP_RECOVERY_RESTART,      /* within budget: restart AP once */
  BK7258_AP_RECOVERY_LOCKOUT       /* budget exhausted: manual action only */
};

/* Lifetime counters are cumulative for the CP boot and are never cleared by
 * a healthy interval.  Only the timestamp history is the rolling budget; it
 * decays strictly with time (window_ms) or an explicit manual recovery.
 */

struct bk7258_ap_recovery_policy_s
{
  uint32_t window_ms;
  uint32_t max_in_window;
  uint32_t lifetime_max;
  uint32_t history[BK7258_AP_RECOVERY_HISTORY];
  uint32_t history_count;
  uint32_t lifetime_faults;
  uint32_t lifetime_recoveries;        /* successful restarts, any origin */
  uint32_t lifetime_auto_attempts;     /* automatic restarts started */
  uint32_t recurring_faults;           /* faults after a successful restart */
  uint32_t lockouts;
  bool recovered_once;
};

void bk7258_ap_recovery_policy_init(
  struct bk7258_ap_recovery_policy_s *policy, uint32_t window_ms,
  uint32_t max_in_window, uint32_t lifetime_max);

/* Number of automatic restarts started within the rolling window. */

uint32_t bk7258_ap_recovery_window_count(
  const struct bk7258_ap_recovery_policy_s *policy, uint32_t now_ms);

/* Record one confirmed fault and decide the automatic action.  With
 * auto_enabled false the counters still advance and NONE is returned.
 */

enum bk7258_ap_recovery_decision_e bk7258_ap_recovery_on_fault(
  struct bk7258_ap_recovery_policy_s *policy, uint32_t now_ms,
  bool auto_enabled);

/* Record the result of one restart.  A successful manual (operator)
 * recovery clears the rolling budget but never the lifetime evidence.
 */

void bk7258_ap_recovery_on_result(
  struct bk7258_ap_recovery_policy_s *policy, bool success, bool manual);

#endif /* __ARCH_ARM_SRC_BK7258_CP_BK7258_AP_RECOVERY_POLICY_H */
