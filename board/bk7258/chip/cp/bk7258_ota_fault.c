/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_ota_fault.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX serialization wrapper for the portable N15-V failpoint plan.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_ota_fault.h>

#include "bk7258_ota_fault_core.h"

static struct bk7258_ota_fault_plan_s g_bk7258_ota_fault_plan;
static spinlock_t g_bk7258_ota_fault_lock = SP_UNLOCKED;
static bool g_bk7258_ota_fault_initialized;

int bk7258_ota_fault_initialize(void)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_bk7258_ota_fault_lock);
  bk7258_ota_fault_core_initialize(&g_bk7258_ota_fault_plan);
  g_bk7258_ota_fault_initialized = true;
  spin_unlock_irqrestore(&g_bk7258_ota_fault_lock, flags);
  return 0;
}

int bk7258_ota_fault_arm(enum bk7258_ota_fault_point_e point,
                         uint32_t ordinal, uint64_t generation)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_bk7258_ota_fault_lock);
  ret = g_bk7258_ota_fault_initialized ?
        bk7258_ota_fault_core_arm(&g_bk7258_ota_fault_plan, point,
                                  ordinal, generation) : -EAGAIN;
  spin_unlock_irqrestore(&g_bk7258_ota_fault_lock, flags);
  return ret;
}

int bk7258_ota_fault_begin(uint64_t generation, uint32_t allowed_mask)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_bk7258_ota_fault_lock);
  ret = g_bk7258_ota_fault_initialized ?
        bk7258_ota_fault_core_begin(&g_bk7258_ota_fault_plan, generation,
                                    allowed_mask) : -EAGAIN;
  spin_unlock_irqrestore(&g_bk7258_ota_fault_lock, flags);
  return ret;
}

int bk7258_ota_fault_before(enum bk7258_ota_fault_point_e point)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_bk7258_ota_fault_lock);
  ret = g_bk7258_ota_fault_initialized ?
        bk7258_ota_fault_core_before(&g_bk7258_ota_fault_plan, point) :
        -EAGAIN;
  spin_unlock_irqrestore(&g_bk7258_ota_fault_lock, flags);
  return ret;
}

int bk7258_ota_fault_get_status(struct bk7258_ota_fault_status_s *status)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_bk7258_ota_fault_lock);
  ret = g_bk7258_ota_fault_initialized ?
        bk7258_ota_fault_core_snapshot(&g_bk7258_ota_fault_plan, status) :
        -EAGAIN;
  spin_unlock_irqrestore(&g_bk7258_ota_fault_lock, flags);
  return ret;
}

int bk7258_ota_fault_finish(struct bk7258_ota_fault_status_s *status)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_bk7258_ota_fault_lock);
  ret = g_bk7258_ota_fault_initialized ?
        bk7258_ota_fault_core_finish(&g_bk7258_ota_fault_plan, status) :
        -EAGAIN;
  spin_unlock_irqrestore(&g_bk7258_ota_fault_lock, flags);
  return ret;
}
