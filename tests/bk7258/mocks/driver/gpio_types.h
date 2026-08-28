/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/bk7258/mocks/driver/gpio_types.h
 *
 * Host stand-in for the immutable v3.1.1.9 SDK driver/gpio_types.h surface
 * used by bk7258_irda.c: the gpio_id_t values and the GPIO device-mux
 * token for the IrDA receiver.  Only the constants the driver references
 * are modelled.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_GPIO_TYPES_H
#define __MOCK_DRIVER_GPIO_TYPES_H

#include <stdint.h>

typedef uint32_t gpio_id_t;

#define GPIO_25  ((gpio_id_t)25)

typedef uint32_t gpio_dev_t;

#define GPIO_DEV_IRDA  ((gpio_dev_t)0xe0u)

#endif /* __MOCK_DRIVER_GPIO_TYPES_H */
