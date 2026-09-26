/****************************************************************************
 * app/bk7258/bk7258_focus_intent.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef BK7258_FOCUS_INTENT_H
#define BK7258_FOCUS_INTENT_H
/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_focus.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Single-slot handoff to the existing product owner. Admission is closed
 * until that owner publishes ready. No caller receives timer ownership.
 * Queue cancellation only cancels a pending intent, never an applied timer.
 */

struct bkfocus_intent_status_s
{
  uint32_t id;
  unsigned int phase; /* 0 idle, 1 pending, 2 applied, 3 failed, 4 canceled */
  int error;
  bool ready;
  uint64_t observed_ms;
  struct bkfocus_snapshot_s timer;
};
/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bkfocus_intent_submit(unsigned int action, uint64_t duration,
                          uint32_t *id);
int bkfocus_intent_cancel(uint32_t id);
void bkfocus_intent_status(struct bkfocus_intent_status_s *status);

/* Called only by product owner, after power/reset gates, before wire work. */

void bkfocus_intent_step(uint64_t now, bool admitted);
struct cJSON;
int bkfocus_tool_execute(const struct cJSON *args, char *output,
                          size_t capacity, int (*check)(void *),
                          void *context);
#endif
