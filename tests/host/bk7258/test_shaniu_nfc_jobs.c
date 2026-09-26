/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/stat.h>
#include "bk7258_nfc_bindings.h"
static char job_root[192];
#undef BKNFC_BINDINGS_ROOT
#define BKNFC_BINDINGS_ROOT job_root
#define CONFIG_BK7258_PROVISION_GATT 1
#define TEST_NFC_RF 1
#define main existing_nfc_rpc_main
#include "test_bk7258_nfc_rpc.c"
#undef main
#undef open
#undef read
#undef close
#undef ioctl
static int sync_mode;
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  struct stat info; assert(fstat(fd,&info)==0);
  if(sync_mode==1 && S_ISREG(info.st_mode))
    {
      struct bknfc_job_status_s status; bk7258_nfc_job_status(&status);
      assert(status.phase==BKNFC_JOB_COMMITTING);
      assert(bk7258_nfc_job_cancel(status.operation)==-EALREADY);
      assert(bk7258_nfc_service_quiesce(true)==-EBUSY);
    }
  if(sync_mode==2 && S_ISDIR(info.st_mode)){errno=EIO;return -1;}
  return __real_fsync(fd);
}
static void cancel_read(void)
{
  struct bknfc_job_status_s s;
  bk7258_nfc_job_status(&s);
  assert(s.phase==BKNFC_JOB_RUNNING);
  assert(bk7258_nfc_job_cancel(s.operation)==0);
  bk7258_nfc_job_status(&s);
  assert(s.phase==BKNFC_JOB_RUNNING);
}
static void stop_read(void)
{
  assert(bk7258_nfc_service_quiesce(true)==-EBUSY);
}

#ifndef NFC_JOBS_NO_MAIN
int main(int argc,char **argv)
{
  assert(argc==2);
  char parent[]="/tmp/shaniu-jobs-XXXXXX"; assert(mkdtemp(parent));
  snprintf(job_root,sizeof(job_root),"%s/cards",parent);
  assert(bk7258_nfc_service_start()==0);
  struct bknfc_job_request_s req={.operation=1,.action=BKNFC_JOB_LOAD};
  struct bknfc_job_status_s status;
  bk7258_nfc_job_status(&status);
  assert(!status.operation && access(job_root,F_OK)<0 && !opens);
  struct bknfc_job_request_s bad=req;bad.action=99;
  assert(bk7258_nfc_job_submit(&bad)==-EINVAL);
  bad=req;bad.operation=0;assert(bk7258_nfc_job_submit(&bad)==-EINVAL);
  assert(access(job_root,F_OK)<0 && !opens);
  assert(bk7258_nfc_job_submit(&req)==0);drain_worker();
  bk7258_nfc_job_status(&status);
  assert(status.phase==BKNFC_JOB_SUCCEEDED && status.revision==0);
  req=(struct bknfc_job_request_s){.operation=2,.action=BKNFC_JOB_ENROLL,.duration_ms=60000};
  if(!strcmp(argv[1],"cancel")) read_hook=cancel_read;
  if(!strcmp(argv[1],"stop")) read_hook=stop_read;
  if(!strcmp(argv[1],"failure")) read_error=EIO;
  if(!strcmp(argv[1],"commit")) sync_mode=1;
  if(!strcmp(argv[1],"unknown")) sync_mode=2;
  assert(bk7258_nfc_job_submit(&req)==0);
  if(!strcmp(argv[1],"conflict"))
    {
      bknfc_ns_bind(&cp,&g_bknfc_server,BKNFC_RPC_ENDPOINT,1);
      struct bknfc_rpc_request_s rpc=request(90);
      assert(deliver(&rpc)>=0 && last_wire.operation_status==-EBUSY);
      struct bknfc_job_request_s newer=req;newer.operation++;
      assert(bk7258_nfc_job_submit(&newer)==-EBUSY);
    }
  if(!strcmp(argv[1],"pending")) assert(bk7258_nfc_job_cancel(2)==0);
  drain_worker();bk7258_nfc_job_status(&status);
  if(!strcmp(argv[1],"cancel") || !strcmp(argv[1],"stop") || !strcmp(argv[1],"pending"))
    assert(status.phase==BKNFC_JOB_CANCELED && status.revision==0);
  else if(!strcmp(argv[1],"unknown"))
    assert(status.phase==BKNFC_JOB_UNKNOWN && status.durations[0]==0);
  else if(!strcmp(argv[1],"failure"))
    assert(status.phase==BKNFC_JOB_FAILED && status.revision==0);
  else
    {
      assert(!strcmp(argv[1],"persist") || !strcmp(argv[1],"reset") ||
             !strcmp(argv[1],"commit") || !strcmp(argv[1],"remove") || !strcmp(argv[1],"conflict"));
      assert(status.phase==BKNFC_JOB_SUCCEEDED && status.revision==1 && status.durations[0]==60000);
      int scans=nfc_selects;
      if(!strcmp(argv[1],"commit")) assert(bk7258_nfc_service_quiesce(false)==0);
      assert(bk7258_nfc_job_submit(&req)==0);drain_worker();assert(nfc_selects==scans);
      req.duration_ms=90000;assert(bk7258_nfc_job_submit(&req)==-EEXIST);
      req.operation=1;assert(bk7258_nfc_job_submit(&req)==-ESTALE);
      assert(bk7258_nfc_job_cancel(2)==-EALREADY);
    }
  sync_mode=0;
  if(!strcmp(argv[1],"remove"))
    {
      req=(struct bknfc_job_request_s){.operation=3,.revision=1,.action=BKNFC_JOB_REMOVE};
      assert(bk7258_nfc_job_submit(&req)==0);drain_worker();
      bk7258_nfc_job_status(&status);assert(status.phase==BKNFC_JOB_SUCCEEDED && status.revision==2 && !status.durations[0]);
    }
  assert(bk7258_nfc_service_quiesce(true)==0);
  if(!strcmp(argv[1],"reset"))
    {
      sync_mode=2;assert(bk7258_nfc_bindings_reset()==-EINPROGRESS);
      bk7258_nfc_job_status(&status);assert(status.phase==BKNFC_JOB_UNKNOWN && !status.durations[0]);
      sync_mode=0;
    }
  assert(bk7258_nfc_bindings_reset()==0);
  bk7258_nfc_job_status(&status);assert(status.revision==0 && status.durations[0]==0);
  assert(bk7258_nfc_service_quiesce(false)==0);
  req=(struct bknfc_job_request_s){.operation=4,.action=BKNFC_JOB_LOAD};
  assert(bk7258_nfc_job_submit(&req)==0);drain_worker();
  bk7258_nfc_job_status(&status);assert(status.phase==BKNFC_JOB_SUCCEEDED && status.revision==0);
  assert(rmdir(job_root)==0 && rmdir(parent)==0);
  puts("CONTRACT_PASS");return 0;
}

#endif
