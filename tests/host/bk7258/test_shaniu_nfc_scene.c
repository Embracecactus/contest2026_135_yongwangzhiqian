/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_nfc_scene.h"
#include "bk7258_focus_intent.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static struct bknfc_scene_s scene;
static struct bknfc_bindings_s bindings;
static const struct bknfc_card_s card = {.size=4,.uid={1,2,3,4}};
static uint32_t id;
static int sample(uint64_t seq, unsigned kind, bool enabled)
{ return bknfc_scene_observe(&scene,&bindings,7,seq,kind,kind==2?&card:NULL,enabled,&id); }
static void timer(unsigned state, uint64_t remaining, uint64_t now)
{
  struct bkfocus_snapshot_s value; assert(bkfocus_snapshot(&value,now)==0);
  assert(value.state==state && value.remaining_ms==remaining);
}
int main(int argc,char **argv)
{
  assert(argc==2);
  char root[]="/tmp/shaniu-scene-XXXXXX";assert(mkdtemp(root));
  assert(bknfc_bindings_open(&bindings,root)==0);
  assert(bknfc_bindings_set(&bindings,0,1,0,&card,60000)==0);
  assert(bknfc_scene_init(&scene,7)==0);
  bkfocus_intent_step(1000,true);
  if(!strcmp(argv[1],"dwell"))
    {
      assert(sample(1,2,true)==0 && id!=0);timer(0,0,1000);
      bkfocus_intent_step(2000,true);timer(1,60000,2000);
      for(unsigned i=2;i<102;i++)assert(sample(i,2,true)==-EALREADY && id==0);
      timer(1,50000,12000);
      assert(sample(102,1,true)==0 && id==0);
      assert(sample(103,2,true)==0 && id!=0);
      bkfocus_intent_step(13000,true);
      /* START cannot restart a running timer; shared owner returns EBUSY. */
      struct bkfocus_intent_status_s status;bkfocus_intent_status(&status);
      assert(status.phase==3 && status.error==-EBUSY);timer(1,49000,13000);
    }
  else if(!strcmp(argv[1],"unknown"))
    {
      assert(sample(1,2,true)==0);bkfocus_intent_step(2000,true);
      assert(sample(2,0,true)==0 && id==0);
      assert(sample(3,2,true)==-EALREADY && id==0);timer(1,59000,3000);
    }
  else if(!strcmp(argv[1],"stale"))
    {
      assert(sample(1,2,true)==0);bkfocus_intent_step(2000,true);
      assert(bknfc_scene_observe(&scene,&bindings,6,200,1,NULL,true,&id)==-ESTALE);
      assert(sample(1,1,true)==-ESTALE);assert(sample(2,2,true)==-EALREADY);
    }
  else if(!strcmp(argv[1],"gate"))
    {
      assert(sample(1,2,false)==-ESHUTDOWN && id==0);
      assert(sample(2,2,true)==-EALREADY);timer(0,0,2000);
      assert(sample(3,1,true)==0);assert(sample(4,2,true)==0);
      bkfocus_intent_step(2000,false);timer(0,0,2000);
      assert(sample(5,2,true)==-EALREADY);
    }
  else if(!strcmp(argv[1],"busy"))
    {
      uint32_t other;assert(bkfocus_intent_submit(1,30000,&other)==0);
      assert(sample(1,2,true)==-EBUSY && id==0);
      bkfocus_intent_step(2000,true);assert(sample(2,2,true)==-EALREADY);
      timer(1,30000,2000);
    }
  else if(!strcmp(argv[1],"binding"))
    {
      struct bknfc_card_s unknown=card;unknown.uid[0]=8;
      assert(bknfc_scene_observe(&scene,&bindings,7,1,2,&unknown,true,&id)==-ENOENT);
      assert(sample(2,2,true)==-EALREADY);
      assert(sample(3,1,true)==0);bindings.uncertain=true;
      assert(sample(4,2,true)==-EINPROGRESS && id==0);
      bindings.uncertain=false;assert(sample(5,2,true)==-EALREADY);timer(0,0,2000);
    }
  else if(!strcmp(argv[1],"invalid"))
    {
      struct bknfc_card_s bad={0};
      assert(bknfc_scene_observe(&scene,&bindings,7,1,2,&bad,true,&id)==-EINVAL);
      assert(sample(1,99,true)==-EINVAL);
      assert(sample(0,1,true)==-EINVAL);
      assert(sample(1,2,true)==0 && id!=0);
    }
  else assert(false);
  assert(bknfc_bindings_reset(root)==0);assert(rmdir(root)==0);
  puts("CONTRACT_PASS");
}
