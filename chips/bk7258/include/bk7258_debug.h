/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/bk7258_debug.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DEBUG_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DEBUG_H

#include <nuttx/config.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
#  define BK7258_SWD_TRACE_ADDRESS 0x2804f800u
#  define BK7258_SWD_TRACE_MAGIC   0x53574454u /* "SWDT" */
#  define BK7258_SWD_TRACE_VERSION 2u
#  define BK7258_SWD_TRACE_SAMPLES 16u

enum bk7258_swd_trace_stage_e
{
  BK7258_SWD_TRACE_CP_ENTRY = 0x100u,
  BK7258_SWD_TRACE_CP_CORE_READY,
  BK7258_SWD_TRACE_CP_C_RUNTIME_READY,
  BK7258_SWD_TRACE_CP_BEFORE_NX_START,
  BK7258_SWD_TRACE_BOARD_LATE_ENTRY = 0x200u,
  BK7258_SWD_TRACE_BOARD_LATE_AFTER_SDK,
  BK7258_SWD_TRACE_BOARD_LATE_AFTER_SWD,
  BK7258_SWD_TRACE_BOARD_LATE_EXIT,
  BK7258_SWD_TRACE_SDK_BEFORE_SYS = 0x300u,
  BK7258_SWD_TRACE_SDK_AFTER_SYS,
  BK7258_SWD_TRACE_SDK_AFTER_IPC,
  BK7258_SWD_TRACE_SDK_AFTER_MB_IPC,
  BK7258_SWD_TRACE_SDK_AFTER_BK_IPC,
  BK7258_SWD_TRACE_SDK_EXIT
};

struct bk7258_swd_trace_sample_s
{
  uint32_t stage;
  uint32_t sequence;
  uint32_t dhcsr;
  uint32_t demcr;
  uint32_t dauthctrl;
  uint32_t sys_debug0;
  uint32_t sys_debug1;
  uint32_t cpu_route;
  uint32_t gpio_function;
  uint32_t gpio_clk_ctrl;
  uint32_t gpio_io_ctrl;
  uint32_t vtor;
  uint32_t primask;
  uint32_t caller;
};

struct bk7258_swd_trace_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t boot_count;
  uint32_t count;
  uint32_t capacity;
  uint32_t sample_words;
  uint32_t reserved[2];
  struct bk7258_swd_trace_sample_s sample[BK7258_SWD_TRACE_SAMPLES];
};

int bk7258_swd_initialize(void);
void bk7258_swd_maintain(void);
void bk7258_swd_trace_begin(void);
void bk7258_swd_trace_snapshot(uint32_t stage);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DEBUG_H */
