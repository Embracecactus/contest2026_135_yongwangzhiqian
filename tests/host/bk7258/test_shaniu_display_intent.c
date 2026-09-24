/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_display_service.h"
struct bkdisplay_service_s { int unused; };
static int renders, render_error;
static char rendered[BKDISPLAY_EXPRESSION_SIZE];
static int bkdisplay_render_locked(struct bkdisplay_service_s *service, const char *expression)
{
  (void)service;
  struct bkdisplay_expression_request_s state;
  assert(bk7258_display_expression_status(&state) == 0);
  assert(state.state == BKDISPLAY_EXPRESSION_RUNNING);
  uint32_t rejected = 0;
  assert(bk7258_display_request_expression("sad", &rejected) == -EBUSY);
  renders++; strcpy(rendered, expression);
  return render_error;
}
#include "bk7258_display_intent.inc"
int main(void)
{
  struct bkdisplay_service_s service = {0};
  struct bkdisplay_expression_request_s state;
  uint32_t id = 0, rejected = 0;
  assert(bk7258_display_request_expression("happy", &id) == -EBUSY);
  bkdisplay_intent_gate(true);
  assert(bk7258_display_request_expression("../invalid", &id) == -EINVAL);
  assert(bk7258_display_request_expression("happy", NULL) == -EINVAL);
  assert(bk7258_display_request_expression("happy", &id) == 0 && id != 0);
  assert(renders == 0 && bkdisplay_intent_pending());
  assert(bk7258_display_expression_status(&state) == 0);
  assert(state.id == id && state.state == BKDISPLAY_EXPRESSION_PENDING);
  assert(bk7258_display_request_expression("sad", &rejected) == -EBUSY);
  assert(bkdisplay_intent_step(&service, true));
  assert(renders == 1 && !strcmp(rendered, "happy"));
  assert(bk7258_display_expression_status(&state) == 0);
  assert(state.id == id && state.state == BKDISPLAY_EXPRESSION_DONE && state.error == 0);
  assert(!bkdisplay_intent_step(&service, true) && renders == 1 && !bkdisplay_intent_pending());
  bkdisplay_intent_gate(false);
  assert(bk7258_display_expression_status(&state) == 0 && state.state == BKDISPLAY_EXPRESSION_DONE);
  bkdisplay_intent_gate(true);
  assert(bk7258_display_request_expression("sad", &rejected) == 0 && rejected > id);
  bkdisplay_intent_gate(false);
  assert(!bkdisplay_intent_step(&service, true) && renders == 1);
  assert(bk7258_display_expression_status(&state) == 0 && state.state == BKDISPLAY_EXPRESSION_CANCELED);
  bkdisplay_intent_gate(true);
  assert(bk7258_display_request_expression("shy", &id) == 0);
  render_error = -EIO;
  assert(bkdisplay_intent_step(&service, true));
  assert(bk7258_display_expression_status(&state) == 0 && state.state == BKDISPLAY_EXPRESSION_FAILED && state.error == -EIO);
  assert(bk7258_display_request_expression("neutral", &id) == 0);
  int before = renders;
  assert(bkdisplay_intent_step(&service, false));
  assert(renders == before);
  assert(bk7258_display_expression_status(&state) == 0 && state.state == BKDISPLAY_EXPRESSION_FAILED && state.error == -ENODEV);
  assert(bk7258_display_request_expression("happy", &id) == 0);
  bkdisplay_intent_supersede();
  assert(!bkdisplay_intent_step(&service, true) && renders == before);
  assert(bk7258_display_expression_status(&state) == 0 && state.state == BKDISPLAY_EXPRESSION_CANCELED);
  g_bkdisplay_intent.id = UINT32_MAX; /* Arrange the public ID exhaustion boundary. */
  assert(bk7258_display_request_expression("happy", &id) == -EOVERFLOW);
  assert(renders == before);
  puts("CONTRACT_PASS");
  return 0;
}
