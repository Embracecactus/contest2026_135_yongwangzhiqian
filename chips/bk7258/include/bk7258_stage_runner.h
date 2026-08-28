/****************************************************************************
 * chips/bk7258/include/bk7258_stage_runner.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic, one-shot initialization-stage runner for BK7258 roles.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STAGE_RUNNER_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STAGE_RUNNER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/compiler.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_platform.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_STAGE_ID_LIMIT        32
#define BK7258_STAGE_ID_INVALID      UINT8_MAX
#define BK7258_STAGE_FLAG_ALWAYS_RUN (1u << 0)
#define BK7258_STAGE_FLAG_EXTERNAL   (1u << 1)

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_stage_class_e
{
  BK7258_STAGE_MANDATORY = 0,
  BK7258_STAGE_BEST_EFFORT
};

struct bk7258_stage_desc_s;

typedef int (*bk7258_stage_execute_t)(
  FAR void *context, FAR const struct bk7258_stage_desc_s *stage);

struct bk7258_stage_desc_s
{
  uint32_t requires_mask;
  uint8_t id;
  uint8_t stage_class;
  uint8_t flags;
};

struct bk7258_stage_runner_s
{
  mutex_t lock;
  FAR const struct bk7258_stage_desc_s *stages;
  bk7258_stage_execute_t execute;
  uint32_t stage_mask;
  uint32_t succeeded_mask;
  uint32_t failed_mask;
  int terminal_result;
  uint8_t stage_count;
  uint8_t state;
  uint8_t first_error_stage;
  uint8_t next_stage;
  uint8_t waiting_stage;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Run every eligible stage exactly once.  A mandatory failure skips later
 * ordinary stages.  BK7258_STAGE_FLAG_ALWAYS_RUN stages may cross that
 * failure only when every stage in requires_mask has succeeded; this keeps
 * recovery/finalization work independent without bypassing a real hardware
 * or storage prerequisite.  Required stages must be unique earlier entries
 * in the same table.  The first mandatory failure is cached permanently and
 * returned to every later caller.  The runner lock must be initialized by
 * its owner before the first call.  Stage callbacks must not re-enter the
 * same runner.
 */

int bk7258_stage_runner_run(FAR struct bk7258_stage_runner_s *runner,
                            FAR void *context);

/* Run normal stages up to the next external checkpoint.  The requested ID
 * must be the next descriptor carrying BK7258_STAGE_FLAG_EXTERNAL.  When its
 * prerequisites and mandatory-failure policy permit execution, *eligible is
 * true and the runner pauses until bk7258_stage_runner_complete_external().
 * An ineligible checkpoint is skipped and does not require completion.
 * Initialization failures are cached but are not returned here, so callers
 * cannot accidentally suppress later ALWAYS_RUN work.
 */

int bk7258_stage_runner_run_until(
  FAR struct bk7258_stage_runner_s *runner,
  FAR void *context, uint8_t external_stage,
  FAR bool *eligible);

/* Record one paused external checkpoint through the same first-error and
 * mask logic as an ordinary stage.  A negative stage result is cached; the
 * function itself returns only checkpoint-protocol errors.
 */

int bk7258_stage_runner_complete_external(
  FAR struct bk7258_stage_runner_s *runner,
  uint8_t external_stage, int stage_result);

/* Run the remaining ordinary stages and seal the one-shot result.  This
 * rejects an unhandled external checkpoint without executing past it.
 */

int bk7258_stage_runner_finish(
  FAR struct bk7258_stage_runner_s *runner,
  FAR void *context);

/* Return the cached terminal result, or -EAGAIN until the run has completed.
 * This function waits for a currently executing run by taking the same lock.
 */

int bk7258_stage_runner_result(FAR struct bk7258_stage_runner_s *runner);

/* Take a lock-consistent copy suitable for health reporting. */

int bk7258_stage_runner_snapshot(
  FAR struct bk7258_stage_runner_s *runner,
  FAR struct bk7258_platform_status_s *status);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_STAGE_RUNNER_H */
