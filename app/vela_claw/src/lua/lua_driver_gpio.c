/****************************************************************************
 * app/vela_claw/src/lua/lua_driver_gpio.c
 *
 * GPIO driver binding for the Lua engine. On real t5board it toggles a BK7258
 * GPIO via the board SDK; on host it logs the action so the wiring is
 * exercised by unit tests. Exposes led_blink(n) to Lua.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_log.h"

#ifdef __NuttX__
/* Real BK7258 GPIO control would be included here, e.g.:
 *   #include <arch/chip/bk7258_gpio.h>
 * For the bring-up foundation we keep the host-compatible logging path and
 * only switch to real register toggles once the board SDK is linked. */
#endif

int claw_gpio_led_blink(int times) {
  if (times <= 0) times = 1;
  CLAW_LOGI("gpio: blink LED x%d (GPIO output toggle)", times);
  /* TODO(t5_board): drive the real BK7258 GPIO via the board SDK API. */
  return CLAW_OK;
}
