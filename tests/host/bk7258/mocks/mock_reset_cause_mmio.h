/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __TEST_BK7258_RESET_CAUSE_MMIO_H
#define __TEST_BK7258_RESET_CAUSE_MMIO_H

#include <stdint.h>

extern volatile uint32_t g_bk7258_test_reset_reg;

#define BK7258_AON_PMU_R7A_ADDR \
  ((uintptr_t)&g_bk7258_test_reset_reg)

#endif
