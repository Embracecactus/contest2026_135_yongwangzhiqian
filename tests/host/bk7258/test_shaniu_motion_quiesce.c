/* SPDX-License-Identifier: Apache-2.0 */
/* Real service/collector/RPC; external scheduler and sensor descriptor only. */
#define MOTION_RPC_NO_MAIN
#include "test_bk7258_motion_rpc.c"
static void stop_during_read(void)
{
  assert(bk7258_motion_service_quiesce(true)==-EBUSY);
  assert(bk7258_motion_service_quiesce(false)==-EBUSY);
}
static void cycle_admission(void)
{
 assert(bk7258_motion_service_quiesce(true)==0);
 assert(bk7258_motion_service_quiesce(false)==0);
}
int main(int argc,char **argv)
{
 assert(argc==2);struct bkmotion_rpc_response_s sample;
 assert(bk7258_motion_service_quiesce(true)==0);
 if(!strcmp(argv[1],"prestart")) {
  assert(bk7258_motion_service_start()==0);
  assert(bk7258_motion_service_sample(&sample)<0 && !opens);
 } else {
  assert(bk7258_motion_service_start()==0);
  assert(bk7258_motion_service_quiesce(false)==0);
  if(!strcmp(argv[1],"publication-cycle")) {
   bkmotion_ns_bind(&cp,&g_bkmotion_server,BKMOTION_RPC_ENDPOINT,1);
   struct bkmotion_rpc_request_s r=request(1);assert(deliver(&r)==0);
   unlock_hook=cycle_admission;drain_worker();
   assert(opens==1 && closes==1 && last_wire.rpc_status==-ECANCELED);
   assert(!last_wire.flags && !last_wire.timestamp_us);
   assert(deliver(&r)==sizeof(last_wire));
   assert(last_wire.rpc_status==-ECANCELED && opens==1);
  } else if(!strcmp(argv[1],"queued-cycle") || !strcmp(argv[1],"waiter-cycle")) {
   bool queued_cycle=!strcmp(argv[1],"queued-cycle");
   if(queued_cycle) {
    bkmotion_ns_bind(&cp,&g_bkmotion_server,BKMOTION_RPC_ENDPOINT,1);
    struct bkmotion_rpc_request_s r=request(1);assert(deliver(&r)==0);
    cycle_admission();drain_worker();
    assert(last_wire.rpc_status==-ECANCELED && !last_wire.flags && !opens);
    assert(deliver(&r)==sizeof(last_wire));
    assert(last_wire.rpc_status==-ECANCELED && !opens);
   } else {
    lock_hook=cycle_admission;in_worker=true;
    assert(bk7258_motion_service_sample(&sample)==-ECANCELED);
    in_worker=false;assert(!sample.flags && !sample.timestamp_us && !opens);
   }
   in_worker=true;assert(bk7258_motion_service_sample(&sample)==0);in_worker=false;
   assert(sample.flags && opens==1 && closes==1);
  } else if(!strcmp(argv[1],"queued") || !strcmp(argv[1],"late")) {
   bkmotion_ns_bind(&cp,&g_bkmotion_server,BKMOTION_RPC_ENDPOINT,1);
   struct bkmotion_rpc_request_s r=request(1);assert(deliver(&r)==0);
   bool late=!strcmp(argv[1],"late");
   if(late) {drain_worker();assert(opens==1 && last_wire.flags);}
   assert(bk7258_motion_service_quiesce(true)==0);
   if(late)assert(deliver(&r)==sizeof(last_wire));
   drain_worker();
   assert(opens==(late?1:0) && last_wire.operation_status<0 && !last_wire.flags);
  } else {
   in_worker=true;
   if(!strcmp(argv[1],"active"))read_hook=stop_during_read;
   else if(!strcmp(argv[1],"close-error"))close_error=EIO;
   else if(!strcmp(argv[1],"open-cleanup")) {ioctl_error=EINVAL;close_error=EIO;}
   else assert(!strcmp(argv[1],"idle"));
   int ret=bk7258_motion_service_sample(&sample);in_worker=false;
   assert(closes==1 && !fd_live);
   if(!strcmp(argv[1],"idle"))assert(ret==0 && sample.flags);
   else assert(ret<0 && !sample.flags && sample.timestamp_us==0);
   bool fault=close_error!=0;
   assert(bk7258_motion_service_quiesce(true)==(fault?-EIO:0));
   int before=opens;
   assert(bk7258_motion_service_sample(&sample)<0 && !sample.flags && opens==before);
   assert(bk7258_motion_service_quiesce(false)==(fault?-EIO:0));
   if(!fault) {
    in_worker=true;assert(bk7258_motion_service_sample(&sample)==0);in_worker=false;
    assert(opens==before+1 && sample.flags);
   }
  }
 }
 puts("CONTRACT_PASS");return 0;
}
