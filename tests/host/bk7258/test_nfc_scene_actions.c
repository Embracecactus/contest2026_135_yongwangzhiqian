/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_nfc_bindings.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
  struct bknfc_bindings_s state = {0};
  struct bknfc_bindings_s reopened = {0};
  struct bknfc_card_s card = {.size = 4, .uid = {1, 2, 3, 4}};
  uint64_t argument;
  unsigned int action;
  char root[] = "/tmp/shaniu-scene-actions-XXXXXX";

  assert(mkdtemp(root));
  assert(bknfc_bindings_open(&state, root) == 0);
  assert(bknfc_bindings_set(&state, 0, 1, 0, &card, 60000) == 0);
  assert(bknfc_bindings_lookup_action(&state, &card, &action, &argument) == 0);
  assert(action == 1 && argument == 60000);

  /* A mapped pause is explicit, not inferred from a UID or timer state. */

  assert(bknfc_bindings_set_action(&state, 1, 2, 0, &card, 2, 0) == 0);
  assert(bknfc_bindings_open(&reopened, root) == 0);
  assert(bknfc_bindings_lookup_action(&reopened, &card,
                                    &action, &argument) == 0);
  assert(action == 2 && argument == 0);
  assert(bknfc_bindings_lookup(&state, &card, &argument) == -ENOTSUP);
  assert(bknfc_bindings_set_action(&state, 1, 2, 0, &card, 2, 0) == 0);
  assert(bknfc_bindings_set_action(&state, 1, 2, 0, &card, 3, 0) == -EEXIST);

  assert(bknfc_bindings_set_action(&state, 2, 3, 0, &card, 5, 1) == 0);
  assert(bknfc_bindings_lookup_action(&state, &card, &action, &argument) == 0);
  assert(action == 5 && argument == 1);
  assert(bknfc_bindings_set_action(&state, 3, 4, 0, &card, 5, 2) == -EINVAL);
  assert(bknfc_bindings_set_action(&state, 3, 4, 0, &card, 6, 1) == -EINVAL);
  assert(state.revision == 3);
  assert(bknfc_bindings_set_action(&state, 3, 4, 0, &card, 6, 0) == 0);
  assert(bknfc_bindings_set(&state, 4, 5, 0, NULL, 0) == 0);
  assert(bknfc_bindings_lookup_action(&state, &card,
                                    &action, &argument) == -ENOENT);
  assert(bknfc_bindings_reset(root) == 0);
  puts("SCENE_ACTION_MAPPING_PASS");
  return 0;
}
