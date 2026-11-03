/* SPDX-License-Identifier: Apache-2.0 */
/* Native-app intent, catalog and recognized-text routing contracts. */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_native_intent.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define HASH UINT64_C(0xbd8bb2f5)

int main(void)
{
  struct bknative_intent_status_s status;
  uint32_t id;
  uint32_t second;

  /* Argument shape: a non-zero hash and at most an hour of run time. */
  assert(!bknative_intent_argument_valid(30));
  assert(!bknative_intent_argument_valid((HASH << 32) | 3601));
  assert(bknative_intent_argument_valid(HASH << 32));
  assert(bknative_intent_submit(30, &id) == -EINVAL);
  assert(bknative_app_hash("shake") == 0xbd8bb2f5u);

  /* One slot: a pending tap is never replaced; take only while admitted. */
  assert(bknative_intent_submit((HASH << 32) | 30, &id) == 0 && id == 1);
  assert(bknative_intent_submit((HASH << 32) | 30, &second) == -EBUSY && !second);
  assert(!bknative_intent_take(false, true, &status));
  assert(bknative_intent_take(true, false, &status));
  assert(status.id == 1 && status.app_hash == 0xbd8bb2f5u && status.seconds == 30);
  assert(!bknative_intent_take(true, false, &status));          /* handed over once */
  assert(bknative_intent_cancel(id) == -ESTALE);          /* start owns it now */
  bknative_intent_finish(id, -EBUSY);
  bknative_intent_status(&status);
  assert(status.phase == 3 && status.error == -EBUSY);
  bknative_intent_finish(id, 0);                          /* exactly once */
  bknative_intent_status(&status);
  assert(status.phase == 3);

  assert(bknative_intent_submit(HASH << 32, &id) == 0 && id == 2);
  assert(bknative_intent_cancel(id) == 0);
  bknative_intent_status(&status);
  assert(status.phase == 4 && status.error == -ECANCELED);
  assert(!bknative_intent_take(true, false, &status));

  assert(bknative_intent_submit(HASH << 32, &id) == 0);
  assert(bknative_intent_take(true, false, &status));
  bknative_intent_finish(id, 0);
  bknative_intent_status(&status);
  assert(status.phase == 2 && status.error == 0);

  /* A request not admitted within its window expires instead of waiting. */
  assert(bknative_intent_submit(HASH << 32, &id) == 0);
  struct timespec wait = { 2, 200000000 };
  nanosleep(&wait, NULL);
  assert(!bknative_intent_take(true, false, &status));
  bknative_intent_status(&status);
  assert(status.id == id && status.phase == 3 && status.error == -ETIMEDOUT);

  /* Request kinds: START waits for app admission, the rest for control. */
  struct bknative_request_s request = {
    .kind = BKNATIVE_INTENT_SCHEDULE_SET, .app_hash = 0xbd8bb2f5u, .seconds = 60,
    .minute = 900, .days = BKNATIVE_DAYS_ALL, .window_ms = BKNATIVE_INTENT_VOICE_MS };
  struct bknative_request_s bad = request;
  bad.minute = 1440; assert(bknative_intent_request(&bad, &id) == -EINVAL);
  bad = request; bad.days = 0x80; assert(bknative_intent_request(&bad, &id) == -EINVAL);
  bad = request; bad.window_ms = BKNATIVE_INTENT_VOICE_MS + 1; assert(bknative_intent_request(&bad, &id) == -EINVAL);
  bad = request; bad.kind = BKNATIVE_INTENT_STOP; assert(bknative_intent_request(&bad, &id) == -EINVAL); /* stray fields */
  bad = request; bad.kind = 9; assert(bknative_intent_request(&bad, &id) == -EINVAL);
  assert(bknative_intent_request(&request, &id) == 0);
  assert(!bknative_intent_take(true, false, &status));      /* control-only kind */
  assert(bknative_intent_take(false, true, &status) && status.kind == BKNATIVE_INTENT_SCHEDULE_SET &&
         status.minute == 900 && status.days == BKNATIVE_DAYS_ALL && status.seconds == 60);
  bknative_intent_finish(id, 0);
  struct bknative_request_s voice = { .kind = BKNATIVE_INTENT_START, .app_hash = 0xbd8bb2f5u,
    .seconds = 0, .window_ms = BKNATIVE_INTENT_VOICE_MS };
  assert(bknative_intent_request(&voice, &id) == 0);
  nanosleep(&wait, NULL);                                    /* > card window */
  assert(!bknative_intent_take(false, true, &status));       /* waits for apps */
  assert(bknative_intent_take(true, false, &status) && status.id == id);
  bknative_intent_finish(id, -EBUSY);

  /* Catalog snapshot and local phrases. */
  struct bknative_catalog_s catalog, copy;
  memset(&catalog, 0, sizeof(catalog));
  assert(!bknative_text_match("帮我做个决定", &catalog, &second));  /* not published */
  catalog.count = 2;
  strcpy(catalog.apps[0].id, "shake"); strcpy(catalog.apps[0].name, "摇一摇做决定");
  catalog.apps[0].hash = bknative_app_hash("shake");
  strcpy(catalog.apps[1].id, "nudge"); strcpy(catalog.apps[1].name, "喝水提醒");
  catalog.apps[1].hash = bknative_app_hash("nudge");
  bknative_catalog_publish(&catalog);
  bknative_catalog_read(&copy);
  assert(copy.valid && copy.count == 2 && !strcmp(copy.apps[1].name, "喝水提醒"));
  uint32_t hash = 0;
  assert(bknative_text_match("傻妞，帮我做个决定。", &copy, &hash) && hash == bknative_app_hash("shake"));
  assert(bknative_text_match("做决定", &copy, &hash) && hash == bknative_app_hash("shake"));
  assert(bknative_text_match("请打开喝水提醒", &copy, &hash) && hash == bknative_app_hash("nudge"));
  assert(bknative_text_match("启动喝水提醒！", &copy, &hash));
  assert(!bknative_text_match("不要打开喝水提醒", &copy, &hash));
  assert(!bknative_text_match("能帮我做个决定吗？", &copy, &hash));
  assert(!bknative_text_match("打开喝水", &copy, &hash));         /* exact names only */
  assert(!bknative_text_match("如果下雨就帮我做个决定", &copy, &hash));
  assert(!bknative_text_match("打开", &copy, &hash));
  copy.apps[0].hash = 1; strcpy(copy.apps[0].id, "other");
  assert(!bknative_text_match("帮我做个决定", &copy, &hash));    /* needs "shake" */

  puts("NATIVE_INTENT_PASS");
  return 0;
}
