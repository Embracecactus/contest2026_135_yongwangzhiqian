/* SPDX-License-Identifier: Apache-2.0 */
#define NFC_JOBS_NO_MAIN 1
#include "test_shaniu_nfc_jobs.c"
#include "bk7258_nfc_control.h"
static struct bkcontrol_session_s control;
static uint32_t sequence;
static uint8_t response[BKCONTROL_RESPONSE_SIZE];
static void wire32(uint8_t *p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static uint32_t value32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int execute(void *c,enum bkcontrol_command_e cmd,uint32_t arg,struct bkcontrol_status_s *s)
{(void)c;(void)cmd;(void)arg;(void)s;return 0;}
static int config(void *c,enum bkcontrol_command_e cmd,uint32_t kind,uint32_t off,const uint8_t *p,size_t n,struct bkcontrol_status_s *s)
{(void)c;return kind==BKCONTROL_CONFIG_NFC_BINDINGS ? bknfc_control(cmd,off,p,n,s) : -ENOTSUP;}
static int packet(unsigned cmd,const void *data,size_t size)
{
 uint8_t bytes[100]={0};memcpy(bytes,"SDC1",4);wire32(bytes+4,cmd);wire32(bytes+8,sequence);wire32(bytes+12,size);
 if(size)memcpy(bytes+16,data,size);
 int r=bkcontrol_session_packet(&control,bytes,16+size,response);
 if(r)return r;
 assert(value32(response+8)==sequence++);return (int32_t)value32(response+16);
}
static void start(bool auth)
{
 uint8_t key[32];memset(key,0x42,32);sequence=0;
 assert(bkcontrol_session_open(&control,key,execute,NULL)==0);
 assert(bkcontrol_session_set_config_handler(&control,config)==0);
 if(auth)assert(packet(BKCONTROL_AUTH,key,32)==0);
}
static int apply(uint8_t record[40])
{
 uint8_t begin[8];wire32(begin,BKCONTROL_CONFIG_NFC_BINDINGS);wire32(begin+4,40);
 int r=packet(BKCONTROL_CONFIG_BEGIN,begin,8);if(r)return r;
 assert(packet(BKCONTROL_CONFIG_APPEND,record,20)==0);
 assert(packet(BKCONTROL_CONFIG_APPEND,record+20,20)==0);
 return packet(BKCONTROL_CONFIG_APPLY,NULL,0);
}
int main(int argc,char **argv)
{
 assert(argc==2);char parent[]="/tmp/shaniu-nfc-wire-XXXXXX";assert(mkdtemp(parent));
 snprintf(job_root,sizeof(job_root),"%s/cards",parent);assert(bk7258_nfc_service_start()==0);
 if(!strcmp(argv[1],"floor"))
   {
    struct bknfc_bindings_s saved={0};struct bknfc_card_s card={.size=4,.uid={1,2,3,4}};
    assert(mkdir(job_root,0700)==0 && bknfc_bindings_open(&saved,job_root)==0);
    assert(bknfc_bindings_set(&saved,0,50,0,&card,60000)==0);
   }
 uint8_t readarg[4];wire32(readarg,BKCONTROL_CONFIG_NFC_BINDINGS<<16);
 if(!strcmp(argv[1],"auth"))
   {
    start(false);assert(packet(BKCONTROL_CONFIG_READ,readarg,4)<0);
    assert(access(job_root,F_OK)<0 && !opens);
   }
 start(true);assert(packet(BKCONTROL_CONFIG_READ,readarg,4)==0);
 assert(!opens && (!strcmp(argv[1],"floor") || access(job_root,F_OK)<0));
 assert(value32(response+20)==112);
 const uint8_t empty_header[16]={'N','C','S','1'};
 assert(!memcmp(response+24,empty_header,16));
 if(!strcmp(argv[1],"sequence"))
   {
    sequence--;assert(packet(BKCONTROL_CONFIG_READ,readarg,4)<0);assert(!opens);start(true);
   }
 uint8_t record[40]={'N','C','F','1'};record[7]=1;record[31]=1;
 assert(apply(record)==0);
 struct bknfc_job_status_s status;bk7258_nfc_job_status(&status);assert(status.phase==BKNFC_JOB_PENDING);
 drain_worker();bk7258_nfc_job_status(&status);assert(status.phase==BKNFC_JOB_SUCCEEDED);
 if(!strcmp(argv[1],"floor"))
   {
    assert(status.operation_floor==50 && status.revision==1);
    record[31]=2;assert(apply(record)==-ESTALE);
    assert(bk7258_nfc_service_quiesce(true)==0 && bk7258_nfc_bindings_reset()==0);
    assert(rmdir(job_root)==0 && rmdir(parent)==0);puts("CONTRACT_PASS");return 0;
   }
 record[7]=2;record[31]=2;record[38]=0xea;record[39]=0x60;
 if(!strcmp(argv[1],"staging"))
   {
    uint8_t begin[8];wire32(begin,BKCONTROL_CONFIG_NFC_BINDINGS);wire32(begin+4,40);
    assert(packet(BKCONTROL_CONFIG_BEGIN,begin,8)==0);
    assert(packet(BKCONTROL_CONFIG_APPEND,record,20)==0);
    bkcontrol_session_close(&control);drain_worker();assert(!nfc_selects);
    start(true);assert(packet(BKCONTROL_CONFIG_APPLY,NULL,0)==-EBUSY);
   }
 if(!strcmp(argv[1],"invalid"))
   {
    record[15]=1;assert(apply(record)==-EINVAL);record[15]=0;
    wire32(readarg,(BKCONTROL_CONFIG_NFC_BINDINGS<<16)|1);assert(packet(BKCONTROL_CONFIG_READ,readarg,4)==-ERANGE);
    wire32(readarg,BKCONTROL_CONFIG_NFC_BINDINGS<<16);
   }
 assert(apply(record)==0);
 if(!strcmp(argv[1],"cancel"))
   {
    memset(record,0,sizeof(record));memcpy(record,"NCF1",4);record[7]=4;record[31]=2;
    assert(apply(record)==0);drain_worker();bk7258_nfc_job_status(&status);
    assert(status.phase==BKNFC_JOB_CANCELED && status.revision==0);
   }
 else
   {
    bkcontrol_session_close(&control);drain_worker();start(true);
    assert(packet(BKCONTROL_CONFIG_READ,readarg,4)==0);
    bk7258_nfc_job_status(&status);assert(status.phase==BKNFC_JOB_SUCCEEDED && status.revision==1);
    assert(status.operation_floor>=2);
    uint8_t expected[16]={0};expected[7]=2;expected[15]=1;
    wire32(readarg,(BKCONTROL_CONFIG_NFC_BINDINGS<<16)|16);
    assert(packet(BKCONTROL_CONFIG_READ,readarg,4)==0 && !memcmp(response+24,expected,16));
    memset(expected,0,16);expected[6]=0xea;expected[7]=0x60;
    wire32(readarg,(BKCONTROL_CONFIG_NFC_BINDINGS<<16)|48);
    assert(packet(BKCONTROL_CONFIG_READ,readarg,4)==0 && !memcmp(response+24,expected,16));
    wire32(readarg,BKCONTROL_CONFIG_NFC_BINDINGS<<16);
    if(!strcmp(argv[1],"quiesce"))
      {
       assert(bkcontrol_session_quiesce(&control)==0);
       assert(packet(BKCONTROL_CONFIG_READ,readarg,4)==0);
       assert(apply(record)==-EBUSY);
      }
   }
 assert(bk7258_nfc_service_quiesce(true)==0 && bk7258_nfc_bindings_reset()==0);
 assert(rmdir(job_root)==0 && rmdir(parent)==0);puts("CONTRACT_PASS");return 0;
}
