/****************************************************************************
 * tests/bk7258/mocks/arch/chip/bk7258_gpio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __TESTS_BK7258_MOCKS_ARCH_CHIP_BK7258_GPIO_H
#define __TESTS_BK7258_MOCKS_ARCH_CHIP_BK7258_GPIO_H

#include <stdbool.h>
#include <stdint.h>

struct bk7258_gpio_config_s
{
  const char *name;
  uint8_t user_led_gpio;
  bool user_led_active_high;
  bool user_led_console_shared;
  uint8_t user_button_gpio;
  bool user_button_active_low;
};

int bk7258_gpio_lowerhalf_initialize(
  const struct bk7258_gpio_config_s *config);

#endif /* __TESTS_BK7258_MOCKS_ARCH_CHIP_BK7258_GPIO_H */
