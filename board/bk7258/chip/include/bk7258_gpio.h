/****************************************************************************
 * board/bk7258/chip/include/bk7258_gpio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 generic GPIO lower-half/test board binding.
 *
 * GPIO clients live in the chip/CP layer and must not consume the selected
 * board's generated board.h directly.  The physical board supplies this
 * small, typed binding from its board bring-up source instead.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_GPIO_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_GPIO_H

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

#define BK7258_GPIO_BINDING_VERSION  1u

struct bk7258_gpio_config_s
{
  uint16_t version;
  uint16_t size;
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

/* Implemented by the selected physical board.  A board without a
 * BOM-backed user LED/key binding does not provide this function; its Kconfig
 * must consequently keep the GPIO lower-half and manual tests disabled.
 */

FAR const struct bk7258_gpio_config_s *bk7258_board_gpio_config(void);

#ifdef CONFIG_BK7258_GPIO_FOUNDATION_TEST
int bk7258_gpio_foundation_test(void);
#endif

#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
int bk7258_gpio_irq_test(void);
#endif

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
int bk7258_gpio_lowerhalf_initialize(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_GPIO_H */
