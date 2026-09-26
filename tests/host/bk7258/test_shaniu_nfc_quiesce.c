/* SPDX-License-Identifier: Apache-2.0 */
#define TEST_NFC_RF 1
#define main existing_nfc_rpc_main
#include "test_bk7258_nfc_rpc.c"
#undef main
static int close_at_stop;
static void stop_during_read(void)
{
  assert(fd_live && rf_on);
  close_at_stop=closes;
  assert(bk7258_nfc_service_quiesce(true) == -EBUSY);
  assert(bk7258_nfc_service_quiesce(false) == -EBUSY);
  assert(bk7258_nfc_service_retry_stop() == -EBUSY);
}
int main(int argc, char **argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "prestart"))
    {
      assert(bk7258_nfc_service_quiesce(true) == 0);
      assert(bk7258_nfc_service_start() == -ESHUTDOWN);
      assert(!worker_entry && !opens);
      assert(bk7258_nfc_service_quiesce(false) == 0);
    }
  assert(bknfc_rpc_client_initialize() == 0);
  assert(bk7258_nfc_service_start() == 0);
  bknfc_device_created(&ap, &g_bknfc_client);
  bknfc_ns_bind(&cp, &g_bknfc_server, BKNFC_RPC_ENDPOINT, 1);
  reset_case();
  struct bknfc_rpc_request_s r=request(1);
  if (!strcmp(argv[1], "queued"))
    {
      assert(deliver(&r)==0);
      assert(bk7258_nfc_service_quiesce(true)==0);
      drain_worker();
      assert(nfc_selects==0 && opens==0 && !rf_on);
    }
  else if (!strcmp(argv[1], "late"))
    {
      assert(deliver(&r)==0);
      assert(bk7258_nfc_service_quiesce(true)==0);
      drain_worker();
      assert(bk7258_nfc_service_quiesce(false)==0);
      assert(deliver(&r)>=0); drain_worker();
      assert(last_wire.operation_status==-ECANCELED && nfc_selects==0);
    }
  else if (!strcmp(argv[1], "active"))
    {
      assert(deliver(&r)==0); read_hook=stop_during_read;
      drain_worker();
      fprintf(stderr,"active selects=%d closes=%d rf=%d\n",nfc_selects,closes,rf_on);
      assert(nfc_selects==1 && closes==close_at_stop+1 && !rf_on);
      assert(responses_sent==0);
      assert(bk7258_nfc_service_quiesce(true)==0);
    }
  else if (!strcmp(argv[1], "close-error") || !strcmp(argv[1], "rf-error"))
    {
      if (!strcmp(argv[1],"close-error")) close_error=EIO;
      else rf_off_error=EIO;
      assert(deliver(&r)==0);drain_worker();
      assert(last_wire.operation_status == -EIO);
      assert(bk7258_nfc_service_quiesce(true)==-EIO);
      assert(bk7258_nfc_service_quiesce(false)==-EIO);
      close_error=rf_off_error=0;
      assert(bk7258_nfc_service_retry_stop()==0);
      assert(bk7258_nfc_service_quiesce(true)==-EBUSY);
      drain_worker();
      assert(bk7258_nfc_service_quiesce(true)==0 && !rf_on);
    }
  else assert(!strcmp(argv[1],"prestart"));
  assert(bk7258_nfc_service_quiesce(true)==0);
  int before=nfc_selects, before_open=opens;
  r=request(2); assert(deliver(&r)>=0);
  assert(last_wire.operation_status==-ESHUTDOWN);
  drain_worker();
  assert(nfc_selects==before && opens==before_open);
  assert(bk7258_nfc_service_quiesce(false)==0);
  r=request(3);assert(deliver(&r)==0);drain_worker();
  assert(last_wire.operation_status==0 && nfc_selects==before+1 && !rf_on);
  puts("CONTRACT_PASS"); return 0;
}
