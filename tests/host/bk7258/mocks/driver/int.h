/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/mocks/driver/int.h
 *
 * Host stand-in for the SDK driver/int.h interrupt-registration surface
 * used by bk7258_irda.c.  The implementation lives in
 * framework/mock_sdk_irda.c.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_INT_H
#define __MOCK_DRIVER_INT_H

#include "driver/int_types.h"

typedef void (*int_group_isr_t)(void);

typedef int bk_err_t;

bk_err_t bk_int_isr_register(icu_int_src_t dev, int_group_isr_t isr,
                             void *arg);

#endif /* __MOCK_DRIVER_INT_H */
