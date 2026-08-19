/*
 * mock_wdog.c - host wdog shim for the IrDA test suite.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * wd_start records one pending handler; mock_wdog_fire() invokes it and
 * clears the slot.  The debounce/repeat state machine of the IrDA driver
 * is exercised by firing the wdog exactly when a 112 ms tick would elapse.
 */
#include "nuttx/wdog.h"

#include <stddef.h>

static wdentry_t g_pending_handler;
static wdparm_t g_pending_arg;
static int g_pending_count;

int wd_start(wdog_t *wdog, int delay, wdentry_t handler, wdparm_t arg)
{
  (void)wdog;
  (void)delay;

  g_pending_handler = handler;
  g_pending_arg = arg;
  g_pending_count++;
  return 0;
}

int wd_cancel(wdog_t *wdog)
{
  (void)wdog;

  g_pending_handler = NULL;
  g_pending_arg = 0;
  return 0;
}

void mock_wdog_reset(void)
{
  g_pending_handler = NULL;
  g_pending_arg = 0;
  g_pending_count = 0;
}

void mock_wdog_fire(void)
{
  wdentry_t handler = g_pending_handler;

  g_pending_handler = NULL;
  if (handler != NULL)
    {
      handler(g_pending_arg);
    }
}

int mock_wdog_pending_count(void)
{
  return g_pending_count;
}
