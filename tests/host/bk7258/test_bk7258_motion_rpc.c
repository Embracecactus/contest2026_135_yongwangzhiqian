/* SPDX-License-Identifier: Apache-2.0 */
/* Consumer: make -C tests/host/bk7258 run-motion-rpc.
 * The target generates isolated NuttX header shims under its build directory.
 */
#include "bk7258_sensor_rpc_test.h"
#include "bk7258_motion_core.c"
#define bkmotion_device_destroy bkmotion_client_destroy
#include "bk7258_motion_client.c"
#undef bkmotion_device_destroy
#include "bk7258_motion_service.c"

static struct rpmsg_device ap = { "ap" }, cp = { "cp" };
static struct bkmotion_rpc_response_s last_wire;
static struct bkmotion_rpc_request_s queued;
static bool reconnect_on_send;
static void client_reconnect(void);

static void drain_worker(void)
{
  assert(!in_worker);
  in_worker = true;
  if (setjmp(worker_idle) == 0) worker_entry(0, NULL);
  in_worker = false;
  assert(!fd_live);
}
static int deliver(struct bkmotion_rpc_request_s *request)
{
  int ret;
  callback_depth++;
  ret = bkmotion_server_cb(&g_bkmotion_server.endpoint, request,
                         sizeof(*request), 0, &g_bkmotion_server);
  callback_depth--;
  return ret;
}
static int rpmsg_trysend(struct rpmsg_endpoint *e, const void *data, int len)
{
  if (e == &g_bkmotion_client.endpoint)
    {
      struct bkmotion_rpc_request_s request;
      assert(len == sizeof(request));
      requests_sent++;
      if (reconnect_on_send)
        { reconnect_on_send = false; unlock_hook = client_reconnect; }
      if (no_buffers)
        { if (no_buffers > 0) no_buffers--; return RPMSG_ERR_NO_BUFF; }
      memcpy(&request, data, sizeof(request));
      return deliver(&request);
    }
  assert(e == &g_bkmotion_server.endpoint && len == sizeof(last_wire));
  memcpy(&last_wire, data, sizeof(last_wire));
  responses_sent++;
  if (!drop_reply)
    {
      callback_depth++;
      (void)bkmotion_client_cb(&g_bkmotion_client.endpoint, &last_wire,
                            sizeof(last_wire), 0, &g_bkmotion_client);
      callback_depth--;
    }
  return len;
}
static void client_reconnect(void)
{
  bkmotion_client_destroy(&ap, &g_bkmotion_client);
  bkmotion_device_created(&ap, &g_bkmotion_client);
  assert(g_bkmotion_client.connection_error == 0);
}
static void server_reconnect(void)
{
  bkmotion_device_destroy(&cp, &g_bkmotion_server);
  bkmotion_ns_bind(&cp, &g_bkmotion_server, BKMOTION_RPC_ENDPOINT, 1);
  assert(!g_bkmotion_server.active && !g_bkmotion_server.replay_valid);
}
static struct bkmotion_rpc_request_s request(unsigned int sequence)
{
  struct bkmotion_rpc_request_s r = {0};
  r.magic = BKMOTION_RPC_MAGIC;
  r.version = BKMOTION_RPC_VERSION;
  r.command = BKMOTION_RPC_SAMPLE;
  r.session = 19;
  r.sequence = sequence;
  return r;
}
static void reset_case(void)
{
  assert(!fd_live && !in_worker);
  server_reconnect();
  client_reconnect();
  drain_worker(); /* Discard any old pending token. */
  opens = reads = closes = requests_sent = responses_sent = 0;
  open_error = read_error = close_error = ioctl_error = no_buffers = 0;
  drop_reply = reconnect_on_send = false;
  unlock_hook = sleep_hook = wait_hook = read_hook = NULL;
  memset(&last_wire, 0, sizeof(last_wire));
}
static int exchange(void)
{
  struct bkmotion_rpc_request_s r = request(1);
  struct bkmotion_rpc_response_s reply = {0};
  int ret = bkmotion_rpc_exchange(&r, &reply, 20);
  if (ret == 0)
    {
      assert(bkmotion_rpc_response_valid(&reply));
      assert(reply.operation_status == 0);
    }
  return ret;
}
static void test_client_backpressure(void)
{
  reset_case();
  no_buffers = 2;
  wait_hook = drain_worker;
  assert(exchange() == 0);
  assert(requests_sent == 3 && reads == 1 && closes == 1);
  passes++;

  reset_case();
  no_buffers = -1;
  clock_t start = ticks;
  assert(exchange() == -ETIMEDOUT);
  assert(ticks - start == BKMOTION_RPC_SEND_WAIT_MS);
  assert(reads == 0 && requests_sent > 1);
  passes++;
}
/* Deliver a late matching reply after fast reconnect.  The old exchange
 * must reject it even though reply_valid is true when the waiter resumes.
 */
static void cached_reply_then_reconnect(void)
{
  drain_worker();
  assert(g_bkmotion_client.reply_valid);
  client_reconnect();
  assert(bkmotion_client_cb(&g_bkmotion_client.endpoint, &last_wire,
                          sizeof(last_wire), 0, &g_bkmotion_client) == 0);
  assert(g_bkmotion_client.reply_valid);
}
static void client_ns_reconnect(void)
{
  struct rpmsg_endpoint *e = &g_bkmotion_client.endpoint;
  assert(e->unbind != NULL);
  e->ready = false; /* OpenAMP invalidates the destination on NS_DESTROY. */
  e->unbind(e);
  assert(g_bkmotion_client.endpoint_created && e->rdev == &ap);
  e->ready = true; /* NS_CREATE binds the existing endpoint again. */
}
static void server_ns_reconnect(void)
{
  struct rpmsg_endpoint *e = &g_bkmotion_server.endpoint;
  assert(e->unbind != NULL);
  e->ready = false;
  e->unbind(e);
  assert(!g_bkmotion_server.endpoint_created);
  assert(!g_bkmotion_server.active && !g_bkmotion_server.replay_valid);
  bkmotion_ns_bind(&cp, &g_bkmotion_server, BKMOTION_RPC_ENDPOINT, 1);
}
static void test_client_reconnect(void)
{
  reset_case();
  no_buffers = -1;
  reconnect_on_send = true;
  assert(exchange() == -ENOTCONN);
  assert(requests_sent == 1 && reads == 0);
  passes++;

  reset_case();
  wait_hook = client_reconnect;
  assert(exchange() == -ENOTCONN);
  assert(requests_sent == 1);
  drain_worker();
  assert(closes == 1);
  passes++;

  reset_case();
  wait_hook = cached_reply_then_reconnect;
  assert(exchange() == -ENOTCONN);
  assert(requests_sent == 1 && closes == 1);
  passes++;

  /* Disconnect exactly after successful send, before waiting. */
  reset_case();
  reconnect_on_send = true;
  assert(exchange() == -ENOTCONN);
  assert(requests_sent == 1);
  drain_worker();
  passes++;
}
static void replay_while_reading(void)
{
  int before = responses_sent;
  assert(deliver(&queued) == 0);
  assert(responses_sent == before); /* active duplicate does not enqueue */
  struct bkmotion_rpc_request_s other = request(queued.sequence + 1);
  (void)deliver(&other);
  assert(last_wire.rpc_status == -EBUSY);
}
static void test_replay_and_busy(void)
{
  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  read_hook = replay_while_reading;
  drain_worker();
  assert(reads == 1 && closes == 1);
  int before = responses_sent;
  assert(deliver(&queued) >= 0);
  assert(responses_sent == before + 1 && reads == 1);
  assert(last_wire.operation_status == 0);
  passes++;

  /* Cached data from a previous connection must never be replayed. */
  server_reconnect();
  assert(deliver(&queued) == 0);
  assert(responses_sent == before + 1);
  drain_worker();
  assert(reads == 2 && closes == 2);
  passes++;
}
static void reconnect_and_queue(void)
{
  server_reconnect();
  queued = request(2);
  assert(deliver(&queued) == 0);
}
static void test_old_worker_and_tokens(void)
{
  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  read_hook = reconnect_and_queue;
  drain_worker();
  assert(reads == 2 && closes == 2);
  assert(responses_sent == 1 && last_wire.sequence == 2);
  assert(g_bkmotion_server.last_request.sequence == 2);
  assert(!g_bkmotion_server.active && !g_bkmotion_server.pending);
  passes++;

  /* Old request never started: old and new tokens must execute new slot once. */
  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  reconnect_and_queue();
  assert(g_bkmotion_server.request_sem == 2);
  drain_worker();
  assert(reads == 1 && closes == 1 && responses_sent == 1);
  assert(last_wire.sequence == 2 && g_bkmotion_server.request_sem == 0);
  passes++;

  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  server_reconnect();
  drain_worker();
  assert(opens == 0 && responses_sent == 0);
  passes++;

  /* A reconnect after completion but before send invalidates that response. */
  reset_case();
  struct bkmotion_rpc_response_s reply;
  bkmotion_rpc_make_response(&reply, &queued, -EBUSY);
  uint32_t old_epoch = g_bkmotion_server.epoch;
  server_reconnect();
  assert(bkmotion_send(&g_bkmotion_server, &reply, old_epoch) == -ENOTCONN);
  assert(responses_sent == 0);
  passes++;
}
static void ns_reconnect_and_queue(void)
{
  server_ns_reconnect();
  queued = request(2);
  assert(deliver(&queued) == 0);
}
static void test_namespace_rebind(void)
{
  reset_case();
  wait_hook = client_ns_reconnect;
  assert(exchange() == -ENOTCONN);
  assert(requests_sent == 1);
  drain_worker();
  assert(g_bkmotion_client.connection_error == -ENOTCONN);
  wait_hook = drain_worker;
  assert(exchange() == 0);
  assert(g_bkmotion_client.connection_error == 0);
  assert(reads == 2 && closes == 2);
  passes++;

  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  read_hook = ns_reconnect_and_queue;
  drain_worker();
  assert(reads == 2 && closes == 2 && responses_sent == 1);
  assert(last_wire.sequence == 2);
  passes++;

  int before = responses_sent;
  server_ns_reconnect();
  assert(deliver(&queued) == 0);
  assert(responses_sent == before); /* cached reply invalidated by unbind */
  drain_worker();
  assert(reads == 3 && closes == 3 && responses_sent == before + 1);
  passes++;

  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  ns_reconnect_and_queue();
  drain_worker();
  assert(reads == 1 && closes == 1 && responses_sent == 1);
  passes++;
}
static void test_errors_close(void)
{
  reset_case();
  queued = request(1);
  read_error = EIO;
  assert(deliver(&queued) == 0);
  drain_worker();
  assert(last_wire.operation_status == -EIO && closes == 1);
  passes++;

  reset_case();
  open_error = ENOENT;
  assert(deliver(&queued) == 0);
  drain_worker();
  assert(last_wire.operation_status == -ENOENT && closes == 0);
  passes++;

  reset_case();
  close_error = EIO;
  assert(deliver(&queued) == 0);
  drain_worker();
  assert(last_wire.operation_status == -EIO && closes == 1);
  passes++;
}
/* MOT-01: local callers must receive a sanitized error, not a retained
 * successful sample, when the owner has not initialized. */
static void test_local_sample_unavailable(void)
{
  struct bkmotion_rpc_response_s sample;
  reset_case();
  in_worker = true; /* Agent worker context, outside RPMsg callback. */
  assert(bk7258_motion_service_sample(&sample) == 0);
  in_worker = false;
  assert(bkmotion_rpc_response_valid(&sample));
  assert(sample.flags == BKMOTION_FLAG_SAMPLE_VALID);
  int before = opens;
  g_bkmotion_server.initialized = false;
  assert(bk7258_motion_service_sample(&sample) == -ENODEV);
  assert(sample.rpc_status == -ENODEV);
  assert(sample.operation_status == -ENODEV);
  assert(sample.flags == 0 && sample.timestamp_us == 0);
  assert(sample.x_mms2 == 0 && sample.y_mms2 == 0 && sample.z_mms2 == 0);
  assert(sample.sensor_status == 0 && bkmotion_rpc_response_valid(&sample));
  assert(opens == before && !fd_live);
  assert(bk7258_motion_service_sample(NULL) == -EINVAL);
  g_bkmotion_server.initialized = true;
  in_worker = true; /* Agent worker context, outside RPMsg callback. */
  assert(bk7258_motion_service_sample(&sample) == 0);
  in_worker = false;
  assert(sample.flags == BKMOTION_FLAG_SAMPLE_VALID && opens == before + 1);
  passes++;
}

int main(void)
{
  assert(bkmotion_rpc_client_initialize() == 0);
  assert(bk7258_motion_service_start() == 0);
  bkmotion_device_created(&ap, &g_bkmotion_client);
  bkmotion_ns_bind(&cp, &g_bkmotion_server, BKMOTION_RPC_ENDPOINT, 1);
  test_client_backpressure();
  test_client_reconnect();
  test_replay_and_busy();
  test_old_worker_and_tokens();
  test_errors_close();
  test_namespace_rebind();
  test_local_sample_unavailable();
  printf("BKMOTION_RPC_HOST_PASS cases=%u\n", passes);
  return 0;
}
