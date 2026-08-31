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
#include <nuttx/compiler.h>

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

enum bk7258_gpio_pull_e
{
  BK7258_GPIO_PULL_NONE = 0,
  BK7258_GPIO_PULL_DOWN,
  BK7258_GPIO_PULL_UP,
};

enum bk7258_gpio_drive_e
{
  BK7258_GPIO_DRIVE_0 = 0,
  BK7258_GPIO_DRIVE_1,
  BK7258_GPIO_DRIVE_2,
  BK7258_GPIO_DRIVE_3,
};

enum bk7258_gpio_irq_trigger_e
{
  BK7258_GPIO_IRQ_FALLING_EDGE = 0,
};

typedef void (*bk7258_gpio_irq_callback_t)(uint8_t pin, FAR void *arg);

/* Stable chip-side identities for consumers of a physically shared external
 * rail.  Boards supply only the rail's control pin; the SDK vote namespace
 * remains an implementation detail of chips/bk7258.
 */

enum bk7258_shared_rail_client_e
{
  BK7258_SHARED_RAIL_SDIO = 0,
  BK7258_SHARED_RAIL_LCD,
  BK7258_SHARED_RAIL_NFC,
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Validate the complete request, then update all selectors and pad ownership
 * under the SDK SYS AMP lock and one local interrupt critical section.
 */

int bk7258_pinmux_apply(const struct bk7258_pinmux_config_s *configs,
                        size_t count);

/* Reclaim a pad from its current second function and configure it as GPIO.
 * These interfaces deliberately hide SDK GPIO types from board code.
 */

int bk7258_gpio_configure_output(uint8_t pin, bool high,
                                 enum bk7258_gpio_drive_e drive);
int bk7258_gpio_configure_input(uint8_t pin,
                                enum bk7258_gpio_pull_e pull);
int bk7258_gpio_write(uint8_t pin, bool high);
int bk7258_gpio_read_input(uint8_t pin, FAR bool *high);
int bk7258_gpio_read_output(uint8_t pin, FAR bool *high);

/* Open-drain helpers keep the input path enabled and only switch the low
 * output driver.  fast_write is reserved for timing-sensitive board buses
 * after the pin has been configured once through the normal interface.
 */

int bk7258_gpio_configure_open_drain(uint8_t pin,
                                     enum bk7258_gpio_pull_e pull);
int bk7258_gpio_open_drain_write(uint8_t pin, bool high);
int bk7258_gpio_fast_write(uint8_t pin, bool high);
int bk7258_gpio_fast_release_pullup(uint8_t pin);

/* The chip layer translates the public trigger/callback contract to the SDK
 * per-pin ISR ABI.  A physical board supplies only its pin and polarity.
 */

int bk7258_gpio_irq_configure(uint8_t pin,
                              enum bk7258_gpio_pull_e pull,
                              enum bk7258_gpio_irq_trigger_e trigger,
                              bk7258_gpio_irq_callback_t callback,
                              FAR void *arg);
int bk7258_gpio_irq_enable(uint8_t pin, bool enable);

/* Add or remove one consumer vote for a board-selected external rail. */

int bk7258_shared_rail_vote(enum bk7258_shared_rail_client_e client,
                            uint8_t control_pin, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PINMUX_H */
