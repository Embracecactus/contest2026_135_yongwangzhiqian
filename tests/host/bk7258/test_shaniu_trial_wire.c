/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_display_trial_control.h"
#include "bk7258_display_service.h"
struct bkdisplay_service_s { struct bkdisplay_service_status_s status; };
static uint64_t now_ms = 100;
static uint64_t bkdisplay_now_ms(void) { return now_ms; }
static unsigned renders;
static int bkdisplay_render_pixels_locked(struct bkdisplay_service_s *s, const char *name)
{ renders++; strcpy(s->status.expression, name); return 0; }
#include "bk7258_display_render_identity.inc"
#include "bk7258_display_intent.inc"
static struct bkcontrol_session_s session;
static uint32_t seq;
static uint8_t reply[BKCONTROL_RESPONSE_SIZE];
static const uint8_t start[32] = {
  'E','T','C','1', 0,0,0,1, 0,0,0,0, 0,0,3,232,
  1,2,3,4,5,6,7,8, 0,0,0,2, 0,0,0,0
};
static const uint8_t cancel[32] = {
  'E','T','C','1', 0,0,0,2, 0,0,0,1, 0,0,0,0,
  1,2,3,4,5,6,7,9, 0,0,0,0, 0,0,0,0
};
static void put(uint8_t *p, uint32_t n)
{ p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
static uint32_t get(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static int execute(void *ctx, enum bkcontrol_command_e cmd, uint32_t arg, struct bkcontrol_status_s *s)
{ (void)ctx;(void)cmd;(void)arg;(void)s;return 0; }
static int config(void *ctx,enum bkcontrol_command_e cmd,uint32_t kind,uint32_t offset,
                  const uint8_t *p,size_t n,struct bkcontrol_status_s *s)
{ (void)ctx;return kind==11 ? bkdisplay_trial_control(cmd,offset,p,n,s,now_ms) : -ENOTSUP; }
static int send(enum bkcontrol_command_e cmd, const uint8_t *p, size_t n)
{
  uint8_t frame[80]={0};memcpy(frame,"SDC1",4);put(frame+4,cmd);put(frame+8,seq);put(frame+12,n);
  if(n)memcpy(frame+16,p,n);
  int ret=bkcontrol_session_packet(&session,frame,n+16,reply);
  if(ret)return ret;
  assert(get(reply+8)==seq++);
  return (int32_t)get(reply+16);
}
static void open_session(void)
{
  uint8_t key[32]={1};seq=0;
  bkcontrol_session_close(&session);
  assert(bkcontrol_session_open(&session,key,execute,NULL)==0);
  assert(bkcontrol_session_set_config_handler(&session,config)==0);
  assert(send(BKCONTROL_AUTH,key,32)==0);
}
static int apply(const uint8_t *record)
{
  const uint8_t begin[8]={0,0,0,11,0,0,0,32};
  assert(send(BKCONTROL_CONFIG_BEGIN,begin,8)==0);
  assert(send(BKCONTROL_CONFIG_APPEND,record,13)==0);
  assert(send(BKCONTROL_CONFIG_APPEND,record+13,19)==0);
  return send(BKCONTROL_CONFIG_APPLY,NULL,0);
}
static void read_header(unsigned state, unsigned id)
{
  const uint8_t read[4]={0,11,0,0};
  assert(send(BKCONTROL_CONFIG_READ,read,4)==0);
  assert(!memcmp(reply+24,"ETS1",4));
  assert(get(reply+28)==state && get(reply+32)==id);
}
int main(void)
{
  struct bkdisplay_service_s service={0};strcpy(service.status.expression,"neutral");
  bkdisplay_intent_gate(true);
  uint8_t key[32]={1}, begin[8]={0,0,0,11,0,0,0,32};
  assert(bkcontrol_session_open(&session,key,execute,NULL)==0);
  assert(bkcontrol_session_set_config_handler(&session,config)==0);
  assert(send(BKCONTROL_CONFIG_BEGIN,begin,8)==-EPROTO && renders==0);
  uint8_t wrong_key[32]={0};seq=0;
  assert(bkcontrol_session_open(&session,key,execute,NULL)==0);
  assert(bkcontrol_session_set_config_handler(&session,config)==0);
  assert(send(BKCONTROL_AUTH,wrong_key,32)==-EACCES);
  open_session();read_header(0,0);
  assert(apply(start)==0 && renders==0);read_header(1,1);
  assert(send(BKCONTROL_CONFIG_CANCEL,NULL,0)==0);
  read_header(1,1); /* Local staging cancel is not remote trial cancel. */
  assert(bkdisplay_intent_step(&service,true));read_header(3,1);
  now_ms=500;open_session();assert(apply(start)==0); /* Retry cannot move original 1100 deadline. */
  const uint8_t read_tail[4]={0,11,0,16};
  assert(send(BKCONTROL_CONFIG_READ,read_tail,4)==0);
  const uint8_t tail[16]={0,0,0,0,0,0,2,88,1,2,3,4,5,6,7,8};
  assert(!memcmp(reply+24,tail,16));
  uint8_t changed[32];memcpy(changed,start,32);changed[27]=3;
  assert(apply(changed)==-EEXIST);
  assert(apply(cancel)==0);read_header(4,1); /* Accepted, not restored. */
  assert(!strcmp(service.status.expression,"happy"));
  assert(bkdisplay_trial_step(&service,true));read_header(7,1);
  assert(!strcmp(service.status.expression,"neutral"));
  assert(apply(cancel)==0);
  memcpy(changed,start,32);changed[11]=1;changed[23]=10;
  assert(apply(changed)==0);read_header(1,2);
  assert(apply(cancel)==-ESTALE); /* Old exact cancel must not affect new trial. */
  assert(bkdisplay_intent_step(&service,true));
  now_ms=1500;assert(bkdisplay_trial_step(&service,true));read_header(6,2);
  assert(apply(changed)==0 && !bkdisplay_intent_step(&service,true));
  uint8_t invalid[32];memcpy(invalid,changed,32);invalid[11]=2;invalid[23]=11;
  invalid[31]=1;assert(apply(invalid)==-EINVAL);
  invalid[31]=0;invalid[27]=10;assert(apply(invalid)==-EINVAL);
  invalid[27]=2;memset(invalid+16,0,8);assert(apply(invalid)==-EINVAL);
  read_header(6,2);
  uint32_t external_id=0;
  assert(bk7258_display_trial_checked("sad",100,1,&external_id)==-ESTALE);
  assert(bk7258_display_trial_checked("sad",100,2,&external_id)==0 && external_id==3);
  assert(apply(changed)==-ESTALE); /* Old receipt cannot describe a new local trial. */
  assert(send(BKCONTROL_CONFIG_READ,read_tail,4)==0);
  const uint8_t no_operation[8]={0};assert(!memcmp(reply+32,no_operation,8));
  assert(bk7258_display_cancel_trial(external_id)==0);
  assert(!bkdisplay_intent_step(&service,true));
  seq--; /* Old authenticated sequence must not reach the config handler. */
  assert(send(BKCONTROL_CONFIG_READ,read_tail,4)==-EPROTO);
  open_session();read_header(7,3);
  assert(bkcontrol_session_quiesce(&session)==0);
  read_header(7,3);
  assert(send(BKCONTROL_CONFIG_BEGIN,begin,8)==-EBUSY);
  bkdisplay_intent_supersede();assert(!bkdisplay_intent_pending());
  puts("CONTRACT_PASS trial authenticated wire");
}
