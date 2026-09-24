/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_FOCUS_PIXELS_H
#define BK7258_FOCUS_PIXELS_H
#include <stdint.h>
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
  if (state==3)
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
#endif
