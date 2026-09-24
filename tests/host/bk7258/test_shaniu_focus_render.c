/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_focus_pixels.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#define BKDISPLAY_CANVAS_WIDTH 160
#define BKDISPLAY_CANVAS_HEIGHT 160
#define BKDISPLAY_CANVAS_PIXELS (160*160)
#define BKDISPLAY_FB0 "left"
#define BKDISPLAY_FB1 "right"
#define BKDISPLAY_SERVICE_READY 3
struct bkdisplay_service_s {
  struct { int state; int last_error; unsigned render_sequence; } status;
  uint16_t *frames[1]; unsigned focus_painted; bool speaking_painted;
};
static int writes, fail_right, fallback;
static int bkdisplay_framebuffer_write(const char *path, const uint16_t *pixels)
{ assert(pixels); writes++; return path[0]=='r' && fail_right ? -EIO : 0; }
static int bkdisplay_builtin_locked(struct bkdisplay_service_s *s, bool b)
{ (void)s; assert(b); fallback++; return 0; }
#include "bk7258_display_focus.inc"
int main(void)
{
  struct bkdisplay_service_s service = {0};
  unsigned visual = (1u<<8)|16;
  fail_right=1;
  assert(bkdisplay_focus_present_locked(&service,visual)==-EIO);
  assert(service.status.render_sequence==0 && service.focus_painted==0);
  assert(writes==2);
  fail_right=0;
  assert(bkdisplay_focus_present_locked(&service,visual)==0);
  assert(service.status.render_sequence==1 && writes==4);
  assert(bkdisplay_focus_present_locked(&service,visual)==0);
  assert(service.status.render_sequence==1 && writes==4);
  assert(bkdisplay_focus_present_locked(&service,0)==0);
  assert(service.status.render_sequence==2 && fallback==1);
  puts("CONTRACT_PASS");return 0;
}
