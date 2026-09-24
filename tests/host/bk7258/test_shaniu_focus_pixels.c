/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_focus_pixels.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
  assert(bkfocus_segments(60000,60000)==0);
  assert(bkfocus_segments(60000,30000)==16);
  assert(bkfocus_segments(60000,0)==32);
  assert(bkfocus_segments(UINT64_MAX,UINT64_MAX)==0);
  assert(bkfocus_segments(UINT64_MAX,0)==32);
  assert(bkfocus_pixel(1,0,0,-56)==0x2104);
  assert(bkfocus_pixel(1,1,0,-56)==0x07ff);
  assert(bkfocus_pixel(1,16,0,56)==0x2104);
  assert(bkfocus_pixel(1,32,0,56)==0x07ff);
  assert(bkfocus_pixel(2,16,-8,35)!=bkfocus_pixel(1,16,-8,35));
  assert(bkfocus_pixel(3,32,0,8)!=bkfocus_pixel(1,32,0,8));
  puts("CONTRACT_PASS");return 0;
}
