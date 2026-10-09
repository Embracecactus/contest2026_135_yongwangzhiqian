/****************************************************************************
 * app/bk7258/bk7258_pc_tasks.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_PC_TASKS_H
#define __APP_BK7258_PC_TASKS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_control_session.h"

/* One volatile task on the serialized product owner, never a worker or ISR.
 * PTE1: magic4, state BE32 (1 start, 2 progress, 3 success, 4 failure,
 * 5 canceled), nonzero task ID16, positive event sequence BE64, remaining
 * TTL ms BE32, progress BE32 (0..100 or UINT32_MAX unknown).
 * Start requires progress zero.
 * Sender sequences increase across tasks within one authorization binding.
 * IDs must not be reused within that binding. Only an explicit start can
 * replace a terminal/expired task. Exact last retry never renews its TTL.
 * PTS1: magic4/state4/ID16/sequence8/remaining-ms8/flags4/progress4.
 * Flags: admission=1, expired=2, terminal-awaiting-feedback=4.
 * Progress updates are limited to one per second after the first progress;
 * Terminal events bypass the limit; rejected events do not advance sequence.
 * Acceptance is not a display receipt. Local visual selection is bounded
 * by the receiver TTL.
 */

struct bkpc_tasks_s
{
  uint64_t binding;
  uint64_t grant;
  uint64_t observed;
  uint64_t deadline;
  uint8_t last[40];
  bool admitted;
  bool expired;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void bkpc_tasks_bind(struct bkpc_tasks_s *state, uint64_t binding,
                     uint64_t grant);
int bkpc_tasks_apply(struct bkpc_tasks_s *state, const void *record,
                     size_t size, uint64_t now);
void bkpc_tasks_step(struct bkpc_tasks_s *state, uint64_t now,
                     bool admitted);
/* Existing display owner uses states 4/5/6 for task result shapes.
 * Voice availability gates all visuals; an active/paused timer keeps priority.
 * Completed focus feedback yields to a nonexpired terminal task notification.
 */

unsigned int bkpc_tasks_visual(const struct bkpc_tasks_s *state,
                               uint64_t now, bool available,
                               unsigned int focus);
void bkpc_tasks_snapshot(const struct bkpc_tasks_s *state, uint8_t out[48],
                         uint64_t now);
int bkpc_tasks_control(struct bkpc_tasks_s *state,
                       enum bkcontrol_command_e command, uint32_t offset,
                       const uint8_t *record, size_t size,
                       struct bkcontrol_status_s *status, uint64_t now);
#endif
