/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_focus.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static struct bkcontrol_session_s session;
static uint32_t seq;
static uint8_t response[BKCONTROL_RESPONSE_SIZE];
static void put(uint8_t *p,uint32_t n)
{p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static uint32_t get(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int execute(void *c,enum bkcontrol_command_e cmd,uint32_t n,struct bkcontrol_status_s *s)
{(void)c;(void)cmd;(void)n;(void)s;return 0;}
static int config(void *c,enum bkcontrol_command_e cmd,uint32_t kind,uint32_t offset,const uint8_t *p,size_t n,struct bkcontrol_status_s *s)
{(void)c;return kind==BKCONTROL_CONFIG_FOCUS ? bkfocus_control(cmd,offset,p,n,s,1000) : -ENOTSUP;}
static int send(enum bkcontrol_command_e cmd,const uint8_t *p,size_t n)
{
 uint8_t frame[80]={0};memcpy(frame,"SDC1",4);put(frame+4,cmd);put(frame+8,seq);put(frame+12,n);
 if(n)memcpy(frame+16,p,n);
 int r=bkcontrol_session_packet(&session,frame,16+n,response);
 if(r)return r;
 assert(get(response+8)==seq++);
 return (int32_t)get(response+16);
}
int main(void)
{
 uint8_t key[32]={1},head[8]={0},record[32]={'F','O','C','1'};
 assert(bkcontrol_session_open(&session,key,execute,NULL)==0);
 assert(bkcontrol_session_set_config_handler(&session,config)==0);
 put(head,10u<<16);
 assert(send(BKCONTROL_CONFIG_READ,head,4)==-EPROTO);
 assert(bkcontrol_session_open(&session,key,execute,NULL)==0);seq=0;
 assert(bkcontrol_session_set_config_handler(&session,config)==0);
 assert(send(BKCONTROL_AUTH,key,32)==0);
 put(head,10);put(head+4,32);
 assert(send(BKCONTROL_CONFIG_BEGIN,head,8)==0);
 record[7]=1;record[23]=1;put(record+28,60000);
 assert(send(BKCONTROL_CONFIG_APPEND,record,32)==0);
 assert(send(BKCONTROL_CONFIG_APPLY,NULL,0)==0);
 put(head,10u<<16);
 assert(send(BKCONTROL_CONFIG_READ,head,4)==0);
 assert(!memcmp(response+24,"FOS1",4) && response[31]==1);
 assert(bkcontrol_session_quiesce(&session)==0);
 assert(send(BKCONTROL_CONFIG_READ,head,4)==0);
 put(head,10);put(head+4,32);
 assert(send(BKCONTROL_CONFIG_BEGIN,head,8)==-EBUSY);
 bkfocus_cancel();assert(bkfocus_step(100000)==0);
 puts("CONTRACT_PASS");return 0;
}
