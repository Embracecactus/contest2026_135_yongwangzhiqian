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
int bkfocus_control(enum bkcontrol_command_e command, uint32_t offset,
                    const uint8_t *record, size_t size,
                    struct bkcontrol_status_s *status, uint64_t now);
int bkfocus_step(uint64_t now);
void bkfocus_cancel(void);
#endif
