/* SPDX-License-Identifier: Apache-2.0 */
#define main storage_fixture_main
#include "test_pc_storage.c"
#undef main
#include "bk7258_pc_authorization.h"
static struct bkcontrol_session_s session;
static uint32_t sequence;
static uint8_t response[BKCONTROL_RESPONSE_SIZE];
static uint64_t config_revision = 1;
static void put32(uint8_t *p, uint32_t v)
{ for (int i=3;i>=0;i--) {p[i]=v;v>>=8;} }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static int execute(void *c,enum bkcontrol_command_e cmd,uint32_t v,struct bkcontrol_status_s *s)
{ (void)c;(void)cmd;(void)v;(void)s;return -ENOTSUP; }
static int dispatch(void *c,enum bkcontrol_command_e cmd,uint32_t kind,uint32_t off,const uint8_t *p,size_t n,struct bkcontrol_status_s *s)
{ (void)c;return kind==14 ? bkpc_authorization_control(config_revision,cmd,off,p,n,s) : -ENOTSUP; }
static int packet(uint32_t cmd,const void *p,size_t n)
{
 uint8_t frame[128]={'S','D','C','1'};
 assert(n<=112);put32(frame+4,cmd);put32(frame+8,sequence);put32(frame+12,n);
 if(n)memcpy(frame+16,p,n);
 int r=bkcontrol_session_packet(&session,frame,n+16,response);
 if(r)return r;
 assert(get32(response+8)==sequence++);return (int32_t)get32(response+16);
}
static void connect(bool auth)
{
 bkcontrol_session_close(&session);sequence=0;
 assert(bkcontrol_session_open(&session,phone,execute,NULL)==0);
 assert(bkcontrol_session_set_config_handler(&session,dispatch)==0);
 if(auth)assert(packet(BKCONTROL_AUTH,phone,32)==0);
}
/* Independent literal protocol vector: config revision=1, PC revision=0,
 * transaction=1, client=33, key=44, resources+scenes=3. */
static const uint8_t grant[88]={ [0]='P',[1]='C',[2]='W',[3]='1',
 [11]=1,[20]=1,[36]=33,[52]=44,[87]=3 };
static int begin(void)
{ const uint8_t p[8]={0,0,0,14,0,0,0,88};return packet(BKCONTROL_CONFIG_BEGIN,p,8); }
static int apply(const uint8_t *p)
{
 assert(begin()==0);
 assert(packet(BKCONTROL_CONFIG_APPEND,p,44)==0);
 assert(packet(BKCONTROL_CONFIG_APPEND,p+44,44)==0);
 return packet(BKCONTROL_CONFIG_APPLY,NULL,0);
}
static int query(const uint8_t transaction[16])
{
 uint8_t p[20]={0,14};memcpy(p+4,transaction,16);
 int ret=packet(BKCONTROL_CONFIG_READ,p,20);
 if(ret)return ret;
 assert(get32(response+20)==32 && !memcmp(response+24,"PCR1",4));
 return (int32_t)get32(response+28);
}
static int wait_result(const uint8_t transaction[16])
{
 int ret=-EAGAIN;
 for(int i=0;i<3000 && ret==-EAGAIN;i++) {ret=query(transaction);if(ret==-EAGAIN)tick();}
 return ret;
}
int main(int argc,char **argv)
{
 assert(argc==3);
 assert(bkprov_storage_start(argv[2])==0 && receipt(config_tx)==0);
 assert(bkprov_storage_commit(0,config_tx,"owner fixture",13)==-EAGAIN);
 assert(receipt(config_tx)==1 && load(1,phone)==0);
 connect(true);
 const uint8_t read[4]={0,14};
 if(!strcmp(argv[1],"auth"))
  { connect(false);assert(begin()<0);connect(true);sequence--;assert(begin()<0);connect(true); }
 if(!strcmp(argv[1],"invalid"))
  {
   uint8_t bad[88];memcpy(bad,grant,88);bad[87]=32; /* First unknown bit after explicit camera=16. */
   assert(apply(bad)==-EINVAL);memcpy(bad,grant,88);bad[11]=2;
   assert(apply(bad)==-ESTALE);memcpy(bad,grant,88);bad[20]=0;
   assert(apply(bad)==-EINVAL);assert(query(tx)==-ENODATA);
  }
 if(!strcmp(argv[1],"cancel"))
  {
   assert(begin()==0 && packet(BKCONTROL_CONFIG_APPEND,grant,88)==0);
   assert(packet(BKCONTROL_CONFIG_CANCEL,NULL,0)==0);
   assert(query(tx)==-ENODATA);
  }
 atomic_store(&failure,!strcmp(argv[1],"failure")?1:!strcmp(argv[1],"unknown")?2:!strcmp(argv[1],"retry-error")?3:0);
 if(!strcmp(argv[1],"pending"))
  { pthread_mutex_lock(&lock);blocked=true;pthread_mutex_unlock(&lock); }
 int accepted=apply(grant);assert(accepted==-EAGAIN);
 if(!strcmp(argv[1],"pending"))
  {
   bool ready=false;
   for(int i=0;i<3000 && !ready;i++)
    { pthread_mutex_lock(&lock);ready=entered;pthread_mutex_unlock(&lock);if(!ready)tick(); }
   assert(ready && query(tx)==-EAGAIN && get32(response+32)==1);
   assert(packet(BKCONTROL_CONFIG_READ,read,4)==-EAGAIN);
   assert(bkcontrol_session_quiesce(&session)==0);
   assert(query(tx)==-EAGAIN && begin()==-EBUSY);
   assert(packet(BKCONTROL_CONFIG_CANCEL,NULL,0)==0 && query(tx)==-EAGAIN);
   pthread_mutex_lock(&lock);blocked=false;pthread_cond_signal(&wake);pthread_mutex_unlock(&lock);
  }
 /* Even local cancel/close after submission cannot claim durable cancellation. */
 assert(packet(BKCONTROL_CONFIG_CANCEL,NULL,0)==0);connect(true);
 if(!strcmp(argv[1],"retry-error"))
  {
   /* EAGAIN from the storage syscall is a completed failure, not pending. */
   int phase=0;
   for(int i=0;i<3000 && phase!=3;i++)
    { assert(query(tx)==-EAGAIN);phase=get32(response+32);if(phase!=3)tick(); }
   assert(phase==3);atomic_store(&failure,0);
   assert(query(tx)==-EAGAIN && get32(response+32)==3);
   bkcontrol_session_close(&session);assert(bkprov_storage_stop()==0);
   puts("CONTRACT_PASS");return 0;
  }
 int result=wait_result(tx);
 assert(result==(!strcmp(argv[1],"failure")?-EIO:!strcmp(argv[1],"unknown")?-EINPROGRESS:0));
 assert(get32(response+32)==(result==-EINPROGRESS?4:result?3:2));
 atomic_store(&failure,0);
 if(result)
  {
   assert(query(tx)==result);
   if(result==-EIO)assert(apply(grant)==-EIO);
  }
 else
  {
   uint8_t snapshot[64];
   for(unsigned i=0;i<64;i+=16)
    { uint8_t part[4]={0,14,0,i};assert(packet(BKCONTROL_CONFIG_READ,part,4)==0);memcpy(snapshot+i,response+24,16); }
   const uint8_t expected[64]={ [0]='P',[1]='C',[2]='S',[3]='1',[7]=1,[15]=1,[23]=1,[27]=3,[32]=33,[48]=1 };
   assert(!memcmp(snapshot,expected,64));
   assert(query(tx)==0);
   if(!strcmp(argv[1],"reopen"))
    {
     assert(bkprov_storage_stop()==0);
     assert(bkprov_storage_start(argv[2])==0 && receipt(config_tx)==1);
     assert(load(1,phone)==0 && query(tx)==0);
    }
   uint8_t revoke[88]={ 'P','C','W','1' };revoke[11]=1;revoke[19]=1;revoke[20]=2;
   assert(apply(revoke)==-EAGAIN && wait_result(next)==0);
   assert(bkprov_storage_pc_snapshot(1,(struct bkprov_pc_snapshot_s[1]){{0}})==0);
   assert(packet(BKCONTROL_CONFIG_READ,read,4)==0);
   assert(get32(response+28)==0);
  }
 bkcontrol_session_close(&session);
 if(result!=-EINPROGRESS)assert(bkprov_storage_stop()==0);
 puts("CONTRACT_PASS");return 0;
}
