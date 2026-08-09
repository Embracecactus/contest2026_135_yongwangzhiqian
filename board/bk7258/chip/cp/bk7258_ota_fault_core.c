/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_ota_fault_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * No NuttX, SDK, MMIO, heap or libc dependency is permitted here.
 ****************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bk7258_ota_fault_core.h"

static bool valid_point(enum bk7258_ota_fault_point_e point)
{
  return point > BK7258_OTA_FAULT_NONE &&
         point < BK7258_OTA_FAULT_POINT_COUNT;
}

void bk7258_ota_fault_core_initialize(
  struct bk7258_ota_fault_plan_s *plan)
{
  if (plan != NULL)
    {
      plan->status.generation = 0;
      plan->status.point = BK7258_OTA_FAULT_NONE;
      plan->status.ordinal = 0;
      plan->status.seen = 0;
      plan->status.configured = false;
      plan->status.active = false;
      plan->status.triggered = false;
    }
}

int bk7258_ota_fault_core_arm(struct bk7258_ota_fault_plan_s *plan,
                              enum bk7258_ota_fault_point_e point,
                              uint32_t ordinal, uint64_t generation)
{
  if (plan == NULL || !valid_point(point) || ordinal == 0 ||
      ordinal > BK7258_OTA_FAULT_MAX_ORDINAL || generation == 0)
    {
      return -EINVAL;
    }

  if (plan->status.configured || plan->status.active)
    {
      return -EBUSY;
    }

  plan->status.generation = generation;
  plan->status.point = (uint32_t)point;
  plan->status.ordinal = ordinal;
  plan->status.seen = 0;
  plan->status.configured = true;
  plan->status.active = false;
  plan->status.triggered = false;
  return 0;
}

int bk7258_ota_fault_core_begin(struct bk7258_ota_fault_plan_s *plan,
                                uint64_t generation,
                                uint32_t allowed_mask)
{
  uint32_t point_bit;

  if (plan == NULL || generation == 0 || allowed_mask == 0)
    {
      return -EINVAL;
    }

  if (!plan->status.configured)
    {
      return 0;
    }

  if (plan->status.active)
    {
      return -EBUSY;
    }

  if (plan->status.generation != generation)
    {
      return -ESTALE;
    }

  point_bit = BK7258_OTA_FAULT_BIT(plan->status.point);
  if ((allowed_mask & point_bit) == 0)
    {
      return -EPERM;
    }

  plan->status.seen = 0;
  plan->status.triggered = false;
  plan->status.active = true;
  return 0;
}

int bk7258_ota_fault_core_before(struct bk7258_ota_fault_plan_s *plan,
                                 enum bk7258_ota_fault_point_e point)
{
  if (plan == NULL || !valid_point(point))
    {
      return -EINVAL;
    }

  if (!plan->status.active || plan->status.point != (uint32_t)point)
    {
      return 0;
    }

  if (plan->status.seen == UINT32_MAX)
    {
      plan->status.active = false;
      return -EOVERFLOW;
    }

  plan->status.seen++;
  if (plan->status.seen != plan->status.ordinal)
    {
      return 0;
    }

  plan->status.triggered = true;
  plan->status.active = false;
  return -ECANCELED;
}

int bk7258_ota_fault_core_snapshot(
  const struct bk7258_ota_fault_plan_s *plan,
  struct bk7258_ota_fault_status_s *status)
{
  if (plan == NULL || status == NULL)
    {
      return -EINVAL;
    }

  *status = plan->status;
  return 0;
}

int bk7258_ota_fault_core_finish(struct bk7258_ota_fault_plan_s *plan,
                                 struct bk7258_ota_fault_status_s *status)
{
  int ret;

  if (plan == NULL)
    {
      return -EINVAL;
    }

  if (status != NULL)
    {
      ret = bk7258_ota_fault_core_snapshot(plan, status);
      if (ret < 0)
        {
          return ret;
        }
    }

  bk7258_ota_fault_core_initialize(plan);
  return 0;
}
