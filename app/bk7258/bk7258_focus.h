/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_FOCUS_H
#define BK7258_FOCUS_H
#include "bk7258_control_session.h"
/* Serialized product owner only. No I/O, allocation, threads or persistence.
 * FOC1: magic/action BE32/expected revision BE64/operation BE64/duration ms BE64.
 * Actions: 1 start, 2 pause, 3 resume, 4 cancel. Duration only for start.
 * FOS1: magic/state BE32/revision BE64/remaining ms BE64/duration ms BE64.
 * States: 0 idle, 1 running, 2 paused, 3 completed, 4 canceled.
 * Exact last successful operation retry is idempotent; stale revisions fail.
 * READ never advances state. step() alone reports a completion once.
 */
/* Transport-independent entry for the same serialized product owner.
 * Callers must pass through product authorization/power admission first.
 * A sensor callback must enqueue an intent, never call this from its worker.
 * Actions and states retain the FOC1/FOS1 values; no extra timer is created.
 * Exact retry compares fields, not struct padding; all mutations use the
 * same revision/operation domain as authenticated wire requests.
 */
struct bkfocus_request_s
{
  unsigned int action;
  uint64_t revision;
  uint64_t operation;
  uint64_t duration_ms;
};

struct bkfocus_snapshot_s
{
  unsigned int state;
  uint64_t revision;
  uint64_t remaining_ms;
  uint64_t duration_ms;
};

int bkfocus_execute(const struct bkfocus_request_s *request, uint64_t now);
int bkfocus_snapshot(struct bkfocus_snapshot_s *snapshot, uint64_t now);

int bkfocus_control(enum bkcontrol_command_e command, uint32_t offset,
                    const uint8_t *record, size_t size,
                    struct bkcontrol_status_s *status, uint64_t now);
int bkfocus_step(uint64_t now);
void bkfocus_cancel(void);
/* Compact read-only display intent: state in high byte, progress 0..32 low. */
unsigned bkfocus_visual(uint64_t now);
#endif
