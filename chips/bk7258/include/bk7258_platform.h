/****************************************************************************
 * chips/bk7258/include/bk7258_platform.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Common BK7258 role-platform health record.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PLATFORM_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PLATFORM_H

#include <stdint.h>

enum bk7258_platform_state_e
{
  BK7258_PLATFORM_NEW = 0,
  BK7258_PLATFORM_RUNNING,
  BK7258_PLATFORM_PAUSED,
  BK7258_PLATFORM_DONE
};

struct bk7258_platform_status_s
{
  uint32_t attempted_mask;
  uint32_t succeeded_mask;
  uint32_t failed_mask;
  uint32_t skipped_mask;
  int terminal_result;
  uint8_t state;
  uint8_t first_error_stage;
};

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PLATFORM_H */
