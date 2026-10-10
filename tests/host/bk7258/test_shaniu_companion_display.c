/* SPDX-License-Identifier: Apache-2.0 */
/* Production intent and cached renderer; only LCD/clock/lock boundaries fake. */
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_display_service.h"
#define BKDISPLAY_FB0 "fb0"
#define BKDISPLAY_FB1 "fb1"
#define LOG_INFO 6
#define syslog(...) ((void)0)
typedef unsigned irqstate_t;
static int g_bkdisplay_intent_lock;
static bool g_bkdisplay_intent_allowed = true, selection_busy, trial_busy;
static struct bkdisplay_expression_request_s g_bkdisplay_intent;
static atomic_bool g_speaking;
static atomic_uint g_activity;
static atomic_uint g_focus_visual;
static unsigned g_companion_visual;
static uint64_t g_companion_started, g_companion_deadline, now_ms=100;
static uint64_t g_bkdisplay_expression_identity=1;
static bool bkdisplay_selection_busy_locked(void) { return selection_busy; }
static bool bkdisplay_trial_busy(void) { return trial_busy; }
static irqstate_t spin_lock_irqsave(int *lock) { assert(!*lock); *lock=1; return 0; }
static void spin_unlock_irqrestore(int *lock, irqstate_t flags)
{ (void)flags; assert(*lock); *lock=0; }
static uint64_t bkdisplay_now_ms(void) { return now_ms; }
struct bkdisplay_service_s
{
  struct bkdisplay_service_status_s status;
  unsigned companion_painted;
  uint64_t companion_identity;
  uint16_t *frames[5];
};
static unsigned writes;
static uint16_t pixel;
static int write_error;
static int bkdisplay_framebuffer_write(const char *node, uint16_t *pixels)
{
  (void)node;
  assert(!g_bkdisplay_intent_lock);
  writes++; pixel=pixels[0]; return write_error;
}
#include "bk7258_display_companion.inc"
int main(int argc, char **argv)
{
  assert(argc==2);
  uint16_t frames[5]={10,11,12,13,14};
  struct bkdisplay_service_s s={.status.state=BKDISPLAY_SERVICE_READY};
  for(unsigned i=0;i<5;i++) s.frames[i]=frames+i;
  if(!strcmp(argv[1],"gate")) {
    g_bkdisplay_intent_allowed=false;
    assert(bk7258_display_companion(1,800)==-EBUSY && writes==0);
    g_bkdisplay_intent_allowed=true;
    trial_busy=true;
    assert(bk7258_display_companion(1,800)==-EBUSY);
    trial_busy=false; selection_busy=true;
    assert(bk7258_display_companion(1,800)==-EBUSY);
    selection_busy=false; atomic_store(&g_speaking,true);
    assert(bk7258_display_companion(1,800)==-EBUSY);
    atomic_store(&g_speaking,false); atomic_store(&g_focus_visual,4u<<8);
    assert(bk7258_display_companion(2,800)==-EALREADY);
    assert(writes==0);
  } else if(!strcmp(argv[1],"activity")) {
    bk7258_display_activity(1);
    assert(bk7258_display_companion(1,800)==-EBUSY);
    assert(bkdisplay_companion_step(&s,100) && pixel==13 && writes==2);
    bk7258_display_activity(2);
    assert(bkdisplay_companion_step(&s,200) && pixel==11 && writes==4);
    bk7258_display_activity(0);
    assert(bkdisplay_companion_step(&s,300) && pixel==10 && writes==6);
  } else {
    assert(bk7258_display_companion(1,800)==0 && writes==0);
    assert(bkdisplay_companion_step(&s,100) && writes==2 && pixel==13);
    assert(bkdisplay_companion_step(&s,200) && writes==2);
    if(!strcmp(argv[1],"expire")) {
      assert(bkdisplay_companion_step(&s,900) && writes==4 && pixel==10);
      assert(!bkdisplay_companion_step(&s,1000) && writes==4);
    } else if(!strcmp(argv[1],"cancel")) {
      assert(bk7258_display_companion(0,0)==0 && writes==2);
      assert(bkdisplay_companion_step(&s,300) && pixel==10 && writes==4);
    } else if(!strcmp(argv[1],"preempt")) {
      bkdisplay_companion_preempt(&s);
      assert(!bkdisplay_companion_step(&s,300) && writes==2);
      assert(!bkdisplay_companion_step(&s,1000) && writes==2);
    } else if(!strcmp(argv[1],"new-default")) {
      g_bkdisplay_expression_identity++;
      frames[0]=99;
      assert(!bkdisplay_companion_step(&s,300) && writes==2);
      assert(!bkdisplay_companion_step(&s,1000) && writes==2);
    } else if(!strcmp(argv[1],"rollback")) {
      assert(bkdisplay_companion_step(&s,50) && pixel==10 && writes==4);
    } else if(!strcmp(argv[1],"failure")) {
      write_error=-EIO;
      assert(bkdisplay_companion_step(&s,900));
      assert(s.status.last_error==-EIO && s.status.render_sequence==1);
    } else assert(0);
  }
  puts("CONTRACT_PASS");
  return 0;
}
