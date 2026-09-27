/* SPDX-License-Identifier: Apache-2.0 */
/* Real render/cache/volume functions are extracted verbatim by the driver. */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include "bk7258_display_store.h"
#include "bk7258_display_service.h"
#include "bk7258_media_volume.h"
typedef int mutex_t; /* Extracted service layout only; no mutex operations. */
static char root[]="/tmp/pack-trial-XXXXXX";
#define BKDISPLAY_MOUNTROOT root
#define BKDISPLAY_MOUNTPOINT root
#define BKDISPLAY_BLOCKDEV "/test/block"
#define BKDISPLAY_FB0 "left"
#define BKDISPLAY_FB1 "right"
static uint64_t clock_ms=100;
static uint64_t bkdisplay_now_ms(void) { return clock_ms; }
static unsigned mounts,unmounts,frames,writes;
static bool expect_green;
ssize_t __real_write(int fd,const void *p,size_t n);
ssize_t __wrap_write(int fd,const void *p,size_t n)
{ writes++;return __real_write(fd,p,n); }
static int test_mount(const char *a,const char *b,const char *c,unsigned long d,const void *e)
{ (void)a;(void)b;(void)c;(void)d;(void)e;mounts++;return 0; }
static int test_umount(const char *p) { (void)p;unmounts++;return 0; }
#define mount test_mount
#define umount test_umount
static int bkdisplay_framebuffer_write(const char *path,const uint16_t *pixels)
{
  assert(pixels && (!strcmp(path,"left")||!strcmp(path,"right")));
  if(expect_green)
    {for(size_t i=0;i<BKDISPLAY_CANVAS_PIXELS;i++)assert(pixels[i]==0x07e0);}
  else assert(pixels[0]==0x0842); /* Source palette background #090b13. */
  frames++;return 0;
}
#include "pack-trial-render.inc"
#include "bk7258_display_render_identity.inc"
#include "bk7258_display_intent.inc"
static void install(const char *path,bool activate)
{
  FILE *f=fopen(path,"rb");assert(f);
  assert(fseek(f,0,SEEK_END)==0);long n=ftell(f);assert(n>0);rewind(f);
  unsigned char *bytes=malloc(n);assert(bytes && fread(bytes,1,n,f)==(size_t)n);assert(fclose(f)==0);
  if(activate)assert(bkdisplay_store_import(root,bytes,n,NULL)==0);
  else
    {
      struct bkdisplay_upload_s u={0};assert(bkdisplay_upload_begin(&u,root,n)==0);
      for(size_t off=0;off<(size_t)n;)
        {size_t take=(size_t)n-off;if(take>4096)take=4096;assert(bkdisplay_upload_append(&u,off,bytes+off,take)==0);off+=take;}
      assert(bkdisplay_upload_finish(&u,NULL)==0);
    }
  free(bytes);
}
static void expect(enum bkdisplay_trial_state_e state)
{struct bkdisplay_trial_status_s s;assert(bk7258_display_trial_status(&s)==0);assert(s.state==state);}
static void selected(const char *name)
{struct bkdisplay_store_selection_s s;assert(bkdisplay_store_resolve(root,&s)==0);assert(!strcmp(s.info.pack_id,name));}
static int remove_entry(const char *p,const struct stat *s,int type,struct FTW *w)
{(void)s;(void)type;(void)w;return remove(p);}
int main(int argc,char **argv)
{
  assert(argc==4 && mkdtemp(root));install(argv[1],true);install(argv[2],false);
  struct bkdisplay_service_s service={.devices_ready=true};
  assert(bkdisplay_render_locked(&service,"neutral")==0);
  selected("shaniu-default-v1");bkdisplay_intent_gate(true);
  unsigned baseline_writes=writes,baseline_frames=frames,baseline_mounts=mounts;
  uint32_t id=0;
  const char *name=!strcmp(argv[3],"missing")?"missing.bkep":"shaniu-upload-v1.bkep";
  if(!strcmp(argv[3],"invalid"))
    {
      assert(bk7258_display_trial_pack_checked("../bad.bkep","happy",30,0,&id)==-EINVAL);
      assert(mounts==baseline_mounts && frames==baseline_frames);
    }
  else
    {
      assert(bk7258_display_trial_pack_checked(name,"happy",30,0,&id)==0);
      expect(BKDISPLAY_TRIAL_PENDING);assert(frames==baseline_frames && writes==baseline_writes);
      if(!strcmp(argv[3],"queued-cancel"))
        {assert(bk7258_display_cancel_trial(id)==0);assert(!bkdisplay_intent_step(&service,true));expect(BKDISPLAY_TRIAL_CANCELED);assert(mounts==baseline_mounts);}
      else if(!strcmp(argv[3],"queued-expiry"))
        {clock_ms=130;assert(bkdisplay_intent_step(&service,true));expect(BKDISPLAY_TRIAL_EXPIRED);assert(mounts==baseline_mounts);}
      else
        {
          expect_green=strcmp(argv[3],"missing")!=0;
          assert(bkdisplay_intent_step(&service,true));
          if(!strcmp(argv[3],"missing"))
            {expect(BKDISPLAY_TRIAL_FAILED);assert(frames==baseline_frames);assert(!strcmp(service.status.pack_id,"shaniu-default-v1"));}
          else
            {
              expect(BKDISPLAY_TRIAL_ACTIVE);assert(!strcmp(service.status.pack_id,"shaniu-upload-v1"));
              assert(frames==baseline_frames+2);selected("shaniu-default-v1");
              if(!strcmp(argv[3],"supersede"))
                {
                  assert(bkdisplay_store_activate(root,name,NULL)==0);
                  baseline_writes=writes;
                  assert(bkdisplay_render_locked(&service,"sad")==0);
                  unsigned newer=frames;clock_ms=130;
                  assert(bkdisplay_trial_step(&service,true));expect(BKDISPLAY_TRIAL_SUPERSEDED);
                  assert(frames==newer && !strcmp(service.status.expression,"sad"));selected("shaniu-upload-v1");
                }
              else
                {
                  if(!strcmp(argv[3],"cancel"))assert(bk7258_display_cancel_trial(id)==0);
                  else clock_ms=130;
                  expect_green=false;
                  assert(bkdisplay_trial_step(&service,true));
                  expect(!strcmp(argv[3],"cancel")?BKDISPLAY_TRIAL_CANCELED:BKDISPLAY_TRIAL_EXPIRED);
                  assert(!strcmp(service.status.pack_id,"shaniu-default-v1"));
                  assert(!strcmp(service.status.expression,"neutral"));selected("shaniu-default-v1");
                }
            }
        }
    }
  assert(writes==baseline_writes && mounts==unmounts);
  assert(!bkdisplay_intent_pending());
  bkdisplay_intent_supersede();
  for(unsigned i=0;i<5;i++)free(service.frames[i]);
  free(service.speaking_frame);
  assert(nftw(root,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0);
  puts("CONTRACT_PASS pack trial real renderer");return 0;
}
