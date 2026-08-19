/****************************************************************************
 * tests/bk7258/mocks/driver/gpio.h
 *
 * Host stand-in for the SDK driver/gpio.h device-mux API used by
 * bk7258_irda.c.  The implementation lives in framework/mock_sdk_irda.c
 * and is programmable (recorded calls, injectable failure).
 ****************************************************************************/

#ifndef __MOCK_DRIVER_GPIO_H
#define __MOCK_DRIVER_GPIO_H

#include "driver/gpio_types.h"

typedef int bk_err_t;

bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t gpio_dev);

#endif /* __MOCK_DRIVER_GPIO_H */
