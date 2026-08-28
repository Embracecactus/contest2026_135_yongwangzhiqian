/****************************************************************************
 * chips/bk7258/include/bk7258_system_reset.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SYSTEM_RESET_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SYSTEM_RESET_H

#include <nuttx/compiler.h>

#include <arch/chip/bk7258_reset_cause.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Reset the complete BK7258 device while preserving an explicit SoC reset
 * source.  This is CP-only chip mechanism; callers choose the semantic
 * reason, while the implementation owns the AON watchdog/PMU sequence.
 */

void bk7258_system_reset(enum bk7258_reset_source_e source)
  noreturn_function;

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SYSTEM_RESET_H */
