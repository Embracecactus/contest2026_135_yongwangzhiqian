/* SPDX-License-Identifier: Apache-2.0 */
/* Real render/cache/volume functions are extracted verbatim by the driver. */
#define _XOPEN_SOURCE 700
#define CONFIG_BK7258_PROVISION_NATIVE 1
#include <assert.h>
#include <errno.h>
#include <dirent.h>
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
#include "bk7258_display_power_pixels.h"
#include "bk7258_display_service.h"
#include "bk7258_media_volume.h"
#include "bk7258_display_trial_control.h"
#include "bk7258_display_selection_control.h"
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
static unsigned expect_power_phase;
static bool power_on_read;
static bool onboarding_render, power_on_frame;
static bool cancel_on_mount, cancel_on_write, fail_frame, fail_unmount;
static bool fail_directory_sync;
static uint32_t selection_id;
static bool catalog_cancel_on_read;
struct dirent *__real_readdir(DIR *dir);
struct dirent *__wrap_readdir(DIR *dir)
{
  if (power_on_read)
    {
      power_on_read = false;
      assert(bk7258_display_power(2) == 0);
      struct bkdisplay_selection_status_s status;
      assert(bk7258_display_selection_status(&status) == 0);
      assert(status.state == BKDISPLAY_SELECTION_CANCEL_PENDING);
    }
  if (catalog_cancel_on_read)
    {
      catalog_cancel_on_read = false;
      assert(bk7258_display_selection_cancel(selection_id) == 0);
      struct bkdisplay_selection_status_s status;
      assert(bk7258_display_selection_status(&status) == 0);
      assert(status.state == BKDISPLAY_SELECTION_CANCEL_PENDING);
    }
  return __real_readdir(dir);
}
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
  if (onboarding_render)
    {
      if (power_on_frame)
        {
          power_on_frame=false;
          assert(bk7258_display_power(2)==0);
        }
    }
  else if (expect_power_phase)
    {
      unsigned center = 80 * 160 + 80;
      if (expect_power_phase == 3)
        { assert(pixels[center] == 0xf800 && pixels[center + 20 * 160] == 0); }
      else
        { assert(pixels[center] == 0 && pixels[center - 20 * 160] ==
                   (expect_power_phase == 2 ? 0xfd20 : 0x07ff)); }
    }
  else if(expect_green)
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
static struct bkdisplay_service_s *power_service;
#define g_bkdisplay_service (*power_service)
static unsigned power_lock_calls;
static unsigned power_unlock_calls;
static bool onboarding_lock_granted;
int nxmutex_lock(mutex_t *lock)
{ (void)lock; power_lock_calls++; return onboarding_lock_granted?0:-EBUSY; }
void bkdisplay_unlock(struct bkdisplay_service_s *service)
{ assert(service==power_service && onboarding_lock_granted);power_unlock_calls++; }
static bool bkdisplay_service_node(const char *path, bool block)
{ (void)path; (void)block; return true; }
#define qrcodegen_BUFFER_LEN_FOR_VERSION(version) 512
#define BKDISPLAY_QR_VERSION 5
#define qrcodegen_Ecc_MEDIUM 0
#define qrcodegen_Mask_AUTO 0
static bool qrcodegen_encodeText(const char *text,uint8_t *temp,uint8_t *qr,
                                 int ecc,int minv,int maxv,int mask,bool boost)
{ (void)text;(void)temp;(void)ecc;(void)minv;(void)maxv;(void)mask;(void)boost;
  memset(qr,0,512);return true; }
static int qrcodegen_getSize(const uint8_t *qr)
{ (void)qr;return 37; }
static bool qrcodegen_getModule(const uint8_t *qr,int x,int y)
{ (void)qr;return ((x+y)&1)==0; }
static void mbedtls_platform_zeroize(void *p,size_t n)
{ volatile uint8_t *out=p;while(n--)*out++=0; }
static int bkdisplay_builtin_locked(struct bkdisplay_service_s *service,
                                    bool fallback);
#include "onboarding-request.inc"
#include "power-request.inc"
#include "power-render.inc"

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
static struct bkselection_control_s selection_control;
static uint32_t wire_kind=11;
static uint32_t sequence;
static uint8_t response[BKCONTROL_RESPONSE_SIZE];
static void put32(uint8_t *p,uint32_t v)
{p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static uint32_t get32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int execute(void *c,enum bkcontrol_command_e cmd,uint32_t arg,struct bkcontrol_status_s *s)
{(void)c;(void)cmd;(void)arg;(void)s;return 0;}
#ifdef TEST_SELECTION_PRODUCT
#include "bk7258_pc_usb.h"
#include "bk7258_display_job_control.h"
#define CONFIG_BK7258_DISPLAY_SERVICE 1
#define g_pc_selection selection_control
static uint64_t g_pc_selection_binding,g_pc_selection_grant;
static uint8_t g_pc_selection_client[16];
static struct bkpc_usb_owner_s g_pc_usb_owner;
static struct bkcontrol_pair_s pair_fixture;
static struct bkpack_control_s g_pc_pack;
static bool g_identity_bound=true,g_control_bound=true;
static uint64_t source_grant=2;
static int bkpc_authorization_snapshot(void *ctx,uint64_t *binding,struct bkprov_pc_snapshot_s *view)
{(void)ctx;memset(view,0,sizeof(*view));*binding=1;view->revision=source_grant;
 view->capabilities=BKPC_CAP_RESOURCES;return 0;}
#define bkpack_control_invalidate(...) (assert(false))
#define bk7258_display_job_quiesce(stop) ((void)(stop),0)
#define mbedtls_platform_zeroize(p,n) ((void)memset(p,0,n))
#define bkagent_ota_busy() false
#define bkvoice_config_now_ms(ctx) clock_ms
/* Installer and entropy are outside this default-route fixture. They must
 * not be invoked: resource authority is already bound and tested separately. */
#define bkpack_control_bind(...) (assert(false),-ENOTSUP)
#define bkpack_control_read(...) (assert(false),-ENOTSUP)
#define bkpack_control_apply(...) (assert(false),-ENOTSUP)
static int selection_entropy(void *ctx,uint8_t *p,size_t n)
{static uint8_t epoch=7;assert(ctx==&pair_fixture.tls.random && n==16);memset(p,0,n);p[0]=epoch++;return 0;}
#define mbedtls_ctr_drbg_random selection_entropy
static int product_config(void *c,enum bkcontrol_command_e cmd,uint32_t kind,uint32_t off,
                          const uint8_t *p,size_t n,struct bkcontrol_status_s *s)
{(void)c;(void)cmd;(void)kind;(void)off;(void)p;(void)n;(void)s;return -ENOTSUP;}
#include "selection-product.inc"
#endif
#ifdef TEST_PHONE_SELECTION
#define g_phone_selection selection_control
static int phone_scope_error=-EACCES;
static uint8_t phone_epoch=7;
static bool phone_admitted=true;
#define g_identity_bound phone_admitted
#define g_control_bound phone_admitted
#define bkagent_ota_busy() false
static int bkprov_owner_control_scope(uint8_t out[16],bool create)
{(void)create;memset(out,0,16);if(phone_scope_error)return phone_scope_error;
 out[0]=phone_epoch;return 0;}
#include "selection-phone.inc"
#endif
static int config(void *c,enum bkcontrol_command_e cmd,uint32_t kind,uint32_t off,
                  const uint8_t *p,size_t n,struct bkcontrol_status_s *s)
{
#ifdef TEST_PHONE_SELECTION
  (void)c;return (kind==17||kind==18)?product_phone_selection_config(cmd,kind,off,p,n,s):-ENOTSUP;
#elif defined(TEST_SELECTION_PRODUCT)
  return product_pc_config(c,cmd,kind,off,p,n,s);
#else
  (void)c;return kind==17?bkselection_control(&selection_control,cmd,off,p,n,s):
    kind==18?bkcatalog_control(&selection_control,cmd,off,p,n,s):
    kind==11?bkdisplay_trial_control(cmd,off,p,n,s,clock_ms):-ENOTSUP;
#endif
}
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
  uint8_t begin[8]={0};put32(begin,wire_kind);put32(begin+4,size);
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
static void client_peer(struct bkdisplay_service_s *service,bool selection)
{
  uint8_t key[32]={1};char line[513];
  unsigned initial_writes=writes;
  if(selection){uint8_t epoch[16]={7};assert(bkselection_control_bind(&selection_control,epoch)==0);}
  assert(bkcontrol_session_open(&wire,key,execute,NULL)==0);
  assert(bkcontrol_session_set_config_handler(&wire,config)==0);
  while(fgets(line,sizeof(line),stdin))
    {
      if(selection && !strcmp(line,"fail-unmount\n"))
        {fail_unmount=true;puts("FAULT_READY");}
      else if(selection && !strcmp(line,"stats\n"))
        {printf("STATS %u %u %u %u\n",writes-initial_writes,frames,mounts,unmounts);}
      else if(selection && !strcmp(line,"step\n"))
        {
          struct bkdisplay_selection_status_s current;
          assert(bk7258_display_selection_status(&current)==0);
          expect_green=!strcmp(current.version.filename,"shaniu-upload-v1.bkep");
          (void)bkdisplay_selection_recover_step(service);
          (void)bkdisplay_selection_step(service,true);
          printf("SELECT %u %u\n",writes-initial_writes,frames);
        }
      else if(!strcmp(line,"step\n"))
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
#ifdef TEST_SELECTION_TLS
#include "selection_tls_peer.inc"
#endif
/* Recovery retries release only; the original save/render outcome is immutable. */
static void selection_wire_case(struct bkdisplay_service_s *service,const char *mode)
{
  uint8_t epoch[16]={7},record[96]={'E','S','C','1',0,0,0,1};
  uint8_t query[20]={0},snapshot[128],older[16];
  struct bkdisplay_selection_status_s state;
  unsigned io=mounts,stored=writes,painted=frames;
  memcpy(record+8,epoch,16);record[24]=1;record[55]=1;
  strcpy((char *)record+56,"shaniu-upload-v1.bkep");
  wire_kind=17;connect_wire();
  assert(wire_apply(record,96)==-EACCES);
#ifdef TEST_SELECTION_PRODUCT
  g_pc_pack.bound=false;g_pc_pack.binding=1;g_pc_pack.grant=2;
  g_pc_usb_owner.pair=&pair_fixture;
  memcpy(g_pc_pack.epoch,epoch,16);
  g_pc_usb_owner.usb.lease.open=true;
  g_pc_usb_owner.usb.lease.capabilities=BKPC_CAP_SCENES;
  g_pc_usb_owner.usb.lease.binding=1;g_pc_usb_owner.usb.lease.revision=2;
  assert(wire_apply(record,96)==-EACCES);
  g_pc_usb_owner.usb.lease.capabilities=BKPC_CAP_RESOURCES;

#elif defined(TEST_PHONE_SELECTION)
  phone_scope_error=0;
#else
  assert(bkselection_control_bind(&selection_control,epoch)==0);
#endif
  put32(query,17u<<16);query[4]=3;
  assert(packet(BKCONTROL_CONFIG_READ,query,20)==0);
  assert(!memcmp(response+24,"ESS1",4));memcpy(older,response+24,16);
#ifdef TEST_SELECTION_PRODUCT
  assert(!g_pc_pack.bound); /* Query must not touch the installer lifecycle. */
  g_pc_usb_owner.usb.lease.revision=3;
  assert(wire_apply(record,96)==-ESTALE);
  g_pc_usb_owner.usb.lease.revision=2;
#endif
  assert(mounts==io && writes==stored);
  if(!strcmp(mode,"selection-wire-invalid"))
    {
      record[8]^=1;assert(wire_apply(record,96)==-ESTALE);record[8]^=1;
      record[44]=1;assert(wire_apply(record,96)==-EINVAL);record[44]=0;
      record[95]=1;assert(wire_apply(record,96)==-EINVAL);record[95]=0;
      record[24]=0;assert(wire_apply(record,96)==-EINVAL);record[24]=1;
      strcpy((char *)record+56,"../a.bkep");assert(wire_apply(record,96)==-EINVAL);
      assert(writes==stored && frames==painted && mounts==io);return;
    }
  if(!strcmp(mode,"selection-wire-refresh"))
    {
      record[7]=2;memset(record+48,0,48);
      assert(wire_apply(record,96)==0);assert(bkdisplay_selection_step(service,true));
      assert(bk7258_display_selection_status(&state)==0 && state.version_known);
      assert(state.version.revision==1 && !state.save_confirmed && !state.render_confirmed);
      assert(mounts==io+1 && frames==painted && writes==stored);return;
    }
  assert(wire_apply(record,96)==0);
  assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_PENDING);
  assert(wire_apply(record,96)==0);
  record[55]=2;assert(wire_apply(record,96)==-EEXIST);record[55]=1;
  assert(mounts==io && writes==stored && frames==painted);
#ifdef TEST_PHONE_SELECTION
  if(!strcmp(mode,"selection-wire-phone-revoke"))
    {
      phone_scope_error=-EACCES;product_phone_selection_step();
      assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_CANCELED);
      assert(!bkdisplay_selection_step(service,true));
      assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_CANCELED);
      assert(wire_apply(record,96)==-EACCES);
      phone_scope_error=0;phone_epoch++;
      assert(wire_apply(record,96)==-ESTALE);
      assert(writes==stored && frames==painted && mounts==io);return;
    }
  if(!strcmp(mode,"selection-wire-phone-reconnect"))
    {
      bkcontrol_session_close(&wire);product_phone_selection_step();
      assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_PENDING);
      connect_wire();assert(wire_apply(record,96)==0);
    }
  if(!strcmp(mode,"selection-wire-phone-replace"))
    {
      /* A new authenticated principal must not inherit the old staged job. */
      phone_epoch++;product_phone_selection_step();
      assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_CANCELED);
      assert(wire_apply(record,96)==-ESTALE);
      assert(writes==stored && frames==painted && mounts==io);return;
    }
#endif
  if(!strcmp(mode,"selection-wire-revoke"))
    {
      bkselection_control_invalidate(&selection_control);
      assert(!bkdisplay_selection_step(service,true));
      assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_CANCELED);
      assert(wire_apply(record,96)==-EACCES);epoch[0]++;
      assert(bkselection_control_bind(&selection_control,epoch)==0);
      assert(wire_apply(record,96)==-ESTALE);
      assert(writes==stored && frames==painted && mounts==io);return;
    }
  if(!strcmp(mode,"selection-wire-cancel"))
    {
      memset(record+48,0,48);record[7]=3;record[24]=2;put32(record+40,1);
      assert(wire_apply(record,96)==0);assert(!bkdisplay_selection_step(service,true));
      assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_CANCELED);
      assert(writes==stored && frames==painted);return;
    }
  if(!strcmp(mode,"selection-wire-recover"))
    {
      fail_unmount=true;assert(bkdisplay_selection_step(service,true));
      assert(bk7258_display_selection_status(&state)==0 && state.release_error==-EIO);
      unsigned committed=writes;
      record[7]=4;record[24]=2;put32(record+40,1);memset(record+48,0,48);
      assert(wire_apply(record,96)==0);assert(wire_apply(record,96)==0);
      assert(bkdisplay_selection_recover_step(service));
      assert(wire_apply(record,96)==0);assert(!bkdisplay_selection_recover_step(service));
      assert(bk7258_display_selection_status(&state)==0 && state.release_error==0);
      assert(state.state==BKDISPLAY_SELECTION_UNKNOWN && state.save_confirmed && !state.render_confirmed);
      assert(frames==painted && writes==committed);return;
    }
  expect_green=true;assert(bkdisplay_selection_step(service,true));
  assert(bk7258_display_selection_status(&state)==0 && state.state==BKDISPLAY_SELECTION_DONE);
  assert(state.save_confirmed && state.render_confirmed && state.version.revision==2);
  assert(wire_apply(record,96)==0);selected("shaniu-upload-v1");
  assert(packet(BKCONTROL_CONFIG_READ,query,20)==0);
  assert(!memcmp(response+24,older,16)); /* Same query retains pre-write state. */
  query[4]=4;put32(query,(17u<<16)|16);assert(packet(BKCONTROL_CONFIG_READ,query,20)==-ESTALE);
  for(unsigned off=0;off<128;off+=16)
    {put32(query,(17u<<16)|off);assert(packet(BKCONTROL_CONFIG_READ,query,20)==0);
     memcpy(snapshot+off,response+24,16);}
  assert(!memcmp(snapshot,"ESS1",4) && get32(snapshot+4)==BKDISPLAY_SELECTION_DONE);
  assert(get32(snapshot+24)==1 && get32(snapshot+36)==7 && snapshot[47]==2);
  assert(!memcmp(snapshot+56,record+24,16) && !strcmp((char *)snapshot+72,"shaniu-upload-v1.bkep"));
  assert(frames==painted+2);
#ifdef TEST_PHONE_SELECTION
  phone_scope_error=-EACCES;product_phone_selection_step();
  assert(!selection_control.bound);
  assert(bk7258_display_selection_status(&state)==0 && state.save_confirmed);
  assert(state.state==BKDISPLAY_SELECTION_DONE && state.version.revision==2);
  selected("shaniu-upload-v1");
  assert(frames==painted+2);
#endif
#ifdef TEST_SELECTION_PRODUCT
  source_grant=3;product_pc_pack_step(true);
  assert(!selection_control.bound && !g_pc_pack.bound);
  g_pc_usb_owner.usb.lease.revision=3;
  assert(wire_apply(record,96)==-ESTALE);
  assert(bk7258_display_selection_status(&state)==0 && state.save_confirmed);
  assert(state.version.revision==2 && state.state==BKDISPLAY_SELECTION_DONE);
#endif
}
static void recovery_case(struct bkdisplay_service_s *service,const char *mode)
{
  struct bkdisplay_selection_status_s before,after;
  uint32_t next=0;
  assert(bk7258_display_selection_recover(0)==-EINVAL);
  assert(bk7258_display_selection_recover(1)==-ESTALE);
  assert(bk7258_display_selection_request("shaniu-upload-v1.bkep",1,0,&selection_id)==0);
  fail_unmount=true;
  assert(bkdisplay_selection_step(service,true));
  assert(bk7258_display_selection_status(&before)==0);
  assert(before.state==BKDISPLAY_SELECTION_UNKNOWN && before.release_error==-EIO);
  assert(before.save_confirmed && !before.render_confirmed);
  unsigned io=mounts,stored=writes,painted=frames,closed=unmounts;
  assert(bk7258_display_selection_recover(selection_id+1)==-ESTALE);
  assert(bk7258_display_selection_recover(selection_id)==0);
  assert(bk7258_display_selection_recover(selection_id)==0);
  assert(bk7258_display_selection_status(&after)==0 && after.recovery_pending);
  assert(after.release_error==-EIO && writes==stored && mounts==io && unmounts==closed);
  assert(bk7258_display_selection_cancel(selection_id)==-EBUSY);
  assert(bk7258_display_selection_refresh(selection_id,&next)==-EBUSY);
  if(!strcmp(mode,"selection-recover-gate")) bkdisplay_intent_gate(false);
  if(!strcmp(mode,"selection-recover-failure")) fail_unmount=true;
  assert(bkdisplay_selection_recover_step(service));
  assert(bk7258_display_selection_status(&after)==0 && !after.recovery_pending);
  assert(after.id==before.id && after.state==before.state && after.error==before.error);
  assert(after.version.revision==before.version.revision && after.save_confirmed);
  assert(!after.render_confirmed && writes==stored && frames==painted && mounts==io);
  if(!strcmp(mode,"selection-recover-failure"))
    {
      assert(after.release_error==-EIO && unmounts==closed);
      assert(bkdisplay_volume_open(service)==-EBUSY);
      assert(!bkdisplay_selection_recover_step(service)); /* No automatic retry. */
      assert(bk7258_display_selection_recover(selection_id)==0);
      assert(bkdisplay_selection_recover_step(service));
    }
  assert(bk7258_display_selection_status(&after)==0 && after.release_error==0);
  assert(!service->volume_leased && !service->volume_mounted && unmounts==closed+1);
  assert(bk7258_display_selection_recover(selection_id)==-EALREADY);
  assert(!bkdisplay_selection_recover_step(service));
  if(!strcmp(mode,"selection-recover-gate"))
    {assert(bk7258_display_selection_refresh(selection_id,&next)==-EBUSY);bkdisplay_intent_gate(true);}
  assert(bk7258_display_selection_refresh(selection_id,&next)==0);
  assert(next==selection_id+1);
  assert(bk7258_display_selection_recover(selection_id)==-ESTALE);
  assert(bkdisplay_selection_step(service,true));
  assert(bk7258_display_selection_status(&after)==0);
  assert(after.state==BKDISPLAY_SELECTION_DONE && after.version.revision==2);
  assert(!after.save_confirmed && !after.render_confirmed);
  assert(writes==stored && frames==painted);
}
static void selection_case(struct bkdisplay_service_s *service,const char *mode)
{
  if(!strncmp(mode,"selection-wire-",15)){selection_wire_case(service,mode);return;}
  if(!strncmp(mode,"selection-recover-",18)){recovery_case(service,mode);return;}
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

#include "catalog_job_cases.inc"
#include "catalog_wire_cases.inc"
#include "power_request_cases.inc"

static void onboarding_clear_case(struct bkdisplay_service_s *service)
{
  unsigned io=mounts,painted=frames,stored=writes;
  uint32_t id=0;
  memcpy(service->claim_qr,"SN1:",4);
  memset(service->claim_qr+4,'A',103);
  service->claim_qr[107]=0;
  bkdisplay_intent_gate(false);

  /* Closing an existing claim window is an exit intent. It must publish no
   * framebuffer/storage work and must not wait for the render owner. */
  assert(bk7258_display_onboarding(NULL)==0);
  assert(power_lock_calls==0 && service->claim_qr[0]);
  assert(frames==painted && mounts==io && writes==stored);

  /* A stale readiness observation cannot reopen ordinary display work until
   * the worker has consumed the clear request. */
  bkdisplay_intent_gate(true);
  assert(bk7258_display_request_expression("happy",&id)==-EBUSY);
  bkdisplay_power_apply_locked(service);
  assert(!service->claim_qr[0] && service->overlay_dirty);
  assert(frames==painted && mounts==io && writes==stored);

  bkdisplay_intent_gate(true);
  assert(bk7258_display_request_expression("happy",&id)==0);
  assert(bk7258_display_cancel_expression(id)==0);
}

static void onboarding_power_case(struct bkdisplay_service_s *service)
{
  char qr[108]="SN1:";
  memset(qr+4,'A',103);
  qr[107]=0;
  service->started=true;
  onboarding_lock_granted=true;
  onboarding_render=true;
  power_on_frame=true;
  unsigned painted=frames;

  /* The power request is injected by the real framebuffer boundary after
   * onboarding's initial admission check. A newer shutdown intent must make
   * the public open fail, so its caller cannot copy the secret or open GATT. */
  assert(bk7258_display_onboarding(qr)==-EAGAIN);
  assert(power_lock_calls==1 && power_unlock_calls==1);
  assert(frames==painted+2 && !power_on_frame);
  assert(!service->claim_qr[0]);
  assert(g_bkdisplay_power_requested==2 && g_bkdisplay_power_pending);
  uint32_t id=0;
  assert(bk7258_display_request_expression("happy",&id)==-EBUSY);
}

int main(int argc,char **argv)
{
  assert(argc==4 && mkdtemp(root));install(argv[1],true);install(argv[2],false);
  struct bkdisplay_service_s service={.devices_ready=true};
  assert(bkdisplay_render_locked(&service,"neutral")==0);
  selected("shaniu-default-v1");bkdisplay_intent_gate(true);
  unsigned baseline_writes=writes,baseline_frames=frames,baseline_mounts=mounts;
  uint32_t id=0;
  const char *name=!strcmp(argv[3],"missing")?"missing.bkep":"shaniu-upload-v1.bkep";
  if (!strncmp(argv[3], "catalog-wire-", 13))
    catalog_wire_case(&service, argv[3]);
  else if (!strcmp(argv[3], "onboarding-clear-held-lock"))
    {
      power_service=&service;
      onboarding_clear_case(&service);
    }
  else if (!strcmp(argv[3], "onboarding-power-preempts-open"))
    {
      power_service=&service;
      onboarding_power_case(&service);
    }
  else if (!strncmp(argv[3], "power-request-", 14))
    {
      power_service = &service;
      power_request_case(&service, argv[3]);
    }
  else if(!strncmp(argv[3],"catalog-job-",12))
    catalog_job_case(&service,argv[3]);
  else if(!strncmp(argv[3],"selection-",10))
    {selection_case(&service,argv[3]);baseline_writes=writes;}
  else if(!strcmp(argv[3],"--peer"))
    client_peer(&service,false);
#ifdef TEST_SELECTION_TLS
  else if(!strcmp(argv[3],"--selection-tls-peer"))
    {selection_tls_peer(&service);baseline_writes=writes;}
#endif
  else if(!strcmp(argv[3],"--selection-peer"))
    {client_peer(&service,true);baseline_writes=writes;}
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
