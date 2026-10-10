/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_pc_tasks.h"
#include "bk7258_focus.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static unsigned char request[40];
static void put(unsigned char *p, uint64_t v, int n)
{ while(n) { p[--n]=(unsigned char)v; v>>=8; } }
static int send(struct bkpc_tasks_s *s, unsigned state, uint64_t sequence,
                unsigned ttl, uint64_t now)
{
  memset(request,0,sizeof(request));memcpy(request,"PTE1",4);
  put(request+4,state,4);request[8]=7;put(request+24,sequence,8);
  put(request+32,ttl,4);put(request+36,state==1?0:50,4);
  return bkpc_tasks_apply(s,request,sizeof(request),now);
}
int main(int argc,char **argv)
{
  assert(argc==2);
  struct bkpc_tasks_s s={0};
  bkpc_tasks_bind(&s,1,1);
  assert(send(&s,1,1,1000,100)==0);
  if(!strcmp(argv[1],"feedback"))
    {
      assert(!bkpc_tasks_take_completion(&s,100));
      assert(send(&s,3,2,1000,200)==0);
      assert(bkpc_tasks_take_completion(&s,200));
      assert(!bkpc_tasks_take_completion(&s,201));
      assert(send(&s,3,2,1000,202)==0);
      assert(!bkpc_tasks_take_completion(&s,202));
      bkpc_tasks_bind(&s,1,2);
      assert(send(&s,1,1,1000,300)==0);
      assert(send(&s,4,2,1000,400)==0);
      assert(!bkpc_tasks_take_completion(&s,1400));
      assert(!bkpc_tasks_take_completion(&s,1401));
      bkpc_tasks_bind(&s,1,3);
      assert(send(&s,1,1,1000,1500)==0);
      assert(send(&s,5,2,1000,1600)==0);
      bkpc_tasks_step(&s,1601,false);
      assert(!bkpc_tasks_take_completion(&s,1601));
    }
  else if(!strcmp(argv[1],"focus-completion"))
    {
      /* The real timer keeps its completed fact, but no longer owns the
       * display ahead of a valid finite task result. Voice always wins.
       */
      struct bkfocus_request_s timer={.action=1,.operation=1,.duration_ms=10};
      struct bkfocus_snapshot_s snapshot;
      assert(bkfocus_execute(&timer,100)==0);
      assert(bkfocus_step(110)==1);
      unsigned done=bkfocus_visual(110);
      assert((done>>8)==3);
      assert(send(&s,3,2,1000,200)==0);
      assert(bkpc_tasks_visual(&s,201,true,done)==(4u<<8));
      assert(bkpc_tasks_visual(&s,202,false,done)==0);
      assert(bkpc_tasks_visual(&s,1199,true,done)==(4u<<8));
      assert(bkpc_tasks_visual(&s,1200,false,done)==0);
      assert(bkpc_tasks_visual(&s,1201,true,done)==done);
      assert(bkfocus_snapshot(&snapshot,1201)==0 && snapshot.state==3);
    }
  else if(!strcmp(argv[1],"visual"))
    {
      assert(bkpc_tasks_visual(&s,100,true,0)==0);
      assert(send(&s,3,2,1000,200)==0);
      assert(bkpc_tasks_visual(&s,201,true,0)==(4u<<8));
      assert(bkpc_tasks_visual(&s,201,false,0)==0);
      assert(bkpc_tasks_visual(&s,201,true,0x110)==0x110);
      assert(bkpc_tasks_visual(&s,1200,true,0)==0);
      assert(bkpc_tasks_visual(&s,199,true,0)==0);
      bkpc_tasks_step(&s,202,false);
      assert(bkpc_tasks_visual(&s,203,true,0)==0);
      for(unsigned result=4;result<=5;result++)
        {
          bkpc_tasks_bind(&s,1,result);
          assert(send(&s,1,1,1000,100)==0);
          assert(send(&s,result,2,1000,200)==0);
          assert(bkpc_tasks_visual(&s,201,true,0)==((result+1)<<8));
        }
    }
  else if(!strcmp(argv[1],"terminal"))
    {
      assert(send(&s,2,2,1000,150)==0);
      assert(send(&s,3,3,1000,200)==0);
      assert(send(&s,2,4,1000,250)==-EALREADY);
      unsigned char view[48];bkpc_tasks_snapshot(&s,view,250);
      assert(view[7]==3 && view[31]==3);
    }
  else if(!strcmp(argv[1],"duplicate"))
    {
      assert(send(&s,3,2,1000,200)==0);
      assert(bkpc_tasks_apply(&s,request,40,500)==0);
      unsigned char view[48];bkpc_tasks_snapshot(&s,view,1199);
      assert(view[39]==1); /* 1 ms, not a renewed deadline. */
      bkpc_tasks_step(&s,1200,true);
      assert(send(&s,3,2,1000,1201)==0);
      bkpc_tasks_snapshot(&s,view,1201);
      assert(view[7]==3 && (view[43]&2));
    }
  else if(!strcmp(argv[1],"ordering"))
    {
      assert(send(&s,2,1,1000,101)==-EEXIST);
      assert(send(&s,2,2,1000,102)==0);
      assert(send(&s,2,1,1000,103)==-ESTALE);
      request[8]=9;put(request+24,3,8);
      assert(bkpc_tasks_apply(&s,request,40,104)==-ENOENT);
    }
  else if(!strcmp(argv[1],"expiry"))
    {
      bkpc_tasks_step(&s,1100,true);
      assert(send(&s,2,2,1000,1101)==-ETIMEDOUT);
      request[8]=9;put(request+4,1,4);put(request+36,0,4);
      assert(bkpc_tasks_apply(&s,request,40,1102)==0);
      assert(send(&s,2,3,1000,1103)==-ENOENT);
    }
  else if(!strcmp(argv[1],"binding"))
    {
      assert(send(&s,3,2,1000,200)==0);
      bkpc_tasks_bind(&s,1,2);
      unsigned char view[48];bkpc_tasks_snapshot(&s,view,201);
      assert(view[7]==0 && view[31]==0);
      assert(send(&s,1,1,1000,202)==0);
      bkpc_tasks_bind(&s,0,0);
      assert(send(&s,3,2,1000,203)==-EACCES);
    }
  else if(!strcmp(argv[1],"readonly"))
    {
      struct bkpc_tasks_s before=s;
      unsigned char view[48];
      for(int i=0;i<100;i++)bkpc_tasks_snapshot(&s,view,2000);
      assert(!memcmp(&before,&s,sizeof(s)));
      bkpc_tasks_step(&s,99,true);
      assert(send(&s,2,2,1000,100)==-ETIMEDOUT);
    }
  else if(!strcmp(argv[1],"rate"))
    {
      assert(send(&s,2,2,3000,150)==0);
      assert(send(&s,2,3,3000,1149)==-EAGAIN);
      assert(send(&s,2,3,3000,1150)==0);
      assert(send(&s,4,4,3000,1151)==0);
    }
  else if(!strcmp(argv[1],"invalid"))
    {
      struct bkpc_tasks_s before=s;
      assert(send(&s,0,2,1000,102)==-EINVAL);
      assert(send(&s,2,2,0,102)==-EINVAL);
      assert(send(&s,2,2,1000,UINT64_MAX)==-EOVERFLOW);
      assert(!memcmp(&before,&s,sizeof(s)));
    }
  else { assert(!strcmp(argv[1],"quiesce"));
      bkpc_tasks_step(&s,101,false);
      assert(send(&s,3,2,1000,102)==-ESHUTDOWN);
      bkpc_tasks_step(&s,103,true);
      assert(send(&s,3,2,1000,104)==-ETIMEDOUT);
    }
  puts("CONTRACT_PASS");return 0;
}
