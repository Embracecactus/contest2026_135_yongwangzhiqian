/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_nfc_scene.h"
#include "bk7258_nfc_core.h"
#include "bk7258_focus_intent.h"
#include "bk7258_local_content.h"
#include <errno.h>
#include <string.h>
int bknfc_scene_init(struct bknfc_scene_s *state, uint64_t generation)
{
  if (!state || !generation) return -EINVAL;
  memset(state, 0, sizeof(*state));
  state->generation = generation;
  state->armed = true;
  return 0;
}
int bknfc_scene_observe(struct bknfc_scene_s *state,
                         const struct bknfc_bindings_s *bindings,
                         uint64_t generation, uint64_t sequence,
                         unsigned int kind, const struct bknfc_card_s *card,
                         bool admitted, uint32_t *intent)
{
  if (intent) *intent = 0;
  if (!state || !bindings || !intent || !generation || !sequence ||
      kind > BKNFC_SCENE_PRESENT ||
      (kind == BKNFC_SCENE_PRESENT ? !bknfc_card_valid(card) : card != NULL))
    return -EINVAL;
  if (generation != state->generation || sequence <= state->sequence)
    return -ESTALE;
  state->sequence = sequence;
  if (kind == BKNFC_SCENE_UNKNOWN) return 0;
  if (kind == BKNFC_SCENE_ABSENT)
    {
      state->armed = true;
      return 0;
    }
  if (!state->armed) return -EALREADY;
  state->armed = false;
  if (!admitted) return -ESHUTDOWN;
  uint64_t duration;
  unsigned int action;
  int ret = bknfc_bindings_lookup_action(bindings, card, &action, &duration);
  if (ret < 0) return ret;
  state->action = action;
  if (action >= 5) return bkcontent_submit(action, duration, intent);
  return bkfocus_intent_submit(action, duration, intent);
}
