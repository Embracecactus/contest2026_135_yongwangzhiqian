/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "bk7258_display_power_pixels.h"

int main(void)
{
  /* Existing public icon geometry remains byte-for-byte stable. */
  for (unsigned int phase = 0; phase <= 2; phase++)
    for (int y = -80; y < 80; y++)
      for (int x = -80; x < 80; x++)
        {
          int rr = x*x + y*y;
          int lit = phase ?
            ((rr >= 784 && rr <= 1156 && (y > -23 || x < -13 || x > 13)) ||
             (x >= -3 && x <= 3 && y >= -39 && y <= -7)) :
            (rr < 1521 && rr > 289);
          assert(bkdisplay_power_pixel(phase, x, y) ==
                 (lit ? (phase == 2 ? 0xfd20 : 0x07ff) : 0));
        }
  /* Failure is a visible X, distinct in shape even without color. */
  assert(bkdisplay_power_pixel(3, 0, 0) != 0);
  assert(bkdisplay_power_pixel(2, 0, 0) == 0);
  assert(bkdisplay_power_pixel(3, 20, 20) != 0);
  assert(bkdisplay_power_pixel(3, 20, -20) != 0);
  assert(bkdisplay_power_pixel(3, 0, 20) == 0);
  assert(bkdisplay_power_pixel(3, 40, 40) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
