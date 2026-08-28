/****************************************************************************
 * tests/bk7258/mocks/arch/chip/bk7258_debug.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __TESTS_BK7258_MOCKS_ARCH_CHIP_BK7258_DEBUG_H
#define __TESTS_BK7258_MOCKS_ARCH_CHIP_BK7258_DEBUG_H

#include <stdint.h>

enum bk7258_swd_trace_stage_e
{
  BK7258_SWD_TRACE_BOARD_LATE_ENTRY = 0x200u,
  BK7258_SWD_TRACE_BOARD_LATE_AFTER_SDK,
  BK7258_SWD_TRACE_BOARD_LATE_AFTER_SWD,
  BK7258_SWD_TRACE_BOARD_LATE_EXIT
};

void bk7258_swd_trace_snapshot(uint32_t stage);
int bk7258_swd_initialize(void);

#endif /* __TESTS_BK7258_MOCKS_ARCH_CHIP_BK7258_DEBUG_H */
