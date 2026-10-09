/****************************************************************************
 * app/bk7258/bk7258_vision_feedback.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_vision_feedback.h"

#include <stddef.h>
#include <errno.h>

void bkvision_feedback_initialize(
  struct bkvision_feedback_s *feedback,
  const struct bkvision_feedback_ops_s *ops)
{
  if (feedback != NULL)
    {
      feedback->ops = ops;
      feedback->identity = 0;
      feedback->rendered = false;
    }
}

void bkvision_feedback_snapshot_begin(struct bkvision_feedback_s *feedback)
{
  if (feedback == NULL || feedback->ops == NULL ||
      feedback->ops->set_expression == NULL)
    {
      return;
    }

  feedback->identity = 0;
  feedback->rendered = feedback->ops->set_expression(feedback->ops->arg, "thinking",
                                                       &feedback->identity) >= 0;
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

  if (feedback->identity == 0 || feedback->ops->replace_expression == NULL)
    return;
  result_expression = capture_succeeded ? "happy" : "error";
  int ret = feedback->ops->replace_expression(feedback->ops->arg,
                                               &feedback->identity,
                                               result_expression);
  if (ret == -ESTALE || ret == -EOVERFLOW)
    {
      feedback->identity = 0;
      return;
    }
  if (ret >= 0) feedback->rendered = true;
  if (!feedback->rendered) { feedback->identity = 0; return; }
  if (feedback->ops->wait_ms != NULL)
    feedback->ops->wait_ms(feedback->ops->arg, BKVISION_FEEDBACK_HOLD_MS);
  (void)feedback->ops->replace_expression(feedback->ops->arg,
                                          &feedback->identity, "neutral");
  feedback->identity = 0;
}
