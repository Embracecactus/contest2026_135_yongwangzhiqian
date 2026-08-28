/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/bk7258/framework/mock_sdk_irda.h
 *
 * Host mock for the small SDK surface used by bk7258_irda.c: the GPIO
 * device-mux call and the INT_SRC_IRDA registration.  Calls are recorded
 * so the suite can assert the receiver bring-up order; the GPIO map result
 * is injectable.
 ****************************************************************************/

#ifndef __MOCK_SDK_IRDA_H
#define __MOCK_SDK_IRDA_H

#include <stdint.h>

#include "driver/gpio_types.h"
#include "driver/int_types.h"

struct mock_irda_gpio_call_s
{
  gpio_id_t id;
  gpio_dev_t dev;
};

struct mock_irda_int_call_s
{
  icu_int_src_t src;
  int isr_registered;
};

/* Reset recorded calls and injectable results. */

void mock_irda_sdk_reset(void);

/* Programmable GPIO device-mux result (default OK). */

void mock_irda_set_gpio_result(int result);

/* Programmable ISR-registration result (default OK). */

void mock_irda_set_int_result(int result);

/* Programmable register_driver() result (default OK). */

void mock_irda_set_fs_register_result(int result);

/* Recorded calls. */

int mock_irda_gpio_calls(void);
int mock_irda_int_calls(void);
int mock_irda_fs_register_calls(void);
const struct mock_irda_gpio_call_s *mock_irda_gpio_call(int index);
const struct mock_irda_int_call_s *mock_irda_int_call(int index);
const char *mock_irda_fs_register_path(void);

/* Last registered ISR (for firing NEC frames directly). */

void (*mock_irda_isr(void))(void);

#endif /* __MOCK_SDK_IRDA_H */
