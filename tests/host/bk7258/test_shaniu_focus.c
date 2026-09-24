/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_focus.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static uint64_t get64(const uint8_t *p)
{ uint64_t n=0; for(int i=0;i<8;i++) n=(n<<8)|p[i]; return n; }
static void put64(uint8_t *p,uint64_t n)
{ for(int i=7;i>=0;i--) {p[i]=n;n>>=8;} }
static uint8_t request[32] = {'F','O','C','1'};
static struct bkcontrol_status_s status;
static int command(unsigned action,uint64_t revision,uint64_t id,uint64_t ms,uint64_t now)
{
 memset(request+4,0,28); request[7]=action;
 put64(request+8,revision);put64(request+16,id);put64(request+24,ms);
 return bkfocus_control(BKCONTROL_CONFIG_APPLY,0,request,32,&status,now);
}
static uint64_t snapshot(unsigned state,uint64_t remain,uint64_t now)
{
 assert(bkfocus_control(BKCONTROL_CONFIG_READ,0,NULL,0,&status,now)==0);
 assert(status.config_total==32 && !memcmp(status.config_chunk,"FOS1",4));
 assert(status.config_chunk[7]==state);
 uint64_t revision=get64(status.config_chunk+8);
 assert(bkfocus_control(BKCONTROL_CONFIG_READ,16,NULL,0,&status,now)==0);
 assert(get64(status.config_chunk)==remain);
 return revision;
}
int main(int argc,char **argv)
{
 assert(argc==2);
 assert(snapshot(0,0,0)==0);
 assert(bkfocus_control(BKCONTROL_CONFIG_BEGIN,0,NULL,32,&status,0)==0);
 assert(snapshot(0,0,0)==0);
 assert(command(1,0,1,60000,1000)==0);
 if(!strcmp(argv[1],"clock"))
 {
  assert(snapshot(1,50000,11000)==1);
  assert(bkfocus_visual(11000)==((1u<<8)|5));
  assert(command(2,1,2,0,11000)==0);
  assert(snapshot(2,50000,16000)==2);
  assert(bkfocus_visual(16000)==((2u<<8)|5));
  assert(command(3,2,3,0,16000)==0);
  assert(bkfocus_step(65999)==0);
  assert(bkfocus_step(66000)==1);
  assert(bkfocus_step(66000)==0 && bkfocus_step(99000)==0);
  assert(snapshot(3,0,99000)==4);
  assert(bkfocus_visual(99000)==((3u<<8)|32));
 }
 else if(!strcmp(argv[1],"replay"))
 {
  assert(command(1,0,1,60000,5000)==0);
  assert(snapshot(1,56000,5000)==1);
  assert(command(1,0,2,60000,5000)==-ESTALE);
  assert(command(2,1,2,0,5000)==0);
  assert(command(3,2,3,0,6000)==0);
  assert(command(2,1,2,0,7000)==-ESTALE);
  assert(command(4,3,4,0,7000)==0);
  assert(bkfocus_step(100000)==0);
  assert(snapshot(4,0,100000)==4);
  assert(command(1,0,1,60000,100000)==-ESTALE);
 }
 else
 {
  assert(!strcmp(argv[1],"invalid"));
  assert(command(2,1,2,0,999)==-EAGAIN);
  assert(command(1,1,2,0,1000)==-EINVAL);
  assert(command(4,1,2,0,1000)==0);
  assert(command(1,2,3,UINT64_MAX,2000)==-EOVERFLOW);
  assert(snapshot(4,0,2000)==2);
  assert(command(1,2,3,60000,2000)==0);
  bkfocus_cancel();
  assert(bkfocus_visual(100000)==0);
  assert(bkfocus_step(100000)==0);
  assert(snapshot(4,0,100000)==4);
 }
 puts("CONTRACT_PASS"); return 0;
}
