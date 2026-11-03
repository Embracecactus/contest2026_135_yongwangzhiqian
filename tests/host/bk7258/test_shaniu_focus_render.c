/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_focus_pixels.h"
#include "bk7258_pc_approval.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
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
static uint16_t observed[2][160*160];
static int bkdisplay_framebuffer_write(const char *path, const uint16_t *pixels)
{ assert(pixels); memcpy(observed[path[0]=='r'], pixels, sizeof(observed[0])); writes++; return path[0]=='r' && fail_right ? -EIO : 0; }
static int bkdisplay_builtin_locked(struct bkdisplay_service_s *s, bool b)
{ (void)s; assert(b); fallback++; return 0; }
#include "bk7258_display_focus.inc"
static atomic_uint g_focus_visual;
#include "focus-command.inc"
#include "approval-visual.inc"
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
  /* Task results use shape as well as color, through the real renderer. */
  assert(bkfocus_pixel(4,0,16,-8)==0x07e0);
  assert(bkfocus_pixel(5,0,0,0)==0xf800);
  assert(bkfocus_pixel(6,0,0,0)==0xffe0);
  for (unsigned state=4;state<=6;state++)
    {
      int before=writes;
      assert(bkdisplay_focus_present_locked(&service,state<<8)==0);
      assert(writes==before+2);
      assert(bkdisplay_focus_present_locked(&service,state<<8)==0);
      assert(writes==before+2);
    }
  /* A current 65-second snapshot displays 1:05, through both framebuffers. */
  assert(bkdisplay_focus_present_locked(&service,0x81000041u)==0);
  assert(observed[0][(80-5)*160+80-16]==0x07e0);
  assert(bkdisplay_focus_present_locked(&service,0)==0);

  /* Exercise approval -> actual display admission -> both framebuffers. */
  struct bkpc_approval_s approval = {
    .state = BKPC_APPROVAL_PENDING, .deadline = 60000, .ttl = 60000
  };
  visual = bkpc_approval_visual(&approval, 0, true);
  assert(visual == ((7u << 8) | 32));
  bk7258_display_focus(visual);
  assert(atomic_load(&g_focus_visual) == visual);
  int before = writes;
  assert(bkdisplay_focus_present_locked(&service,
                                        atomic_load(&g_focus_visual)) == 0);
  assert(writes == before + 2);
  for (unsigned eye = 0; eye < 2; eye++)
    {
      assert(observed[eye][(80 - 16) * 160 + 80] == 0xfd20);
      assert(observed[eye][(80 + 18) * 160 + 80] == 0xfd20);
      assert(observed[eye][(80 + 10) * 160 + 80] == 0);
    }

  bk7258_display_focus((8u << 8) | 32);
  assert(atomic_load(&g_focus_visual) == visual);
  bk7258_display_focus((7u << 8) | 33);
  assert(atomic_load(&g_focus_visual) == visual);
  approval.state = BKPC_APPROVAL_APPROVED;
  bk7258_display_focus(bkpc_approval_visual(&approval, 1, true));
  assert(atomic_load(&g_focus_visual) == 0);
  assert(bkdisplay_focus_present_locked(&service,
                                        atomic_load(&g_focus_visual)) == 0);
  puts("CONTRACT_PASS");return 0;
}
