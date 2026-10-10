/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_FOCUS_PIXELS_H
#define BK7258_FOCUS_PIXELS_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* A volatile, observed timer reply; low 23 bits are remaining seconds. */
#define BKFOCUS_STATUS_VISUAL 0x80000000u
/* Exact 32-step progress without floating point or overflowing uint64_t. */
static inline unsigned bkfocus_segments(uint64_t duration, uint64_t remaining)
{
  if (!duration || remaining > duration) return 0;
  uint64_t elapsed = duration - remaining;
  unsigned result = 0;
  for (unsigned i = 1; i <= 32; i++)
    {
      uint64_t threshold = (duration / 32) * i + ((duration % 32) * i + 31) / 32;
      if (elapsed < threshold) break;
      result = i;
    }
  return result;
}
static inline uint16_t bkfocus_pixel(unsigned state, unsigned segments, int x, int y)
{
  static const int8_t points[32][2] = {
    {0,-56},{11,-55},{21,-52},{31,-47},{40,-40},{47,-31},{52,-21},{55,-11},
    {56,0},{55,11},{52,21},{47,31},{40,40},{31,47},{21,52},{11,55},
    {0,56},{-11,55},{-21,52},{-31,47},{-40,40},{-47,31},{-52,21},{-55,11},
    {-56,0},{-55,-11},{-52,-21},{-47,-31},{-40,-40},{-31,-47},{-21,-52},{-11,-55}};
  int rr=x*x+y*y;
  if (rr>=52*52 && rr<=60*60) for (unsigned i=0;i<32;i++)
    {
      int dx=x-points[i][0],dy=y-points[i][1];
      if (dx*dx+dy*dy<=16) return i<segments ? 0x07ff : 0x2104;
    }
  if (state == 5)
    {
      /* Failure cross. */
      int a = y - x, b = y + x;
      return x >= -20 && x <= 20 && y >= -20 && y <= 20 &&
        ((a >= -3 && a <= 3) || (b >= -3 && b <= 3)) ? 0xf800 : 0;
    }
  if (state == 6)
    {
      /* Canceled: a horizontal stop bar, not a failure cross. */
      return x >= -22 && x <= 22 && y >= -3 && y <= 3 ? 0xffe0 : 0;
    }
  if (state == 3 || state == 4)
    {
      /* Check mark, also recognizable without color. */
      int a=y-x-8,b=y+x-8;
      return ((x>=-16 && x<=0 && a>=-3 && a<=3) ||
              (x>=0 && x<=24 && b>=-3 && b<=3)) ? 0x07e0 : 0;
    }
  if (state==2 && y>=30 && y<=40 &&
      ((x>=-10 && x<=-5)||(x>=5 && x<=10))) return 0xffe0;
  return rr<22*22 && rr>9*9 ? 0x07ff : 0;
}
static inline uint16_t bkfocus_status_pixel(unsigned visual, int x, int y)
{
  static const uint8_t masks[10] =
    {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
  unsigned state = (visual >> 24) & 7;
  unsigned seconds = visual & 0x7fffff;
  char digits[16];
  if (state == 4) return bkfocus_pixel(6, 0, x, y);
  if (state == 5) return bkfocus_pixel(5, 0, x, y);
  if (state == 6)
    {
      /* Unknown result: amber question mark, never a success check. */
      return ((y >= -15 && y <= -12 && x >= -9 && x <= 9) ||
              (x >= 6 && x <= 9 && y >= -12 && y <= -3) ||
              (y >= -3 && y <= 0 && x >= 0 && x <= 9) ||
              (x >= 0 && x <= 3 && y >= 0 && y <= 7) ||
              (x >= 0 && x <= 3 && y >= 12 && y <= 15)) ? 0xffe0 : 0;
    }

  if (seconds >= 3600)
    snprintf(digits, sizeof(digits), "%u:%02u:%02u", seconds / 3600,
             seconds / 60 % 60, seconds % 60);
  else
    snprintf(digits, sizeof(digits), "%u:%02u", seconds / 60, seconds % 60);
  int count = strlen(digits);
  int local = x + (count * 12 - 2) / 2;
  int row = y + 8;
  if (local >= 0 && local < count * 12 && row >= 0 && row < 14)
    {
      int column = local % 12;
      char ch = digits[local / 12];
      bool lit;
      if (ch == ':')
        lit = column >= 3 && column <= 4 &&
              ((row >= 3 && row <= 4) || (row >= 9 && row <= 10));
      else
        {
          unsigned mask = masks[ch - '0'];
          lit = ((mask & 1) && row < 2 && column < 8) ||
                ((mask & 2) && column >= 6 && column < 8 && row < 7) ||
                ((mask & 4) && column >= 6 && column < 8 && row >= 7) ||
                ((mask & 8) && row >= 12 && column < 8) ||
                ((mask & 16) && column < 2 && row >= 7) ||
                ((mask & 32) && column < 2 && row < 7) ||
                ((mask & 64) && row >= 6 && row < 8 && column < 8);
        }

      if (lit) return state == 1 ? 0x07e0 : 0xffe0;
    }

  if ((state == 0 || state == 2) && y >= 20 && y <= 23 &&
      x >= -12 && x <= 12) return 0xffe0;
  return 0;
}
#endif
