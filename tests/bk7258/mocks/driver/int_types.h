/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/bk7258/mocks/driver/int_types.h
 *
 * Host stand-in for the SDK driver/int_types.h interrupt-source enum used
 * by bk7258_irda.c.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_INT_TYPES_H
#define __MOCK_DRIVER_INT_TYPES_H

#include <stdint.h>

typedef uint32_t icu_int_src_t;

#define INT_SRC_IRDA  ((icu_int_src_t)32)

#endif /* __MOCK_DRIVER_INT_TYPES_H */
