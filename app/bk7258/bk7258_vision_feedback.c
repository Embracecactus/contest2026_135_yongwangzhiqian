/****************************************************************************
 * app/bk7258/bk7258_vision_feedback.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_vision_feedback.h"

#include <stddef.h>

void bkvision_feedback_initialize(
  struct bkvision_feedback_s *feedback,
  const struct bkvision_feedback_ops_s *ops)
{
  if (feedback != NULL)
    {
      feedback->ops = ops;
      feedback->rendered_expression = NULL;
    }
}

void bkvision_feedback_snapshot_begin(struct bkvision_feedback_s *feedback)
{
  if (feedback == NULL || feedback->ops == NULL ||
      feedback->ops->set_expression == NULL)
    {
      return;
    }

  if (feedback->ops->set_expression(feedback->ops->arg, "thinking") >= 0)
    {
      feedback->rendered_expression = "thinking";
    }
}

void bkvision_feedback_snapshot_finish(struct bkvision_feedback_s *feedback,
                                       bool capture_succeeded)
{
  const char *result_expression;

  if (feedback == NULL || feedback->ops == NULL ||
      feedback->ops->set_expression == NULL)
    {
      return;
    }

  result_expression = capture_succeeded ? "happy" : "error";
  if (feedback->ops->set_expression(feedback->ops->arg,
                                    result_expression) >= 0)
    {
      feedback->rendered_expression = result_expression;
    }

  if (feedback->rendered_expression == NULL)
    {
      return;
    }

  if (feedback->ops->wait_ms != NULL)
    {
      feedback->ops->wait_ms(feedback->ops->arg,
                             BKVISION_FEEDBACK_HOLD_MS);
    }

  if (feedback->ops->replace_expression != NULL)
    {
      (void)feedback->ops->replace_expression(
        feedback->ops->arg, feedback->rendered_expression, "neutral");
    }

  feedback->rendered_expression = NULL;
}
