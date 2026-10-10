/* SPDX-License-Identifier: Apache-2.0 */
/* Real motion worker and collector. Only sensor/scheduler boundaries are fake. */
#define TEST_MOTION_POLL
#define MOTION_RPC_NO_MAIN
#include "test_bk7258_motion_rpc.c"

static void close_poll_during_read(void)
{
  assert(bk7258_motion_service_poll(false) == 0);
  assert(bk7258_motion_service_poll(true) == 0);

}

static void drain_poll_worker(void)
{
  assert(!in_worker);
  in_worker = true;
  if (setjmp(worker_idle) == 0) worker_entry(0, NULL);
  in_worker = false;
}

int main(int argc, char **argv)
{
  struct bkmotion_rpc_response_s sample;
  assert(argc == 2);
  assert(bk7258_motion_service_start() == 0);
  assert(bk7258_motion_service_snapshot(&sample) == -ENODATA);
  assert(opens == 0 && reads == 0);
  assert(bk7258_motion_service_poll(true) == 0);
  if (!strncmp(argv[1], "reuse", 5))
    {
      worker_timeouts = 3;
      drain_poll_worker();
      assert(opens == 1 && reads == 3 && closes == 0 && fd_live);
      bool shutdown = !strcmp(argv[1], "reuse-quiesce");
      bool fault = !strcmp(argv[1], "reuse-close-error");
      if (fault) close_error = EIO;
      if (shutdown)
        {
          assert(bk7258_motion_service_quiesce(true) == -EBUSY);
          assert(bk7258_motion_service_quiesce(false) == -EBUSY);
        }
      else assert(bk7258_motion_service_poll(false) == 0);
      assert(closes == 0); /* caller does not wait for peripheral I/O */
      drain_poll_worker();
      assert(opens == 1 && reads == 3 && closes == 1 && !fd_live);
      assert(bk7258_motion_service_snapshot(&sample) == -ENODATA);
      assert(bk7258_motion_service_quiesce(true) == (fault ? -EIO : 0));
      assert(bk7258_motion_service_quiesce(false) == (fault ? -EIO : 0));
      if (!fault)
        {
          assert(bk7258_motion_service_poll(true) == 0);
          worker_timeouts = 1;
          drain_poll_worker();
          assert(opens == 2 && reads == 4 && closes == 1 && fd_live);
          assert(bk7258_motion_service_poll(false) == 0);
          drain_poll_worker();
          assert(opens == 2 && closes == 2 && !fd_live);
        }
      puts("CONTRACT_PASS");
      return 0;
    }

  if (!strcmp(argv[1], "late")) read_hook = close_poll_during_read;
  if (!strcmp(argv[1], "read-error")) read_error = EIO;
  worker_timeouts = 1;
  drain_poll_worker();
  bool released = read_error || !strcmp(argv[1], "late");
  assert(opens == 1 && closes == (released ? 1 : 0));
  assert(fd_live == !released);
  struct bkmotion_metrics_s timing;
  assert(bk7258_motion_service_metrics(NULL) == -EINVAL);
  assert(bk7258_motion_service_metrics(&timing) == 0);
  assert(timing.collections == 1 && timing.open_us == 12000);
  assert(timing.read_us == 0 && timing.close_us == 0);
  assert(timing.total_us == 12000);
  assert(opens == 1 && reads == 1 && closes == (released ? 1 : 0));

  if (!strcmp(argv[1], "late"))
    assert(bk7258_motion_service_snapshot(&sample) == -ENODATA);
  else
    {
      assert(bk7258_motion_service_snapshot(&sample) == 0);
      if (read_error) assert(sample.operation_status == -EIO && !sample.flags);
      else assert(sample.flags && sample.timestamp_us == 123456);
    }

  for (unsigned int i = 0; i < 20; i++)
    (void)bk7258_motion_service_snapshot(&sample);
  assert(opens == 1);
  bkmotion_ns_bind(&cp, &g_bkmotion_server, BKMOTION_RPC_ENDPOINT, 1);
  struct bkmotion_rpc_request_s status = request(10);
  status.command = BKMOTION_RPC_STATUS;
  assert(deliver(&status) == sizeof(last_wire));
  assert(opens == 1 && last_wire.sequence == 10);
  assert(bkmotion_rpc_handle_request(&status, &sample, &g_bkmotion_ops,
                                    &g_bkmotion_server.source) == -ENOTSUP);
  assert(opens == 1);
  if (!strcmp(argv[1], "quiesce"))
    {
      assert(bk7258_motion_service_quiesce(true) == -EBUSY);
      assert(bk7258_motion_service_poll(true) == -ESHUTDOWN);
    }
  else assert(bk7258_motion_service_poll(false) == 0);
  assert(bk7258_motion_service_snapshot(&sample) == -ENODATA);
  assert(!sample.flags && !sample.timestamp_us);
  worker_timeouts = 2;
  drain_poll_worker();
  assert(opens == 1 && closes == 1 && !fd_live);
  assert(bk7258_motion_service_quiesce(true) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
