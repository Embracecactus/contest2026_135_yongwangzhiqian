/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/mocks/driver/gpio.h
 *
 * Host stand-in for the SDK driver/gpio.h surface consumed by maintained
 * BK7258 chip implementations.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_GPIO_H
#define __MOCK_DRIVER_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio_types.h"

typedef int bk_err_t;

#define GPIO_CTRL_LDO_MODULE_SDIO 0u
#define GPIO_CTRL_LDO_MODULE_LCD  1u
#define GPIO_CTRL_LDO_MODULE_NFC  5u

typedef enum
{
  GPIO_OUTPUT_STATE_LOW = 0,
  GPIO_OUTPUT_STATE_HIGH,
  GPIO_OUTPUT_STATE_INVALID
} gpio_output_state_e;

bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t gpio_dev);
bk_err_t bk_gpio_driver_init(void);
bk_err_t bk_gpio_enable_output(gpio_id_t gpio_id);
bk_err_t bk_gpio_disable_output(gpio_id_t gpio_id);
bk_err_t bk_gpio_enable_input(gpio_id_t gpio_id);
bk_err_t bk_gpio_disable_input(gpio_id_t gpio_id);
bk_err_t bk_gpio_disable_pull(gpio_id_t gpio_id);
bk_err_t bk_gpio_pull_up(gpio_id_t gpio_id);
bk_err_t bk_gpio_pull_down(gpio_id_t gpio_id);
bk_err_t bk_gpio_set_output_value(gpio_id_t gpio_id, bool value);
bool bk_gpio_set_capacity(gpio_id_t gpio_id, uint32_t capacity);
bool bk_gpio_get_input(gpio_id_t gpio_id);
bool bk_gpio_get_output(gpio_id_t gpio_id);
bk_err_t bk_gpio_set_interrupt_type(gpio_id_t gpio_id,
                                    gpio_int_type_t type);
bk_err_t bk_gpio_enable_interrupt(gpio_id_t gpio_id);
bk_err_t bk_gpio_disable_interrupt(gpio_id_t gpio_id);
bk_err_t bk_gpio_clear_interrupt(gpio_id_t gpio_id);
bk_err_t bk_gpio_register_isr(gpio_id_t gpio_id, gpio_isr_t isr);

#endif /* __MOCK_DRIVER_GPIO_H */
