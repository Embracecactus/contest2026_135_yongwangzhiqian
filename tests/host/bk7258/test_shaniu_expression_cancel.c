/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_display_service.h"
struct bkdisplay_service_s { int unused; };
static int renders, cancel_during_render;
static int bkdisplay_render_locked(struct bkdisplay_service_s *service, const char *expression)
{
  (void)service; (void)expression;
  renders++;
  if (cancel_during_render) {
    struct bkdisplay_expression_request_s s;
    assert(bk7258_display_expression_status(&s) == 0);
    assert(bk7258_display_cancel_expression(s.id) == -EBUSY);
  }
  return 0;
}
#include "bk7258_display_intent.inc"
static int voice_result, replace_during_cancel;
static uint32_t newer;
static int voice_channel_cancel(void)
{
  if (replace_during_cancel) {
    bkdisplay_intent_supersede();
    assert(bk7258_display_request_expression("shy", &newer) == 0);
  }
  return voice_result;
}
#include "bk7258_agent_display_control.inc"
static int checks, reject_at;
static int guard(void *context)
{
  (void)context;
  checks++;
  return checks == reject_at ? -ECANCELED : 0;
}
int main(void)
{
  struct bkdisplay_expression_request_s state;
  struct bkdisplay_service_s service = {0};
  uint32_t old = 0;
  bkdisplay_intent_gate(true);
  reject_at = 1;
  assert(product_expression_request("happy", &old, guard, NULL) == -ECANCELED);
  assert(bk7258_display_expression_status(&state) == 0 && state.id == 0);
  checks = 0; reject_at = 2;
  assert(product_expression_request("happy", &old, guard, NULL) == -ECANCELED);
  assert(bk7258_display_expression_status(&state) == 0 && state.state == BKDISPLAY_EXPRESSION_CANCELED);
  assert(bk7258_display_cancel_expression(old) == 0);
  assert(!bkdisplay_intent_pending());
  assert(!bkdisplay_intent_step(&service, true) && renders == 0);
  checks = 0; reject_at = 0;
  assert(product_expression_request("happy", &old, guard, NULL) == 0);
  voice_result = -ENOENT;
  assert(product_cancel() == 0); /* Voice idle, but pending expression canceled. */
  assert(!bkdisplay_intent_step(&service, true));
  assert(product_expression_request("happy", &old, guard, NULL) == 0);
  replace_during_cancel = 1; voice_result = 0;
  assert(product_cancel() == 0 && newer != old);
  assert(bk7258_display_cancel_expression(old) == -ESTALE);
  assert(bk7258_display_expression_status(&state) == 0 && state.id == newer && state.state == BKDISPLAY_EXPRESSION_PENDING);
  cancel_during_render = 1;
  assert(bkdisplay_intent_step(&service, true) && renders == 1);
  assert(bk7258_display_expression_status(&state) == 0 && state.state == BKDISPLAY_EXPRESSION_DONE);
  assert(bk7258_display_cancel_expression(newer) == -EALREADY);
  assert(bk7258_display_cancel_expression(0) == -EINVAL);
  replace_during_cancel = 0; voice_result = -ENOENT;
  assert(product_cancel() == -ENOENT);
  puts("CONTRACT_PASS");
  return 0;
}
