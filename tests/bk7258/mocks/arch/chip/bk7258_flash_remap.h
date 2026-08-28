#ifndef __TEST_BK7258_FLASH_REMAP_H
#define __TEST_BK7258_FLASH_REMAP_H

#include <stdint.h>

extern volatile uint32_t g_bk7258_test_remap_regs[4];

#define BK7258_FLASH_REMAP_BEGIN_REG \
  ((uintptr_t)&g_bk7258_test_remap_regs[0])
#define BK7258_FLASH_REMAP_END_REG \
  ((uintptr_t)&g_bk7258_test_remap_regs[1])
#define BK7258_FLASH_REMAP_OFFSET_REG \
  ((uintptr_t)&g_bk7258_test_remap_regs[2])
#define BK7258_FLASH_REMAP_ENABLE_REG \
  ((uintptr_t)&g_bk7258_test_remap_regs[3])
#define BK7258_FLASH_REMAP_ENABLE_BIT 1u

#endif
