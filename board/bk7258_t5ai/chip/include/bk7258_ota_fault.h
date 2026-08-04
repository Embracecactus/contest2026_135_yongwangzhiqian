/****************************************************************************
 * arch/arm/include/bk7258/bk7258_ota_fault.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Validation-only N15-V deterministic fault-injection interface.
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_FAULT_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_FAULT_H

#include <stdbool.h>
#include <stdint.h>

enum bk7258_ota_fault_point_e
{
  BK7258_OTA_FAULT_NONE = 0,
  BK7258_OTA_FAULT_STAGE_ERASE,
  BK7258_OTA_FAULT_STAGE_WRITE,
  BK7258_OTA_FAULT_STAGE_READ,
  BK7258_OTA_FAULT_PUBLISH_READ,
  BK7258_OTA_FAULT_PUBLISH_ERASE,
  BK7258_OTA_FAULT_PUBLISH_WRITE,
  BK7258_OTA_FAULT_TRIAL_READ,
  BK7258_OTA_FAULT_TRIAL_WRITE,
  BK7258_OTA_FAULT_POINT_COUNT
};

#define BK7258_OTA_FAULT_BIT(point) (1u << (uint32_t)(point))

#define BK7258_OTA_FAULT_STAGE_MASK \
  (BK7258_OTA_FAULT_BIT(BK7258_OTA_FAULT_STAGE_ERASE) | \
   BK7258_OTA_FAULT_BIT(BK7258_OTA_FAULT_STAGE_WRITE) | \
   BK7258_OTA_FAULT_BIT(BK7258_OTA_FAULT_STAGE_READ))

#define BK7258_OTA_FAULT_PUBLISH_MASK \
  (BK7258_OTA_FAULT_BIT(BK7258_OTA_FAULT_PUBLISH_READ) | \
   BK7258_OTA_FAULT_BIT(BK7258_OTA_FAULT_PUBLISH_ERASE) | \
   BK7258_OTA_FAULT_BIT(BK7258_OTA_FAULT_PUBLISH_WRITE))

#define BK7258_OTA_FAULT_TRIAL_MASK \
  (BK7258_OTA_FAULT_BIT(BK7258_OTA_FAULT_TRIAL_READ) | \
   BK7258_OTA_FAULT_BIT(BK7258_OTA_FAULT_TRIAL_WRITE))

#define BK7258_OTA_FAULT_MAX_ORDINAL 65535u

struct bk7258_ota_fault_status_s
{
  uint64_t generation;
  uint32_t point;
  uint32_t ordinal;
  uint32_t seen;
  bool configured;
  bool active;
  bool triggered;
};

int bk7258_ota_fault_initialize(void);
int bk7258_ota_fault_arm(enum bk7258_ota_fault_point_e point,
                         uint32_t ordinal, uint64_t generation);
int bk7258_ota_fault_begin(uint64_t generation, uint32_t allowed_mask);
int bk7258_ota_fault_before(enum bk7258_ota_fault_point_e point);
int bk7258_ota_fault_get_status(struct bk7258_ota_fault_status_s *status);
int bk7258_ota_fault_finish(struct bk7258_ota_fault_status_s *status);

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_FAULT_H */
