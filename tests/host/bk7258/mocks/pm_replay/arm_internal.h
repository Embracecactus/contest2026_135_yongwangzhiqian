/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __TEST_PM_REPLAY_ARM_INTERNAL_H
#define __TEST_PM_REPLAY_ARM_INTERNAL_H

#include <stdint.h>

void test_pm_putreg32(uint32_t value, uintptr_t address);
#define putreg32(v, a) test_pm_putreg32((v), (a))

#endif
