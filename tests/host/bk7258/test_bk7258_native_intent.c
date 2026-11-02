/* SPDX-License-Identifier: Apache-2.0 */
/* NFC action 7 → native-app intent slot.  Built with native apps enabled;
 * focus and content owners are stubs because only routing is under test.
 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_native_intent.h"
#include "bk7258_nfc_scene.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned int g_other_owner;
int bkcontent_submit(unsigned int action, uint64_t content, uint32_t *id)
{ (void)action; (void)content; *id = 0; g_other_owner++; return -ENOTSUP; }
int bkfocus_intent_submit(unsigned int action, uint64_t duration, uint32_t *id)
{ (void)action; (void)duration; *id = 0; g_other_owner++; return -ENOTSUP; }

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
  assert(!bknative_intent_take(false, &status));
  assert(bknative_intent_take(true, &status));
  assert(status.id == 1 && status.app_hash == 0xbd8bb2f5u && status.seconds == 30);
  assert(!bknative_intent_take(true, &status));          /* handed over once */
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
  assert(!bknative_intent_take(true, &status));

  assert(bknative_intent_submit(HASH << 32, &id) == 0);
  assert(bknative_intent_take(true, &status));
  bknative_intent_finish(id, 0);
  bknative_intent_status(&status);
  assert(status.phase == 2 && status.error == 0);

  /* A request not admitted within its window expires instead of waiting. */
  assert(bknative_intent_submit(HASH << 32, &id) == 0);
  struct timespec wait = { 2, 200000000 };
  nanosleep(&wait, NULL);
  assert(!bknative_intent_take(true, &status));
  bknative_intent_status(&status);
  assert(status.id == id && status.phase == 3 && status.error == -ETIMEDOUT);

  /* The scene routes action 7 to this slot and nowhere else. */
  struct bknfc_bindings_s bindings = {0};
  struct bknfc_scene_s scene;
  const struct bknfc_card_s card = {.size = 4, .uid = {1, 2, 3, 4}};
  char root[] = "/tmp/shaniu-native-intent-XXXXXX";
  assert(mkdtemp(root));
  assert(bknfc_bindings_open(&bindings, root) == 0);
  assert(bknfc_bindings_set_action(&bindings, 0, 1, 0, &card, 7,
                                   (HASH << 32) | 45) == 0);
  assert(bknfc_scene_init(&scene, 7) == 0);
  assert(bknfc_scene_observe(&scene, &bindings, 7, 1, BKNFC_SCENE_PRESENT,
                             &card, true, &id) == 0 && id != 0);
  assert(scene.action == 7 && g_other_owner == 0);
  bknative_intent_status(&status);
  assert(status.id == id && status.phase == 1 && status.seconds == 45);
  /* Dwell does not replay; a trusted removal re-arms the card. */
  assert(bknfc_scene_observe(&scene, &bindings, 7, 2, BKNFC_SCENE_PRESENT,
                             &card, true, &second) == -EALREADY && !second);
  assert(bknfc_bindings_reset(root) == 0);
  puts("NATIVE_INTENT_PASS");
  return 0;
}
