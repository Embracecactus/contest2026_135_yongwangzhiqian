/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/nuttx_yuv/clock.h
 *
 * YUV suite variant of the clock shim (replaces mocks/nuttx/clock.h for
 * the bk7258_yuv_h264 build only, via -I mocks/nuttx_yuv ahead of -I
 * mocks).
 *
 * clock_t is the POSIX one from <sys/types.h>; CLK_TCK reflects the 1 kHz
 * tick used by the mock.  clock_systime_ticks() is implemented by
 * mock_sdk_yuv_h264.c and is test-controllable (default 0), so deadline
 * arithmetic stays exact.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_YUV_CLOCK_H
#define __MOCK_NUTTX_YUV_CLOCK_H

#include <stdint.h>
#include <sys/types.h>

#define CLK_TCK 1000

#define MSEC2TICK(ms) ((clock_t)(ms))

clock_t clock_systime_ticks(void);

#endif /* __MOCK_NUTTX_YUV_CLOCK_H */
