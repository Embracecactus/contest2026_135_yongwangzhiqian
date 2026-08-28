/****************************************************************************
 * chips/bk7258/include/bk7258_reset_cause.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RESET_CAUSE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RESET_CAUSE_H

#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

/* Stable BK7258/SDK reset-source ABI values.  Board code maps these raw SoC
 * reasons to the generic BOARDIOC reset-cause namespace.
 */

enum bk7258_reset_source_e
{
  BK7258_RESET_SOURCE_POWERON = 0x00,
  BK7258_RESET_SOURCE_REBOOT = 0x01,
  BK7258_RESET_SOURCE_WATCHDOG = 0x02,
  BK7258_RESET_SOURCE_NMI_WDT = 0x10
};

struct bk7258_reset_cause_raw_s
{
  uint32_t source;
  bool from_persistent_flag;
};

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_reset_cause_read(FAR struct bk7258_reset_cause_raw_s *raw);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RESET_CAUSE_H */
