/****************************************************************************
 * chips/bk7258/include/bk7258_gpio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 generic GPIO lower-half/test configuration.
 *
 * The physical board passes its immutable LED/key wiring explicitly when it
 * invokes a chip lower half or validation helper.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_GPIO_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_GPIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_gpio_config_s
{
  FAR const char *name;
  uint8_t user_led_gpio;
  bool user_led_active_high;
  bool user_led_console_shared;
  uint8_t user_button_gpio;
  bool user_button_active_low;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_GPIO_FOUNDATION_TEST
int bk7258_gpio_foundation_test(
  FAR const struct bk7258_gpio_config_s *config);
#endif

#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
int bk7258_gpio_irq_test(
  FAR const struct bk7258_gpio_config_s *config);
#endif

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
int bk7258_gpio_lowerhalf_initialize(
  FAR const struct bk7258_gpio_config_s *config);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_GPIO_H */
