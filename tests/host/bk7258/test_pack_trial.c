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
#include "bk7258_display_trial_control.h"
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
static bool cancel_on_mount, cancel_on_write, fail_frame, fail_unmount;
static bool fail_directory_sync;
static uint32_t selection_id;
ssize_t __real_write(int fd,const void *p,size_t n);
ssize_t __wrap_write(int fd,const void *p,size_t n)
{ writes++;
  if (cancel_on_write)
    { cancel_on_write=false;assert(bk7258_display_selection_cancel(selection_id)==-EBUSY); }
  return __real_write(fd,p,n); }
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  struct stat info;
  if(fail_directory_sync && fstat(fd,&info)==0 && S_ISDIR(info.st_mode))
    {fail_directory_sync=false;errno=EIO;return -1;}
  return __real_fsync(fd);
}
static int test_mount(const char *a,const char *b,const char *c,unsigned long d,const void *e)
{ (void)a;(void)b;(void)c;(void)d;(void)e;mounts++;
  if(cancel_on_mount)
    {cancel_on_mount=false;assert(bk7258_display_selection_cancel(selection_id)==0);
     struct bkdisplay_selection_status_s s;assert(bk7258_display_selection_status(&s)==0);
     assert(s.state==BKDISPLAY_SELECTION_CANCEL_PENDING);}
  return 0; }
static int test_umount(const char *p)
{(void)p;if(fail_unmount){fail_unmount=false;errno=EIO;return -1;}unmounts++;return 0;}
#define mount test_mount
#define umount test_umount
static int bkdisplay_framebuffer_write(const char *path,const uint16_t *pixels)
{
  assert(pixels && (!strcmp(path,"left")||!strcmp(path,"right")));
  if(expect_green)
    {for(size_t i=0;i<BKDISPLAY_CANVAS_PIXELS;i++)assert(pixels[i]==0x07e0);}
  else assert(pixels[0]==0x0842); /* Source palette background #090b13. */
  if(fail_frame){fail_frame=false;return -EIO;}
  frames++;return 0;
}
static inline bool bkdisplay_selection_storage_blocked(void);
#include "pack-trial-render.inc"
#include "bk7258_display_render_identity.inc"
#include "bk7258_display_intent.inc"
#include "bk7258_display_selection.inc"
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
static struct bkcontrol_session_s wire;
static uint32_t sequence;
static uint8_t response[BKCONTROL_RESPONSE_SIZE];
static void put32(uint8_t *p,uint32_t v)
{p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static uint32_t get32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int execute(void *c,enum bkcontrol_command_e cmd,uint32_t arg,struct bkcontrol_status_s *s)
{(void)c;(void)cmd;(void)arg;(void)s;return 0;}
static int config(void *c,enum bkcontrol_command_e cmd,uint32_t kind,uint32_t off,
                  const uint8_t *p,size_t n,struct bkcontrol_status_s *s)
{(void)c;return kind==11?bkdisplay_trial_control(cmd,off,p,n,s,clock_ms):-ENOTSUP;}
static int packet(enum bkcontrol_command_e cmd,const uint8_t *p,size_t n)
{
  uint8_t bytes[80]={0};assert(n<=64);memcpy(bytes,"SDC1",4);
  put32(bytes+4,cmd);put32(bytes+8,sequence);put32(bytes+12,n);
  if(n)memcpy(bytes+16,p,n);
  assert(bkcontrol_session_packet(&wire,bytes,n+16,response)==0);
  int ret=(int32_t)get32(response+16);
  if(ret!=-EPROTO)sequence++;
  return ret;
}
static void connect_wire(void)
{
  uint8_t key[32]={1};sequence=0;bkcontrol_session_close(&wire);
  assert(bkcontrol_session_open(&wire,key,execute,NULL)==0);
  assert(bkcontrol_session_set_config_handler(&wire,config)==0);
  assert(packet(BKCONTROL_AUTH,key,32)==0);
}
static int wire_apply(const uint8_t *record,size_t size)
{
  uint8_t begin[8]={0};put32(begin,11);put32(begin+4,size);
  int ret=packet(BKCONTROL_CONFIG_BEGIN,begin,8);if(ret)return ret;
  for(size_t off=0;off<size;)
    {size_t n=size-off;if(n>13)n=13;ret=packet(BKCONTROL_CONFIG_APPEND,record+off,n);if(ret)return ret;off+=n;}
  return packet(BKCONTROL_CONFIG_APPLY,NULL,0);
}
static void wire_case(struct bkdisplay_service_s *service,const char *variant)
{
  /* Independent wire bytes, not generated by a production encoder. */
  uint8_t record[72]={'E','T','C','2',0,0,0,1,0,0,0,0,0,0,0,30,
                     1,2,3,4,5,6,7,8,0,0,0,2,0,0,0,0};
  const uint8_t cancel[32]={'E','T','C','1',0,0,0,2,0,0,0,1,0,0,0,0,
                          1,2,3,4,5,6,7,9,0,0,0,0,0,0,0,0};
  strcpy((char *)record+32,!strcmp(variant,"wire-missing")?"missing.bkep":"shaniu-upload-v1.bkep");
  unsigned before=frames,io=mounts;
  connect_wire();
  if(!strcmp(variant,"wire-invalid"))
    {
      const char *bad[]={"../bad.bkep","Bad.bkep","a/b.bkep","a\\b.bkep","x.txt",""};
      for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
        {memset(record+32,0,40);strcpy((char *)record+32,bad[i]);assert(wire_apply(record,72)==-EINVAL);}
      memset(record+32,'a',40);assert(wire_apply(record,72)==-EINVAL);
      memset(record+32,0,40);strcpy((char *)record+32,"a.bkep");record[71]=1;
      assert(wire_apply(record,72)==-EINVAL);
      record[71]=0;record[7]=2;assert(wire_apply(record,72)==-EINVAL);
      assert(frames==before && mounts==io);expect(BKDISPLAY_TRIAL_IDLE);
    }
  else
    {
      assert(wire_apply(record,72)==0);expect(BKDISPLAY_TRIAL_PENDING);
      assert(frames==before && mounts==io);
      expect_green=strcmp(variant,"wire-missing")!=0;
      assert(bkdisplay_intent_step(service,true));
      if(!strcmp(variant,"wire-missing"))
        {expect(BKDISPLAY_TRIAL_FAILED);assert(frames==before);}
      else
        {
          expect(BKDISPLAY_TRIAL_ACTIVE);assert(frames==before+2);selected("shaniu-default-v1");
          clock_ms=110;connect_wire();assert(wire_apply(record,72)==0);
          assert(frames==before+2); /* Reconnect/repeat never renews deadline. */
          record[32]='x';assert(wire_apply(record,72)==-EEXIST);record[32]='s';
          uint8_t old[32];memcpy(old,record,32);old[3]='1';assert(wire_apply(old,32)==-EEXIST);
          expect_green=false;
          if(!strcmp(variant,"wire-cancel"))
            {assert(wire_apply(cancel,32)==0);expect(BKDISPLAY_TRIAL_CANCEL_PENDING);assert(frames==before+2);}
          else clock_ms=130;
          assert(bkdisplay_trial_step(service,true));
          expect(!strcmp(variant,"wire-cancel")?BKDISPLAY_TRIAL_CANCELED:BKDISPLAY_TRIAL_EXPIRED);
          assert(frames==before+4);selected("shaniu-default-v1");
        }
    }
  bkcontrol_session_close(&wire);
}
/* Host transport peer only: protocol, trial, store and pixels stay real. */
static void client_peer(struct bkdisplay_service_s *service)
{
  uint8_t key[32]={1};char line[513];
  unsigned initial_writes=writes;
  assert(bkcontrol_session_open(&wire,key,execute,NULL)==0);
  assert(bkcontrol_session_set_config_handler(&wire,config)==0);
  while(fgets(line,sizeof(line),stdin))
    {
      if(!strcmp(line,"step\n"))
        {
          struct bkdisplay_trial_status_s current;
          assert(bk7258_display_trial_status(&current)==0);
          expect_green=current.state==BKDISPLAY_TRIAL_PENDING;
          if(!bkdisplay_intent_step(service,true))
            (void)bkdisplay_trial_step(service,true);
          selected("shaniu-default-v1");assert(writes==initial_writes);
          printf("STEP %u %s %s\n",frames,service->status.pack_id,service->status.expression);
        }
      else if(!strncmp(line,"time ",5))
        {
          unsigned long long value;assert(sscanf(line+5,"%llu",&value)==1);
          clock_ms=value;puts("TIME");
        }
      else
        {
          uint8_t bytes[80];size_t n=strcspn(line,"\n");
          assert(n>0 && n%2==0 && n/2<=sizeof(bytes));
          for(size_t i=0;i<n/2;i++)
            {unsigned value;assert(sscanf(line+i*2,"%2x",&value)==1);bytes[i]=value;}
          assert(bkcontrol_session_packet(&wire,bytes,n/2,response)==0);
          for(size_t i=0;i<sizeof(response);i++)printf("%02x",response[i]);
          putchar('\n');
        }
      fflush(stdout);
    }
  bkcontrol_session_close(&wire);
}
static void selection_case(struct bkdisplay_service_s *service,const char *mode)
{
  struct bkdisplay_selection_status_s state;
  unsigned io=mounts,painted=frames,stored=writes;
  uint32_t trial=0;
  if(!strcmp(mode,"selection-supersede"))
    {assert(bk7258_display_trial_checked("happy",30,0,&trial)==0);
     assert(bkdisplay_intent_step(service,true));io=mounts;painted=frames;}
  if(!strcmp(mode,"selection-refresh"))
    assert(bk7258_display_selection_refresh(0,&selection_id)==0);
  else
    assert(bk7258_display_selection_request("shaniu-upload-v1.bkep",
      !strcmp(mode,"selection-stale")?0:1,0,&selection_id)==0);
  assert(selection_id==1 && writes==stored && frames==painted && mounts==io);
  assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_PENDING);
  assert(writes==stored && mounts==io);
  if(!strcmp(mode,"selection-cancel"))
    {assert(bk7258_display_selection_cancel(selection_id)==0);
     assert(!bkdisplay_selection_step(service,true));}
  else if(!strcmp(mode,"selection-gate"))
    {bkdisplay_intent_gate(false);assert(!bkdisplay_selection_step(service,true));}
  else
    {
      if(!strcmp(mode,"selection-stale-job"))
        {uint32_t old=selection_id,ignored;
         assert(bk7258_display_selection_cancel(old)==0);
         assert(bk7258_display_selection_request("shaniu-upload-v1.bkep",1,old,&selection_id)==0);
         assert(selection_id==old+1);
         assert(bk7258_display_selection_cancel(old)==-ESTALE);
         assert(bk7258_display_selection_refresh(old,&ignored)==-ESTALE);}
      cancel_on_mount=!strcmp(mode,"selection-preparing-cancel");
      cancel_on_write=!strcmp(mode,"selection-commit-cancel");
      fail_frame=!strcmp(mode,"selection-render-failure");
      fail_unmount=!strcmp(mode,"selection-release-failure");
      fail_directory_sync=!strcmp(mode,"selection-commit-unknown");
      expect_green=true;
      assert(bkdisplay_selection_step(service,true));
    }
  assert(bk7258_display_selection_status(&state)==0);
  if(!strcmp(mode,"selection-cancel") || !strcmp(mode,"selection-gate") ||
     !strcmp(mode,"selection-preparing-cancel"))
    {assert(state.state==BKDISPLAY_SELECTION_CANCELED && !state.save_confirmed);
     assert(writes==stored && frames==painted);selected("shaniu-default-v1");}
  else if(!strcmp(mode,"selection-refresh"))
    {assert(state.state==BKDISPLAY_SELECTION_DONE && state.version_known);
     assert(state.version.revision==1 && !state.save_confirmed && !state.render_confirmed);
     assert(!strcmp(state.version.filename,"shaniu-default-v1.bkep"));
     assert(writes==stored && frames==painted);}
  else if(!strcmp(mode,"selection-commit-unknown"))
    {assert(state.state==BKDISPLAY_SELECTION_UNKNOWN && state.error==-EIO);
     assert(!state.save_confirmed && !state.render_confirmed && frames==painted);
     selected("shaniu-upload-v1"); /* Visible rename is not confirmed durability. */
     assert(bk7258_display_selection_refresh(selection_id,&selection_id)==0);
     assert(bkdisplay_selection_step(service,true));
     assert(bk7258_display_selection_status(&state)==0);
     assert(state.state==BKDISPLAY_SELECTION_DONE && state.version_known);
     assert(state.version.revision==2 && !state.save_confirmed && !state.render_confirmed);}
  else if(!strcmp(mode,"selection-stale"))
    {assert(state.state==BKDISPLAY_SELECTION_FAILED && state.error==-ESTALE);
     assert(!state.save_confirmed && writes==stored && frames==painted);}
  else
    {
      assert(state.save_confirmed && state.version_known && state.version.revision==2);
      selected("shaniu-upload-v1");assert(writes>stored);
      if(!strcmp(mode,"selection-render-failure"))
        {assert(state.state==BKDISPLAY_SELECTION_FAILED && state.error==-EIO);
         assert(!state.render_confirmed && frames==painted);}
      else if(!strcmp(mode,"selection-release-failure"))
        {assert(state.state==BKDISPLAY_SELECTION_UNKNOWN && state.release_error==-EIO);
         assert(!state.render_confirmed && frames==painted && bkdisplay_intent_pending());
         uint32_t next;
         assert(bk7258_display_selection_refresh(selection_id,&next)==-EBUSY);
         assert(bkdisplay_volume_open(service)==-EBUSY);
         assert(bkdisplay_volume_close(service)==0); /* Explicit fixture cleanup only. */}
      else
        {assert(state.state==BKDISPLAY_SELECTION_DONE && state.render_confirmed);
         assert(frames==painted+2);
         if(trial){clock_ms=130;assert(!bkdisplay_trial_step(service,true));
                   expect(BKDISPLAY_TRIAL_SUPERSEDED);assert(frames==painted+2);}}
    }
  unsigned stable=writes;
  if(!state.release_error)assert(!bkdisplay_selection_step(service,true));
  assert(writes==stable);
}
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
  if(!strncmp(argv[3],"selection-",10))
    {selection_case(&service,argv[3]);baseline_writes=writes;}
  else if(!strcmp(argv[3],"--peer"))
    client_peer(&service);
  else if(!strncmp(argv[3],"wire-",5))
    wire_case(&service,argv[3]);
  else if(!strcmp(argv[3],"invalid"))
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
  if(strcmp(argv[3],"selection-release-failure"))assert(!bkdisplay_intent_pending());
  bkdisplay_intent_supersede();
  for(unsigned i=0;i<5;i++)free(service.frames[i]);
  free(service.speaking_frame);
  assert(nftw(root,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0);
  puts("CONTRACT_PASS pack trial real renderer");return 0;
}
