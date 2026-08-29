/****************************************************************************
 * chips/bk7258/include/bk7258_pinmux.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SoC pin-function selector ownership.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PINMUX_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PINMUX_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* One physical pin selector update.  function is the four-bit BK7258 SYS
 * selector value.  peripheral enables the pad's second-function route;
 * false returns the pad to GPIO ownership.
 */

struct bk7258_pinmux_config_s
{
  uint8_t pin;
  uint8_t function;
  bool peripheral;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Validate the complete request, then update all selectors and pad ownership
 * under the SDK SYS AMP lock and one local interrupt critical section.
 */

int bk7258_pinmux_apply(const struct bk7258_pinmux_config_s *configs,
                        size_t count);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PINMUX_H */
