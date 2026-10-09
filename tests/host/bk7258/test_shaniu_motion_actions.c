/* SPDX-License-Identifier: Apache-2.0 */
/* Candidate timing/sequence contract; vectors are synthetic, not gesture data. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_motion_core.h"
static struct bkmotion_actions_s state;
static struct bkmotion_actions_config_s config = {
 .move_mms2=3000,.quiet_mms2=500,.gravity_min_mms2=8000,.gravity_max_mms2=12000,
 .tilt_enter_cos=800,.tilt_leave_cos=950,.settle_us=400000,.cooldown_us=200000,
 .max_gap_us=200000
};
static int feed(uint64_t t,int x,int y,int z,bool admitted,enum bkmotion_action_e *event)
{
 struct bkmotion_rpc_response_s s={.magic=BKMOTION_RPC_MAGIC,.version=1,
 .command=BKMOTION_RPC_RESPONSE,.session=1,.sequence=1,
 .flags=BKMOTION_FLAG_SAMPLE_VALID,.timestamp_us=t,.x_mms2=x,.y_mms2=y,.z_mms2=z};
 return bkmotion_actions_step(&state,&config,&s,admitted,event);
}
int main(int argc,char **argv)
{
 assert(argc==2);enum bkmotion_action_e event;
 assert(feed(100000,0,0,10000,true,&event)==0 && event==BKMOTION_ACTION_NONE);
 if(!strcmp(argv[1],"move-settle")) {
  assert(feed(200000,4000,0,10000,true,&event)==0 && event==BKMOTION_ACTION_MOVED);
  for(uint64_t t=300000;t<600000;t+=100000)assert(feed(t,4000,0,10000,true,&event)==0 && !event);
  assert(feed(600000,4000,0,10000,true,&event)==0 && event==BKMOTION_ACTION_SETTLED);
  assert(feed(800000,4000,0,10000,true,&event)==0 && !event);
 } else if(!strcmp(argv[1],"tilt")) {
  config.move_mms2=20000;
  for(uint64_t t=200000;t<600000;t+=100000)assert(feed(t,7000,0,7000,true,&event)==0 && !event);
  assert(feed(600000,7000,0,7000,true,&event)==0 && event==BKMOTION_ACTION_TILTED);
  for(uint64_t t=700000;t<1500000;t+=100000)assert(feed(t,7000,0,7000,true,&event)==0 && !event);
  for(uint64_t t=1500000;t<=1900000;t+=100000)assert(feed(t,0,0,10000,true,&event)==0 && !event);
  for(uint64_t t=2000000;t<2400000;t+=100000)assert(feed(t,7000,0,7000,true,&event)==0 && !event);
  assert(feed(2400000,7000,0,7000,true,&event)==0 && event==BKMOTION_ACTION_TILTED);
 } else if(!strcmp(argv[1],"freshness")) {
  assert(feed(100000,20000,0,0,true,&event)==-ESTALE && !event);
  assert(feed(90000,20000,0,0,true,&event)==-ESTALE && !event);
  assert(feed(100000,0,0,10000,true,&event)==0 && !event);
  assert(feed(1000000,20000,0,0,true,&event)==0 && !event);
 } else if(!strcmp(argv[1],"gate")) {
  assert(feed(200000,4000,0,10000,false,&event)==0 && !event);
  assert(feed(300000,4000,0,10000,true,&event)==0 && !event);
  assert(feed(400000,8000,0,10000,true,&event)==0 && event==BKMOTION_ACTION_MOVED);
 } else if(!strcmp(argv[1],"error")) {
  struct bkmotion_rpc_response_s s={0};
  assert(bkmotion_actions_step(&state,&config,&s,true,&event)<0 && !event);
  assert(feed(200000,4000,0,10000,true,&event)==0 && !event);
  config.quiet_mms2=config.move_mms2;
  assert(feed(300000,9000,0,0,true,&event)==-EINVAL && !event);
 } else if(!strcmp(argv[1],"cooldown")) {
  config.cooldown_us=2000000;
  assert(feed(200000,4000,0,10000,true,&event)==0 && event==BKMOTION_ACTION_MOVED);
  for(uint64_t t=300000;t<=1500000;t+=100000)assert(feed(t,4000,0,10000,true,&event)==0 && !event);
  assert(feed(1600000,0,0,10000,true,&event)==0 && !event);
  for(uint64_t t=1700000;t<=3000000;t+=100000)assert(feed(t,0,0,10000,true,&event)==0 && !event);
 } else if(!strcmp(argv[1],"creep")) {
  assert(feed(200000,4000,0,10000,true,&event)==0 && event==BKMOTION_ACTION_MOVED);
  for(uint64_t t=300000;t<=1200000;t+=100000)assert(feed(t,4000+(int)(t/100000-2)*300,0,10000,true,&event)==0 && !event);
 } else if(!strcmp(argv[1],"gate-cooldown")) {
  config.cooldown_us=2000000;
  assert(feed(200000,4000,0,10000,true,&event)==0 && event==BKMOTION_ACTION_MOVED);
  assert(feed(300000,4000,0,10000,false,&event)==0 && !event);
  assert(feed(400000,4000,0,10000,true,&event)==0 && !event);
  assert(feed(500000,8000,0,10000,true,&event)==0 && !event);
  assert(feed(600000,0,0,10000,false,&event)==0 && !event);
  assert(feed(100000,0,0,10000,true,&event)==0 && !event);
  assert(feed(200000,4000,0,10000,true,&event)==0 && !event);
 } else if(!strcmp(argv[1],"extreme")) {
  assert(feed(200000,INT32_MAX,INT32_MIN,INT32_MAX,true,&event)==0);
  assert(event==BKMOTION_ACTION_MOVED);
  for(uint64_t t=300000;t<=900000;t+=100000) {
   assert(feed(t,INT32_MAX,INT32_MIN,INT32_MAX,true,&event)==0);
   assert(event!=BKMOTION_ACTION_TILTED);
  }
 } else assert(0);
 puts("CONTRACT_PASS");return 0;
}
