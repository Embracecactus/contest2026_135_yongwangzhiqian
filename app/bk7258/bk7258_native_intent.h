/****************************************************************************
 * apps/bk7258/bk7258_native_intent.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __APP_BK7258_NATIVE_INTENT_H
#define __APP_BK7258_NATIVE_INTENT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NFC scene action 7 argument: app-ID hash in the high word, run seconds
 * (0 selects the app default of 60) in the low word.
 */

#define BKNATIVE_INTENT_SECONDS_MAX 3600u
#define BKNATIVE_INTENT_PENDING_MS 2000u

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bknative_intent_status_s
{
  uint32_t id;
  unsigned int phase; /* 0 idle, 1 pending, 2 started, 3 failed, 4 canceled */
  int error;
  uint32_t app_hash;
  uint32_t seconds;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* FNV-1a over the app ID: the stable card reference for one installed app. */

uint32_t bknative_app_hash(const char *id);
bool bknative_intent_argument_valid(uint64_t argument);

/* Producer side (NFC worker).  One slot; a pending request is never
 * replaced, so a second tap while one waits is refused with -EBUSY.
 */

int bknative_intent_submit(uint64_t argument, uint32_t *id);
int bknative_intent_cancel(uint32_t id);
void bknative_intent_status(struct bknative_intent_status_s *status);

/* Consumer side (product loop, CPU0).  take() expires a request pending for
 * longer than BKNATIVE_INTENT_PENDING_MS and otherwise hands it over only
 * while admitted; finish() publishes the start result exactly once.
 */

bool bknative_intent_take(bool admitted, struct bknative_intent_status_s *out);
void bknative_intent_finish(uint32_t id, int result);

#endif
