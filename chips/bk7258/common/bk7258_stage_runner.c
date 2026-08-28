/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/common/
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
          (stage->flags & ~BK7258_STAGE_FLAG_ALWAYS_RUN) != 0 ||
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_stage_runner_run(FAR struct bk7258_stage_runner_s *runner,
                            FAR void *context)
{
  bool mandatory_failed = false;
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

  ret = bk7258_stage_runner_validate(runner);
  if (ret < 0)
    {
      nxmutex_unlock(&runner->lock);
      return ret;
    }

  runner->succeeded_mask = 0;
  runner->failed_mask = 0;
  runner->terminal_result = 0;
  runner->first_error_stage = BK7258_STAGE_ID_INVALID;

  runner->state = BK7258_PLATFORM_RUNNING;

  for (i = 0; i < runner->stage_count; i++)
    {
      FAR const struct bk7258_stage_desc_s *stage = &runner->stages[i];
      uint32_t bit = UINT32_C(1) << stage->id;

      if ((stage->requires_mask & runner->succeeded_mask) !=
            stage->requires_mask ||
          (mandatory_failed &&
           (stage->flags & BK7258_STAGE_FLAG_ALWAYS_RUN) == 0))
        {
          continue;
        }

      ret = runner->execute(context, stage);
      if (ret < 0)
        {
          _err("bk7258: platform stage %u failed: %d\n",
               (unsigned int)stage->id, ret);
          runner->failed_mask |= bit;

          if (stage->stage_class == BK7258_STAGE_MANDATORY)
            {
              mandatory_failed = true;
              if (runner->terminal_result >= 0)
                {
                  runner->terminal_result = ret;
                  runner->first_error_stage = stage->id;
                }
            }
        }
      else
        {
          runner->succeeded_mask |= bit;
        }
    }

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
