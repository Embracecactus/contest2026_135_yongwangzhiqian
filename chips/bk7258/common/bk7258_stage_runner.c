/****************************************************************************
 * chips/bk7258/common/
 * bk7258_stage_runner.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <debug.h>

#include <arch/chip/bk7258_stage_runner.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_stage_runner_validate(
  FAR struct bk7258_stage_runner_s *runner)
{
  uint32_t ids = 0;
  uint8_t i;

  if (runner->stages == NULL || runner->execute == NULL ||
      runner->stage_count == 0 ||
      runner->stage_count > BK7258_STAGE_ID_LIMIT)
    {
      return -EINVAL;
    }

  for (i = 0; i < runner->stage_count; i++)
    {
      FAR const struct bk7258_stage_desc_s *stage = &runner->stages[i];
      uint32_t bit;

      if (stage->id >= BK7258_STAGE_ID_LIMIT ||
          stage->stage_class > BK7258_STAGE_BEST_EFFORT ||
          (stage->flags & ~(BK7258_STAGE_FLAG_ALWAYS_RUN |
                            BK7258_STAGE_FLAG_EXTERNAL)) != 0 ||
          (stage->requires_mask & ~ids) != 0)
        {
          return -EINVAL;
        }

      bit = UINT32_C(1) << stage->id;
      if ((ids & bit) != 0)
        {
          return -EINVAL;
        }

      ids |= bit;
    }

  runner->stage_mask = ids;
  return 0;
}

static int bk7258_stage_runner_prepare(
  FAR struct bk7258_stage_runner_s *runner)
{
  int ret = bk7258_stage_runner_validate(runner);

  if (ret < 0)
    {
      return ret;
    }

  runner->succeeded_mask = 0;
  runner->failed_mask = 0;
  runner->terminal_result = 0;
  runner->first_error_stage = BK7258_STAGE_ID_INVALID;
  runner->next_stage = 0;
  runner->waiting_stage = BK7258_STAGE_ID_INVALID;
  runner->state = BK7258_PLATFORM_RUNNING;
  return 0;
}

static bool bk7258_stage_runner_eligible(
  FAR const struct bk7258_stage_runner_s *runner,
  FAR const struct bk7258_stage_desc_s *stage)
{
  return (stage->requires_mask & runner->succeeded_mask) ==
           stage->requires_mask &&
         (runner->terminal_result >= 0 ||
          (stage->flags & BK7258_STAGE_FLAG_ALWAYS_RUN) != 0);
}

static void bk7258_stage_runner_record(
  FAR struct bk7258_stage_runner_s *runner,
  FAR const struct bk7258_stage_desc_s *stage, int result)
{
  uint32_t bit = UINT32_C(1) << stage->id;

  if (result < 0)
    {
      _err("bk7258: platform stage %u failed: %d\n",
           (unsigned int)stage->id, result);
      runner->failed_mask |= bit;

      if (stage->stage_class == BK7258_STAGE_MANDATORY &&
          runner->terminal_result >= 0)
        {
          runner->terminal_result = result;
          runner->first_error_stage = stage->id;
        }
    }
  else
    {
      runner->succeeded_mask |= bit;
    }
}

static void bk7258_stage_runner_execute_range(
  FAR struct bk7258_stage_runner_s *runner,
  FAR void *context, uint8_t end)
{
  while (runner->next_stage < end)
    {
      FAR const struct bk7258_stage_desc_s *stage =
        &runner->stages[runner->next_stage];

      if (bk7258_stage_runner_eligible(runner, stage))
        {
          int result = runner->execute(context, stage);

          bk7258_stage_runner_record(runner, stage, result);
        }

      runner->next_stage++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_stage_runner_run(FAR struct bk7258_stage_runner_s *runner,
                            FAR void *context)
{
  int lockret;
  int ret;
  uint8_t i;

  if (runner == NULL)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&runner->lock);
  if (lockret < 0)
    {
      return lockret;
    }

  if (runner->state == BK7258_PLATFORM_DONE)
    {
      ret = runner->terminal_result;
      nxmutex_unlock(&runner->lock);
      return ret;
    }

  if (runner->state != BK7258_PLATFORM_NEW)
    {
      nxmutex_unlock(&runner->lock);
      return -EBUSY;
    }

  ret = bk7258_stage_runner_prepare(runner);
  if (ret < 0)
    {
      nxmutex_unlock(&runner->lock);
      return ret;
    }

  for (i = 0; i < runner->stage_count; i++)
    {
      if ((runner->stages[i].flags & BK7258_STAGE_FLAG_EXTERNAL) != 0)
        {
          runner->state = BK7258_PLATFORM_NEW;
          nxmutex_unlock(&runner->lock);
          return -EINVAL;
        }
    }

  bk7258_stage_runner_execute_range(runner, context, runner->stage_count);
  runner->state = BK7258_PLATFORM_DONE;
  ret = runner->terminal_result;
  nxmutex_unlock(&runner->lock);
  return ret;
}

int bk7258_stage_runner_run_until(
  FAR struct bk7258_stage_runner_s *runner,
  FAR void *context, uint8_t external_stage,
  FAR bool *eligible)
{
  FAR const struct bk7258_stage_desc_s *stage;
  int lockret;
  int ret;
  uint8_t target;

  if (runner == NULL || eligible == NULL)
    {
      return -EINVAL;
    }

  *eligible = false;
  lockret = nxmutex_lock(&runner->lock);
  if (lockret < 0)
    {
      return lockret;
    }

  if (runner->state == BK7258_PLATFORM_NEW)
    {
      ret = bk7258_stage_runner_prepare(runner);
      if (ret < 0)
        {
          nxmutex_unlock(&runner->lock);
          return ret;
        }
    }
  else if (runner->state != BK7258_PLATFORM_RUNNING)
    {
      nxmutex_unlock(&runner->lock);
      return runner->state == BK7258_PLATFORM_PAUSED ? -EBUSY : -EALREADY;
    }

  target = runner->next_stage;
  while (target < runner->stage_count &&
         (runner->stages[target].flags & BK7258_STAGE_FLAG_EXTERNAL) == 0)
    {
      target++;
    }

  if (target >= runner->stage_count ||
      runner->stages[target].id != external_stage)
    {
      nxmutex_unlock(&runner->lock);
      return -EPROTO;
    }

  bk7258_stage_runner_execute_range(runner, context, target);
  stage = &runner->stages[target];
  if (bk7258_stage_runner_eligible(runner, stage))
    {
      runner->waiting_stage = stage->id;
      runner->state = BK7258_PLATFORM_PAUSED;
      *eligible = true;
    }
  else
    {
      runner->next_stage++;
    }

  nxmutex_unlock(&runner->lock);
  return 0;
}

int bk7258_stage_runner_complete_external(
  FAR struct bk7258_stage_runner_s *runner,
  uint8_t external_stage, int stage_result)
{
  FAR const struct bk7258_stage_desc_s *stage;
  int lockret;

  if (runner == NULL)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&runner->lock);
  if (lockret < 0)
    {
      return lockret;
    }

  if (runner->state != BK7258_PLATFORM_PAUSED ||
      runner->waiting_stage != external_stage ||
      runner->next_stage >= runner->stage_count)
    {
      nxmutex_unlock(&runner->lock);
      return -EPROTO;
    }

  stage = &runner->stages[runner->next_stage];
  if (stage->id != external_stage ||
      (stage->flags & BK7258_STAGE_FLAG_EXTERNAL) == 0)
    {
      nxmutex_unlock(&runner->lock);
      return -EPROTO;
    }

  bk7258_stage_runner_record(runner, stage, stage_result);
  runner->next_stage++;
  runner->waiting_stage = BK7258_STAGE_ID_INVALID;
  runner->state = BK7258_PLATFORM_RUNNING;
  nxmutex_unlock(&runner->lock);
  return 0;
}

int bk7258_stage_runner_finish(
  FAR struct bk7258_stage_runner_s *runner,
  FAR void *context)
{
  int lockret;
  int ret;
  uint8_t i;

  if (runner == NULL)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&runner->lock);
  if (lockret < 0)
    {
      return lockret;
    }

  if (runner->state == BK7258_PLATFORM_DONE)
    {
      ret = runner->terminal_result;
      nxmutex_unlock(&runner->lock);
      return ret;
    }

  if (runner->state == BK7258_PLATFORM_NEW)
    {
      ret = bk7258_stage_runner_prepare(runner);
      if (ret < 0)
        {
          nxmutex_unlock(&runner->lock);
          return ret;
        }
    }
  else if (runner->state != BK7258_PLATFORM_RUNNING)
    {
      nxmutex_unlock(&runner->lock);
      return -EBUSY;
    }

  for (i = runner->next_stage; i < runner->stage_count; i++)
    {
      if ((runner->stages[i].flags & BK7258_STAGE_FLAG_EXTERNAL) != 0)
        {
          nxmutex_unlock(&runner->lock);
          return -EPROTO;
        }
    }

  bk7258_stage_runner_execute_range(runner, context, runner->stage_count);
  runner->state = BK7258_PLATFORM_DONE;
  ret = runner->terminal_result;
  nxmutex_unlock(&runner->lock);
  return ret;
}

int bk7258_stage_runner_result(FAR struct bk7258_stage_runner_s *runner)
{
  int lockret;
  int ret;

  if (runner == NULL)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&runner->lock);
  if (lockret < 0)
    {
      return lockret;
    }

  if (runner->state == BK7258_PLATFORM_DONE)
    {
      ret = runner->terminal_result;
    }
  else
    {
      ret = -EAGAIN;
    }

  nxmutex_unlock(&runner->lock);
  return ret;
}

int bk7258_stage_runner_snapshot(
  FAR struct bk7258_stage_runner_s *runner,
  FAR struct bk7258_platform_status_s *status)
{
  int lockret;

  if (runner == NULL || status == NULL)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&runner->lock);
  if (lockret < 0)
    {
      return lockret;
    }

  status->attempted_mask = runner->succeeded_mask | runner->failed_mask;
  status->succeeded_mask = runner->succeeded_mask;
  status->failed_mask = runner->failed_mask;
  status->skipped_mask = runner->stage_mask & ~status->attempted_mask;
  status->terminal_result = runner->terminal_result;
  status->state = runner->state;
  status->first_error_stage = runner->first_error_stage;

  nxmutex_unlock(&runner->lock);
  return 0;
}
