/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_ota_fault_core.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable state machine for validation-only deterministic OTA failpoints.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_OTA_FAULT_CORE_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_OTA_FAULT_CORE_H

#include <stdint.h>

#include "../include/bk7258_ota_fault.h"

struct bk7258_ota_fault_plan_s
{
  struct bk7258_ota_fault_status_s status;
};

void bk7258_ota_fault_core_initialize(
  struct bk7258_ota_fault_plan_s *plan);
int bk7258_ota_fault_core_arm(struct bk7258_ota_fault_plan_s *plan,
                              enum bk7258_ota_fault_point_e point,
                              uint32_t ordinal, uint64_t generation);
int bk7258_ota_fault_core_begin(struct bk7258_ota_fault_plan_s *plan,
                                uint64_t generation,
                                uint32_t allowed_mask);
int bk7258_ota_fault_core_before(struct bk7258_ota_fault_plan_s *plan,
                                 enum bk7258_ota_fault_point_e point);
int bk7258_ota_fault_core_snapshot(
  const struct bk7258_ota_fault_plan_s *plan,
  struct bk7258_ota_fault_status_s *status);
int bk7258_ota_fault_core_finish(struct bk7258_ota_fault_plan_s *plan,
                                 struct bk7258_ota_fault_status_s *status);

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_OTA_FAULT_CORE_H */
