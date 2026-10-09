/* SPDX-License-Identifier: Apache-2.0 */
#define CONFIG_BK7258_APP_AGENT 1
#define TEST_NFC_SCENE 1
#define NFC_JOBS_NO_MAIN
#include "test_shaniu_nfc_jobs.c"
#include "bk7258_focus_intent.h"
#include "bk7258_nfc_control.c"
static void inspect_capability(void)
{
 struct bknfc_scene_status_s scene;struct bknfc_job_status_s job;
 bk7258_nfc_scene_status(&scene);bk7258_nfc_job_status(&job);
 assert((scene.flags & 8)!=0 && (scene.flags & 4)==0 && job.revision==0);
}
static void fail_release(void) { rf_off_error=EIO; }
static void revoke(void) { bk7258_nfc_scene_admit(false); }
static void stop_scene(void) { assert(bk7258_nfc_service_quiesce(true)==-EBUSY); }
static bool revoke_load;
ssize_t __real_read(int fd, void *buffer, size_t size);
ssize_t __wrap_read(int fd, void *buffer, size_t size)
{
 ssize_t result=__real_read(fd,buffer,size);
 if(revoke_load){revoke_load=false;bk7258_nfc_scene_admit(false);}
 return result;
}
static void poll(void) { worker_timeouts=1;drain_worker(); }
static void timer(unsigned state,uint64_t remaining,uint64_t now)
{
 struct bkfocus_snapshot_s s;assert(bkfocus_snapshot(&s,now)==0);
 assert(s.state==state && s.remaining_ms==remaining);
}
int main(int argc,char **argv)
{
 assert(argc==2);char parent[]="/tmp/shaniu-worker-scene-XXXXXX";assert(mkdtemp(parent));
 snprintf(job_root,sizeof(job_root),"%s/cards",parent);assert(mkdir(job_root,0700)==0);
 struct bknfc_bindings_s saved={0};assert(bknfc_bindings_open(&saved,job_root)==0);
 struct bknfc_card_s card={.size=4,.uid={0xa5,0xa5,0xa5,0xa5}};
 if(strcmp(argv[1],"first-enroll"))assert(bknfc_bindings_set(&saved,0,1,0,&card,60000)==0);
 assert(bk7258_nfc_service_start()==0);poll();assert(nfc_observations==0);
 bkfocus_intent_step(1000,true);bk7258_nfc_scene_admit(true);
 if(!strcmp(argv[1],"capability-inflight"))read_hook=inspect_capability;
 if(!strcmp(argv[1],"load-revoke"))revoke_load=true;
 if(!strcmp(argv[1],"revoke"))read_hook=revoke;
 if(!strcmp(argv[1],"stop"))read_hook=stop_scene;
 if(!strcmp(argv[1],"release"))read_hook=fail_release;
 if(!strcmp(argv[1],"error"))read_error=ETIMEDOUT;
 poll();assert(!fd_live);
 assert(nfc_observations==(!strcmp(argv[1],"load-revoke")?0:1));
 struct bkfocus_intent_status_s intent;bkfocus_intent_status(&intent);
 if(!strcmp(argv[1],"capability") || !strcmp(argv[1],"capability-inflight"))
  {
   struct bkcontrol_status_s status={0};int before=opens;
   assert(bknfc_scene_control(BKCONTROL_CONFIG_READ,0,&status)==0);
   const uint8_t expected[16]={'N','C','A','1',0,0,0,1,0,0,0,7,0,0,0,0};
   assert(status.config_total==16 && !memcmp(status.config_chunk,expected,16));
   bk7258_nfc_scene_admit(false);
   assert(bknfc_scene_control(BKCONTROL_CONFIG_READ,0,&status)==0 && status.config_chunk[11]==5);
   assert(bknfc_scene_control(BKCONTROL_CONFIG_READ,16,&status)==-ERANGE);
   assert(bknfc_scene_control(BKCONTROL_CONFIG_APPLY,0,&status)==-EPERM);
   assert(opens==before);goto cleanup;
  }
 if(!strcmp(argv[1],"first-enroll"))
  {
   assert(intent.phase==0);
   struct bknfc_job_request_s req={.operation=1,.action=BKNFC_JOB_ENROLL,.duration_ms=60000};
   assert(bk7258_nfc_job_submit(&req)==0);drain_worker();
   struct bknfc_job_status_s status;bk7258_nfc_job_status(&status);
   assert(status.phase==BKNFC_JOB_SUCCEEDED && status.revision==1);
   bk7258_nfc_scene_admit(true);poll();bkfocus_intent_status(&intent);assert(intent.phase==0);
   nfc_present=0;poll();nfc_present=1;poll();bkfocus_intent_status(&intent);assert(intent.phase==1);
   bkfocus_intent_step(2000,true);timer(1,60000,2000);
   goto cleanup;
  }

 if(!strcmp(argv[1],"load-revoke")||!strcmp(argv[1],"revoke")||!strcmp(argv[1],"stop")||!strcmp(argv[1],"release")||!strcmp(argv[1],"error"))
  {
   assert(intent.phase==0);timer(0,0,2000);
   if(!strcmp(argv[1],"release"))
    {assert(bk7258_nfc_service_quiesce(true)==-EIO);rf_off_error=0;assert(bk7258_nfc_service_retry_stop()==0);drain_worker();}
   assert(bk7258_nfc_service_quiesce(false)==0);
   read_error=0;bk7258_nfc_scene_admit(true);
   if(!strcmp(argv[1],"load-revoke")||!strcmp(argv[1],"revoke")||!strcmp(argv[1],"stop"))
    {poll();bkfocus_intent_status(&intent);assert(intent.phase==0);}
   nfc_present=0;poll();nfc_present=1;poll();bkfocus_intent_step(3000,true);timer(1,60000,3000);
  }
 else
  {
   assert(intent.phase==1 && intent.id!=0);timer(0,0,1000);
   if(!strcmp(argv[1],"pending-cancel"))
    {bk7258_nfc_scene_admit(false);bkfocus_intent_step(2000,true);timer(0,0,2000);}
   else
    {
     bkfocus_intent_step(2000,true);timer(1,60000,2000);uint32_t first=intent.id;
     for(int i=0;i<5;i++){poll();}bkfocus_intent_status(&intent);assert(intent.id==first);
     if(!strcmp(argv[1],"unknown"))
      {read_error=ETIMEDOUT;poll();read_error=0;poll();bkfocus_intent_status(&intent);assert(intent.id==first);}
     else if(!strcmp(argv[1],"enroll"))
      {
       struct bknfc_job_status_s status;bk7258_nfc_job_status(&status);
       assert(status.revision==1 && status.operation_floor>=1);
       struct bknfc_job_request_s req={.operation=status.operation_floor+1,.revision=1,.action=BKNFC_JOB_ENROLL,.duration_ms=90000};
       assert(bk7258_nfc_job_submit(&req)==0);int reads=nfc_observations;drain_worker();assert(nfc_observations==reads);
       /* 产品owner每轮重新发布准入；模拟该真实外部事件。 */
       bk7258_nfc_scene_admit(true);
       poll();bkfocus_intent_status(&intent);assert(intent.id==first);
      }
     else assert(!strcmp(argv[1],"dwell"));
     nfc_present=0;poll();nfc_present=1;poll();bkfocus_intent_step(12000,true);
     bkfocus_intent_status(&intent);assert(intent.id==first+1 && intent.error==-EBUSY);timer(1,50000,12000);
    }
  }
cleanup:
 bk7258_nfc_scene_admit(false);assert(bk7258_nfc_service_quiesce(true)==0);
 assert(bk7258_nfc_bindings_reset()==0);assert(rmdir(job_root)==0 && rmdir(parent)==0);
 puts("CONTRACT_PASS");
}
