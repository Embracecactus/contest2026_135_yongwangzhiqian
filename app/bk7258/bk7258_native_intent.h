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

/* Compact local-trigger argument: app-ID hash in the high word, run seconds
 * (0 selects the app default of 60) in the low word.  Product integrations
 * decide which authenticated local trigger, if any, is allowed to submit it.
 */

#define BKNATIVE_INTENT_SECONDS_MAX 3600u
#define BKNATIVE_INTENT_PENDING_MS 2000u

/* A voice start waits for the current voice interaction to end, because a
 * native app is admitted only while voice is idle.
 */

#define BKNATIVE_INTENT_VOICE_MS 30000u

#define BKNATIVE_CATALOG_APPS 4u
#define BKNATIVE_SCHEDULE_MAX 4u
#define BKNATIVE_LABEL_MAX 17u      /* 16 UTF-8 bytes plus NUL */
#define BKNATIVE_DAYS_ALL 0x7fu     /* bit 0 Sunday .. bit 6 Saturday */

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bknative_intent_kind_e
{
  BKNATIVE_INTENT_START = 1,
  BKNATIVE_INTENT_STOP = 2,
  BKNATIVE_INTENT_SCHEDULE_SET = 3,
  BKNATIVE_INTENT_SCHEDULE_CLEAR = 4
};

struct bknative_request_s
{
  unsigned int kind;
  uint32_t app_hash;
  uint32_t seconds;       /* START, SCHEDULE_SET: 0..3600, 0 = default */
  uint32_t minute;        /* SCHEDULE_SET: 0..1439 local */
  uint32_t days;          /* SCHEDULE_SET: 1..BKNATIVE_DAYS_ALL */
  uint32_t window_ms;     /* 1..BKNATIVE_INTENT_VOICE_MS */
};

struct bknative_intent_status_s
{
  uint32_t id;
  unsigned int phase; /* 0 idle, 1 pending, 2 applied, 3 failed, 4 canceled */
  int error;
  unsigned int kind;
  uint32_t app_hash;
  uint32_t seconds;
  uint32_t minute;
  uint32_t days;
};

/* Metadata snapshot published by the manager on CPU0 so that the Agent tool
 * and the local-text route can read app facts from any thread.
 */

struct bknative_catalog_app_s
{
  char id[32];
  char name[65];
  uint32_t hash;
  uint32_t version;
  bool blocked;
  unsigned int result_count;
  char results[4][BKNATIVE_LABEL_MAX];
};

struct bknative_catalog_schedule_s
{
  uint32_t app_hash;      /* 0 marks an empty slot */
  uint32_t minute;
  uint32_t days;
  uint32_t seconds;
  int last_result;        /* 1 not yet due, 0 started, negative skipped */
};

struct bknative_catalog_s
{
  bool valid;
  uint64_t revision;
  unsigned int count;
  struct bknative_catalog_app_s apps[BKNATIVE_CATALOG_APPS];
  struct bknative_catalog_schedule_s schedule[BKNATIVE_SCHEDULE_MAX];
  char running[32];
  char last_app[32];
  int last_result;
  uint32_t last_value;    /* ABI 3 result of the last clean completion */
  bool clock_valid;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* FNV-1a over the app ID: the stable card reference for one installed app. */

uint32_t bknative_app_hash(const char *id);
bool bknative_intent_argument_valid(uint64_t argument);

/* Producer side.  One slot; a pending request is never replaced, so a second
 * request while one waits is refused with -EBUSY.  submit() is the compact
 * START shape with the short local-trigger window.
 */

int bknative_intent_submit(uint64_t argument, uint32_t *id);
int bknative_intent_request(const struct bknative_request_s *request,
                            uint32_t *id);
int bknative_intent_cancel(uint32_t id);
void bknative_intent_status(struct bknative_intent_status_s *status);

/* Consumer side (product loop, CPU0).  take() expires a request pending past
 * its window.  START is handed over only while apps are admitted; the other
 * kinds only while control is admitted.  finish() publishes the result once.
 */

bool bknative_intent_take(bool app_admitted, bool control_admitted,
                          struct bknative_intent_status_s *out);
void bknative_intent_finish(uint32_t id, int result);

void bknative_catalog_publish(const struct bknative_catalog_s *catalog);
void bknative_catalog_read(struct bknative_catalog_s *catalog);

/* Local-text route: "启动/打开/开始" + an installed app name, or a decision
 * request when an app with ID "shake" is installed.  Pure; returns true and
 * the app hash on a match.
 */

bool bknative_text_match(const char *text,
                         const struct bknative_catalog_s *catalog,
                         uint32_t *hash);

#endif
