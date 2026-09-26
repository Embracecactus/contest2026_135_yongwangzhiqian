/* SPDX-License-Identifier: Apache-2.0 */
/* Production intent/trial and render identity; external clock/pixel sink. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_display_service.h"
struct bkdisplay_service_s { struct bkdisplay_service_status_s status; };
static uint64_t clock_ms = 100;
static uint64_t bkdisplay_now_ms(void) { return clock_ms; }
static int renders, render_error;
static uint32_t cancel_during_render;
static int bkdisplay_render_pixels_locked(struct bkdisplay_service_s *s, const char *expression)
{
  renders++;
  if (cancel_during_render)
    {
      struct bkdisplay_trial_status_s state;
      assert(bk7258_display_trial_status(&state) == 0);
      assert(state.state == BKDISPLAY_TRIAL_RENDERING || state.state == BKDISPLAY_TRIAL_RESTORING);
      assert(bk7258_display_cancel_trial(cancel_during_render) == -EBUSY);
    }
  if (render_error) return render_error;
  strcpy(s->status.expression, expression);
  return 0;
}
#include "bk7258_display_render_identity.inc"
#include "bk7258_display_intent.inc"
static void expect(uint32_t id, enum bkdisplay_trial_state_e expected)
{
  struct bkdisplay_trial_status_s state;
  assert(bk7258_display_trial_status(&state) == 0);
  assert(state.id == id && state.state == expected);
}
int main(void)
{
  struct bkdisplay_service_s service = {0};
  uint32_t id = 0, later = 0;
  strcpy(service.status.expression, "neutral");
  bkdisplay_intent_gate(true);
  assert(bk7258_display_trial("happy", 0, &id) == -EINVAL);
  assert(bk7258_display_trial("../bad", 30, &id) == -EINVAL);
  assert(bk7258_display_trial("happy", 30, &id) == 0);
  expect(id, BKDISPLAY_TRIAL_PENDING);
  assert(renders == 0);
  assert(bk7258_display_trial("sad", 30, &later) == -EBUSY);
  assert(bkdisplay_intent_step(&service, true));
  expect(id, BKDISPLAY_TRIAL_ACTIVE);
  assert(!strcmp(service.status.expression, "happy"));
  clock_ms = 129;
  assert(!bkdisplay_trial_step(&service, true));
  clock_ms = 130;
  assert(bkdisplay_trial_step(&service, true));
  expect(id, BKDISPLAY_TRIAL_EXPIRED);
  assert(!strcmp(service.status.expression, "neutral"));
  int before = renders;
  assert(!bkdisplay_trial_step(&service, true) && renders == before);

  assert(bk7258_display_trial("happy", 30, &id) == 0);
  clock_ms += 30;
  assert(bkdisplay_intent_step(&service, true));
  expect(id, BKDISPLAY_TRIAL_EXPIRED);
  assert(renders == before); /* Queue expiry must never flash the trial. */

  assert(bk7258_display_trial("happy", 30, &id) == 0);
  assert(bk7258_display_cancel_trial(id) == 0);
  expect(id, BKDISPLAY_TRIAL_CANCELED);
  assert(!bkdisplay_intent_step(&service, true));
  assert(bk7258_display_cancel_trial(id) == 0);

  assert(bk7258_display_trial("happy", 30, &later) == 0);
  assert(bk7258_display_cancel_trial(id) == -ESTALE);
  assert(bkdisplay_intent_step(&service, true));
  assert(bk7258_display_cancel_trial(later) == 0);
  expect(later, BKDISPLAY_TRIAL_CANCEL_PENDING);
  assert(bkdisplay_trial_step(&service, true));
  expect(later, BKDISPLAY_TRIAL_CANCELED);
  assert(!strcmp(service.status.expression, "neutral"));

  assert(bk7258_display_trial("happy", 30, &id) == 0);
  assert(bkdisplay_intent_step(&service, true));
  assert(bkdisplay_render_locked(&service, "happy") == 0); /* New same-name choice. */
  before = renders;
  clock_ms += 30;
  assert(bkdisplay_trial_step(&service, true));
  expect(id, BKDISPLAY_TRIAL_SUPERSEDED);
  assert(renders == before && !strcmp(service.status.expression, "happy"));

  assert(bk7258_display_trial("sad", 30, &id) == 0);
  assert(bkdisplay_intent_step(&service, true));
  assert(bk7258_display_request_expression("shy", &later) == 0);
  assert(bkdisplay_intent_step(&service, true));
  assert(bkdisplay_trial_step(&service, true));
  expect(id, BKDISPLAY_TRIAL_SUPERSEDED);
  assert(!strcmp(service.status.expression, "shy"));

  assert(bk7258_display_trial("happy", 30, &id) == 0);
  assert(bkdisplay_intent_step(&service, true));
  bkdisplay_intent_gate(false);
  expect(id, BKDISPLAY_TRIAL_SUPERSEDED);
  before = renders;
  clock_ms += 30;
  assert(!bkdisplay_trial_step(&service, true) && renders == before);
  bkdisplay_intent_gate(true);
  assert(bk7258_display_trial("happy", 30, &id) == 0);
  render_error = -EIO;
  assert(bkdisplay_intent_step(&service, true));
  expect(id, BKDISPLAY_TRIAL_FAILED);
  render_error = 0;
  assert(!bkdisplay_trial_step(&service, true));
  assert(bk7258_display_trial("happy", 30, &id) == 0);
  cancel_during_render = id;
  assert(bkdisplay_intent_step(&service, true));
  assert(bk7258_display_cancel_trial(id) == 0);
  render_error = -EIO;
  assert(bkdisplay_trial_step(&service, true));
  expect(id, BKDISPLAY_TRIAL_FAILED);
  assert(bk7258_display_cancel_trial(id) == -EALREADY);
  assert(!bkdisplay_trial_step(&service, true));
  cancel_during_render = 0;
  render_error = 0;
  assert(bk7258_display_trial("happy", 30, &id) == 0);
  assert(bkdisplay_intent_step(&service, false));
  expect(id, BKDISPLAY_TRIAL_FAILED);

  assert(bk7258_display_trial("happy", 30, &id) == 0);
  assert(bk7258_display_cancel_expression(id) == 0);
  expect(id, BKDISPLAY_TRIAL_CANCELED);
  assert(!bkdisplay_intent_step(&service, true));

  assert(bk7258_display_trial("happy", 30, &id) == 0);
  assert(bkdisplay_intent_step(&service, true));
  clock_ms--;
  assert(bkdisplay_trial_step(&service, true));
  expect(id, BKDISPLAY_TRIAL_FAILED); /* Clock reversal restores once; never extends TTL. */
  clock_ms = 0;
  assert(bk7258_display_trial("happy", 30, &id) == -EAGAIN);
  clock_ms = UINT64_MAX - 2;
  assert(bk7258_display_trial("happy", 30, &id) == -EOVERFLOW);
  bkdisplay_intent_supersede();
  assert(!bkdisplay_intent_pending());
  puts("CONTRACT_PASS expression trial");
}
