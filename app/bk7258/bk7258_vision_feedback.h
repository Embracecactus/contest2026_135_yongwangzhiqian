/****************************************************************************
 * app/bk7258/bk7258_vision_feedback.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Best-effort display feedback for an explicit Shaniu snapshot.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VISION_FEEDBACK_H
#define __APP_BK7258_BK7258_VISION_FEEDBACK_H

#include <stdbool.h>
#include <stdint.h>

#define BKVISION_FEEDBACK_HOLD_MS 800u

struct bkvision_feedback_ops_s
{
  int (*set_expression)(void *arg, const char *expression, uint64_t *identity);
  int (*replace_expression)(void *arg, uint64_t *identity,
                            const char *replacement);
  void (*wait_ms)(void *arg, unsigned int milliseconds);
  void *arg;
};

struct bkvision_feedback_s
{
  const struct bkvision_feedback_ops_s *ops;
  uint64_t identity;
  bool rendered;
};

void bkvision_feedback_initialize(
  struct bkvision_feedback_s *feedback,
  const struct bkvision_feedback_ops_s *ops);
void bkvision_feedback_snapshot_begin(struct bkvision_feedback_s *feedback);
void bkvision_feedback_snapshot_finish(struct bkvision_feedback_s *feedback,
                                       bool capture_succeeded);

#endif /* __APP_BK7258_BK7258_VISION_FEEDBACK_H */
