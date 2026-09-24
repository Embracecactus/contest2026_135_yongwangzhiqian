/* SPDX-License-Identifier: Apache-2.0 */
/* Real display commands and vision feedback; only render/OS are substitutes.
 * The Make target extracts the command bodies verbatim from production. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_display_service.h"
#include "bk7258_vision_feedback.h"
struct bkdisplay_service_s {
  pthread_mutex_t lock;
  struct bkdisplay_service_status_s status;
};
static struct bkdisplay_service_s g_bkdisplay_service = { .lock = PTHREAD_MUTEX_INITIALIZER };
#define nxmutex_lock pthread_mutex_lock
static void bkdisplay_unlock(struct bkdisplay_service_s *service)
{ assert(pthread_mutex_unlock(&service->lock) == 0); }
static int render_error, renders;
static int bkdisplay_render_pixels_locked(struct bkdisplay_service_s *service, const char *expression)
{ renders++; if (render_error) return render_error;
  strcpy(service->status.expression, expression); return 0; }
#include "bk7258_display_render_identity.inc"
#include "bk7258_display_intent.inc"
#include "display-commands.inc"
static int set(void *unused, const char *expression, uint64_t *identity)
{ (void)unused; return bk7258_display_set_expression_owned(expression, identity); }
static int replace(void *unused, uint64_t *identity, const char *replacement)
{ (void)unused; return bk7258_display_replace_expression(identity, replacement); }
static bool newer_selection;
static void wait_ms(void *unused, unsigned int duration)
{
  (void)unused;
  assert(duration == BKVISION_FEEDBACK_HOLD_MS);
  if (newer_selection) assert(bk7258_display_set_expression("happy") == 0);
}
int main(void)
{
  const struct bkvision_feedback_ops_s ops = {
    .set_expression = set, .replace_expression = replace, .wait_ms = wait_ms
  };
  struct bkvision_feedback_s feedback;
  bkdisplay_intent_gate(true);
  bkdisplay_intent_supersede();
  assert(!bkdisplay_intent_pending());
  assert(!bkdisplay_intent_step(&g_bkdisplay_service, true));
  bkvision_feedback_initialize(&feedback, &ops);
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(strcmp(g_bkdisplay_service.status.expression, "neutral") == 0);
  newer_selection = true;
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(strcmp(g_bkdisplay_service.status.expression, "happy") == 0);
  newer_selection = false;
  bkvision_feedback_snapshot_begin(&feedback);
  assert(bk7258_display_set_expression("thinking") == 0);
  int before = renders;
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(renders == before && !strcmp(g_bkdisplay_service.status.expression, "thinking"));

  bkvision_feedback_snapshot_begin(&feedback);
  uint32_t request = 0;
  assert(bk7258_display_request_expression("sad", &request) == 0);
  before = renders;
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(renders == before);
  assert(bkdisplay_intent_step(&g_bkdisplay_service, true));
  assert(!strcmp(g_bkdisplay_service.status.expression, "sad"));

  render_error = -EIO;
  bkvision_feedback_snapshot_begin(&feedback);
  render_error = 0;
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(!strcmp(g_bkdisplay_service.status.expression, "neutral"));

  uint64_t identity = 0;
  assert(bk7258_display_set_expression_owned("happy", &identity) == 0);
  assert(identity != 0);
  assert(bkdisplay_identity_advance() == 0); /* Overlay invalidation boundary. */
  before = renders;
  assert(bk7258_display_replace_expression(&identity, "neutral") == -ESTALE);
  assert(renders == before);
  assert(bk7258_display_set_expression_owned("happy", NULL) == -EINVAL);
  g_bkdisplay_expression_identity = UINT64_MAX;
  assert(bk7258_display_set_expression_owned("happy", &identity) == -EOVERFLOW);
  assert(identity == 0 && renders == before);
  puts("CONTRACT_PASS expression ownership");
  return 0;
}
