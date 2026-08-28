/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/nuttx/wdog.h
 *
 * Host shim for the NuttX watchdog-timer (wdog) surface used by
 * bk7258_irda.c for key debounce / repeat classification.  wd_start records
 * the pending handler; the test fires it explicitly via mock_wdog_fire(),
 * so the 112 ms debounce cadence is fully deterministic.  The
 * implementation lives in framework/mock_wdog.c.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_WDOG_H
#define __MOCK_NUTTX_WDOG_H

#include <stdint.h>

typedef uint32_t wdparm_t;
typedef void (*wdentry_t)(wdparm_t arg);

typedef struct wdog_s
{
  int unused;
} wdog_t;

/* 1 tick == 1 ms for the host shim. */

#define WDOG_USEC2TICKS(us)  ((int)((us) / 1000u))
#define MSEC2TICK(ms)        ((int)(ms))

int wd_start(wdog_t *wdog, int delay, wdentry_t handler, wdparm_t arg);
int wd_cancel(wdog_t *wdog);

/* Test control: fire the currently pending wdog handler, if any. */

void mock_wdog_reset(void);
void mock_wdog_fire(void);
int mock_wdog_pending_count(void);

#endif /* __MOCK_NUTTX_WDOG_H */
