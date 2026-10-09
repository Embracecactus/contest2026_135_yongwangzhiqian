/****************************************************************************
 * Host tests for snapshot-to-display product feedback.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_vision_feedback.h"

struct fixture_s
{
  const char *expressions[4];
  unsigned int expression_count;
  unsigned int waits;
  unsigned int waited_ms;
  int results[4];
  const char *current_expression;
  const char *replace_expected;
  const char *replace_value;
  const char *expression_during_wait;
  unsigned int replace_count;
  uint64_t identity;
  uint64_t replace_identity;
};

static int set_expression(void *arg, const char *expression, uint64_t *identity)
{
  struct fixture_s *fixture = arg;
  unsigned int index = fixture->expression_count++;

  assert(index < 4);
  fixture->expressions[index] = expression;
  *identity = ++fixture->identity;
  if (fixture->results[index] >= 0)
    {
      fixture->current_expression = expression;
    }

  return fixture->results[index];
}

static int replace_expression(void *arg, uint64_t *identity,
                              const char *replacement)
{
  struct fixture_s *fixture = arg;

  fixture->replace_count++;
  fixture->replace_identity = *identity;
  fixture->replace_expected = fixture->current_expression;
  fixture->replace_value = replacement;
  if (*identity != fixture->identity)
    {
      return -ESTALE;
    }

  if (strcmp(replacement, "neutral") != 0)
    return set_expression(arg, replacement, identity);
  *identity = ++fixture->identity;
  fixture->current_expression = replacement;
  return 0;
}

static void wait_ms(void *arg, unsigned int milliseconds)
{
  struct fixture_s *fixture = arg;

  fixture->waits++;
  fixture->waited_ms = milliseconds;
  if (fixture->expression_during_wait != NULL)
    {
      fixture->current_expression = fixture->expression_during_wait;
      fixture->identity++;
    }
}

static struct bkvision_feedback_ops_s make_ops(struct fixture_s *fixture)
{
  struct bkvision_feedback_ops_s ops =
  {
    .set_expression = set_expression,
    .replace_expression = replace_expression,
    .wait_ms = wait_ms,
    .arg = fixture,
  };

  return ops;
}

static void test_success_feedback(void)
{
  struct fixture_s fixture = {0};
  struct bkvision_feedback_ops_s ops = make_ops(&fixture);
  struct bkvision_feedback_s feedback;

  bkvision_feedback_initialize(&feedback, &ops);
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(fixture.expression_count == 2);
  assert(strcmp(fixture.expressions[0], "thinking") == 0);
  assert(strcmp(fixture.expressions[1], "happy") == 0);
  assert(fixture.replace_count == 2);
  assert(strcmp(fixture.replace_expected, "happy") == 0);
  assert(strcmp(fixture.replace_value, "neutral") == 0);
  assert(strcmp(fixture.current_expression, "neutral") == 0);
  assert(fixture.waits == 1);
  assert(fixture.waited_ms == BKVISION_FEEDBACK_HOLD_MS);
}

static void test_failure_feedback(void)
{
  struct fixture_s fixture = {0};
  struct bkvision_feedback_ops_s ops = make_ops(&fixture);
  struct bkvision_feedback_s feedback;

  bkvision_feedback_initialize(&feedback, &ops);
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, false);
  assert(fixture.expression_count == 2);
  assert(strcmp(fixture.expressions[0], "thinking") == 0);
  assert(strcmp(fixture.expressions[1], "error") == 0);
  assert(fixture.replace_count == 2);
  assert(strcmp(fixture.replace_expected, "error") == 0);
  assert(strcmp(fixture.current_expression, "neutral") == 0);
  assert(fixture.waits == 1);
}

static void test_unavailable_display_does_not_wait(void)
{
  struct fixture_s fixture = {.results = {-ENODEV, -ENODEV}};
  struct bkvision_feedback_ops_s ops = make_ops(&fixture);
  struct bkvision_feedback_s feedback;

  bkvision_feedback_initialize(&feedback, &ops);
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(fixture.expression_count == 2);
  assert(fixture.waits == 0);
  assert(fixture.replace_count == 1);
}

static void test_result_can_recover_after_thinking_failure(void)
{
  struct fixture_s fixture = {.results = {-EAGAIN, 0}};
  struct bkvision_feedback_ops_s ops = make_ops(&fixture);
  struct bkvision_feedback_s feedback;

  bkvision_feedback_initialize(&feedback, &ops);
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(fixture.expression_count == 2);
  assert(fixture.waits == 1);
  assert(fixture.replace_count == 2);
  assert(strcmp(fixture.replace_expected, "happy") == 0);
  assert(strcmp(fixture.current_expression, "neutral") == 0);
}

static void test_newer_expression_is_not_overwritten(void)
{
  struct fixture_s fixture = {.expression_during_wait = "speaking"};
  struct bkvision_feedback_ops_s ops = make_ops(&fixture);
  struct bkvision_feedback_s feedback;

  bkvision_feedback_initialize(&feedback, &ops);
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, true);
  assert(fixture.waits == 1);
  assert(fixture.replace_count == 2);
  assert(fixture.replace_identity != fixture.identity);
  assert(strcmp(fixture.current_expression, "speaking") == 0);
}

static void test_missing_callbacks_are_noops(void)
{
  struct bkvision_feedback_s feedback;
  struct bkvision_feedback_ops_s ops = {0};

  bkvision_feedback_initialize(&feedback, NULL);
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, false);
  bkvision_feedback_initialize(&feedback, &ops);
  bkvision_feedback_snapshot_begin(&feedback);
  bkvision_feedback_snapshot_finish(&feedback, true);
}

int main(void)
{
  test_success_feedback();
  test_failure_feedback();
  test_unavailable_display_does_not_wait();
  test_result_can_recover_after_thinking_failure();
  test_newer_expression_is_not_overwritten();
  test_missing_callbacks_are_noops();
  puts("BKVISION_FEEDBACK_HOST_PASS");
  return 0;
}
