/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_DISPLAY_POWER_PIXELS_H
#define BK7258_DISPLAY_POWER_PIXELS_H

#include <stdint.h>

/* Built-in pixels need no external volume; coordinates are center-relative. */
static inline uint16_t bkdisplay_power_pixel(unsigned int phase, int x, int y)
{
  if (phase == 3)
    {
      /* A red X distinguishes failure by shape as well as color. */
      int a = x - y, b = x + y;
      return x >= -28 && x <= 28 && y >= -28 && y <= 28 &&
             ((a >= -4 && a <= 4) || (b >= -4 && b <= 4)) ? 0xf800 : 0;
    }
  int rr = x * x + y * y;
  int lit = phase ?
      ((rr >= 28 * 28 && rr <= 34 * 34 && (y > -23 || x < -13 || x > 13)) ||
       (x >= -3 && x <= 3 && y >= -39 && y <= -7)) :
      (rr < 39 * 39 && rr > 17 * 17);
  return lit ? (phase == 2 ? 0xfd20 : 0x07ff) : 0;
}
#endif
