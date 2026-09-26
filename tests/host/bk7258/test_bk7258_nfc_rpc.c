/* SPDX-License-Identifier: Apache-2.0 */
/* Consumer: make -C tests/host/bk7258 run-nfc-rpc.
 * The target generates isolated NuttX header shims under its build directory.
 */
#include "bk7258_sensor_rpc_test.h"
#include "bk7258_nfc_core.c"
#define bknfc_device_destroy bknfc_client_destroy
#include "bk7258_nfc_client.c"
#undef bknfc_device_destroy
#include "bk7258_nfc_service.c"

static struct rpmsg_device ap = { "ap" }, cp = { "cp" };
static struct bknfc_rpc_response_s last_wire;
static struct bknfc_rpc_request_s queued;
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
static int deliver(struct bknfc_rpc_request_s *request)
{
  int ret;
  callback_depth++;
  ret = bknfc_server_cb(&g_bknfc_server.endpoint, request,
                         sizeof(*request), 0, &g_bknfc_server);
  callback_depth--;
  return ret;
}
static int rpmsg_trysend(struct rpmsg_endpoint *e, const void *data, int len)
{
  if (e == &g_bknfc_client.endpoint)
    {
      struct bknfc_rpc_request_s request;
      assert(len == sizeof(request));
      requests_sent++;
      if (reconnect_on_send)
        { reconnect_on_send = false; unlock_hook = client_reconnect; }
      if (no_buffers)
        { if (no_buffers > 0) no_buffers--; return RPMSG_ERR_NO_BUFF; }
      memcpy(&request, data, sizeof(request));
      return deliver(&request);
    }
  assert(e == &g_bknfc_server.endpoint && len == sizeof(last_wire));
  memcpy(&last_wire, data, sizeof(last_wire));
  responses_sent++;
  if (!drop_reply)
    {
      callback_depth++;
      (void)bknfc_client_cb(&g_bknfc_client.endpoint, &last_wire,
                            sizeof(last_wire), 0, &g_bknfc_client);
      callback_depth--;
    }
  return len;
}
static void client_reconnect(void)
{
  bknfc_client_destroy(&ap, &g_bknfc_client);
  bknfc_device_created(&ap, &g_bknfc_client);
  assert(g_bknfc_client.connection_error == 0);
}
static void server_reconnect(void)
{
  bknfc_device_destroy(&cp, &g_bknfc_server);
  bknfc_ns_bind(&cp, &g_bknfc_server, BKNFC_RPC_ENDPOINT, 1);
  assert(!g_bknfc_server.active && !g_bknfc_server.replay_valid);
}
static struct bknfc_rpc_request_s request(unsigned int sequence)
{
  struct bknfc_rpc_request_s r = {0};
  r.magic = BKNFC_RPC_MAGIC;
  r.version = BKNFC_RPC_VERSION;
  r.command = BKNFC_RPC_SCAN;
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
  opens = reads = nfc_selects = closes = requests_sent = responses_sent = 0;
  nfc_uid_size = 4; nfc_sak = 0;
  open_error = read_error = close_error = ioctl_error = no_buffers = 0;
  drop_reply = reconnect_on_send = false;
  unlock_hook = sleep_hook = wait_hook = read_hook = NULL;
  memset(&last_wire, 0, sizeof(last_wire));
}
static int exchange(void)
{
  struct bknfc_rpc_request_s r = request(1);
  struct bknfc_rpc_response_s reply = {0};
  int ret = bknfc_rpc_exchange(&r, &reply, 20);
  if (ret == 0)
    {
      assert(bknfc_rpc_response_valid(&reply));
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
  assert(requests_sent == 3 && nfc_selects == 1 && closes == 1);
  passes++;

  reset_case();
  no_buffers = -1;
  clock_t start = ticks;
  assert(exchange() == -ETIMEDOUT);
  assert(ticks - start == BKNFC_RPC_SEND_WAIT_MS);
  assert(nfc_selects == 0 && requests_sent > 1);
  passes++;
}
/* Deliver a late matching reply after fast reconnect.  The old exchange
 * must reject it even though reply_valid is true when the waiter resumes.
 */
static void cached_reply_then_reconnect(void)
{
  drain_worker();
  assert(g_bknfc_client.reply_valid);
  client_reconnect();
  assert(bknfc_client_cb(&g_bknfc_client.endpoint, &last_wire,
                          sizeof(last_wire), 0, &g_bknfc_client) == 0);
  assert(g_bknfc_client.reply_valid);
}
static void client_ns_reconnect(void)
{
  struct rpmsg_endpoint *e = &g_bknfc_client.endpoint;
  assert(e->unbind != NULL);
  e->ready = false; /* OpenAMP invalidates the destination on NS_DESTROY. */
  e->unbind(e);
  assert(g_bknfc_client.endpoint_created && e->rdev == &ap);
  e->ready = true; /* NS_CREATE binds the existing endpoint again. */
}
static void server_ns_reconnect(void)
{
  struct rpmsg_endpoint *e = &g_bknfc_server.endpoint;
  assert(e->unbind != NULL);
  e->ready = false;
  e->unbind(e);
  assert(!g_bknfc_server.endpoint_created);
  assert(!g_bknfc_server.active && !g_bknfc_server.replay_valid);
  bknfc_ns_bind(&cp, &g_bknfc_server, BKNFC_RPC_ENDPOINT, 1);
}
static void test_client_reconnect(void)
{
  reset_case();
  no_buffers = -1;
  reconnect_on_send = true;
  assert(exchange() == -ENOTCONN);
  assert(requests_sent == 1 && nfc_selects == 0);
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
  struct bknfc_rpc_request_s other = request(queued.sequence + 1);
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
  assert(nfc_selects == 1 && closes == 1);
  int before = responses_sent;
  assert(deliver(&queued) >= 0);
  assert(responses_sent == before + 1 && nfc_selects == 1);
  assert(last_wire.operation_status == 0);
  passes++;

  /* Cached data from a previous connection must never be replayed. */
  server_reconnect();
  assert(deliver(&queued) == 0);
  assert(responses_sent == before + 1);
  drain_worker();
  assert(nfc_selects == 2 && closes == 2);
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
  assert(nfc_selects == 2 && closes == 2);
  assert(responses_sent == 1 && last_wire.sequence == 2);
  assert(g_bknfc_server.last_request.sequence == 2);
  assert(!g_bknfc_server.active && !g_bknfc_server.pending);
  passes++;

  /* Old request never started: old and new tokens must execute new slot once. */
  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  reconnect_and_queue();
  assert(g_bknfc_server.request_sem == 2);
  drain_worker();
  assert(nfc_selects == 1 && closes == 1 && responses_sent == 1);
  assert(last_wire.sequence == 2 && g_bknfc_server.request_sem == 0);
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
  struct bknfc_rpc_response_s reply;
  bknfc_rpc_make_response(&reply, &queued, -EBUSY);
  uint32_t old_epoch = g_bknfc_server.epoch;
  server_reconnect();
  assert(bknfc_send(&g_bknfc_server, &reply, old_epoch) == -ENOTCONN);
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
  assert(g_bknfc_client.connection_error == -ENOTCONN);
  wait_hook = drain_worker;
  assert(exchange() == 0);
  assert(g_bknfc_client.connection_error == 0);
  assert(nfc_selects == 2 && closes == 2);
  passes++;

  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  read_hook = ns_reconnect_and_queue;
  drain_worker();
  assert(nfc_selects == 2 && closes == 2 && responses_sent == 1);
  assert(last_wire.sequence == 2);
  passes++;

  int before = responses_sent;
  server_ns_reconnect();
  assert(deliver(&queued) == 0);
  assert(responses_sent == before); /* cached reply invalidated by unbind */
  drain_worker();
  assert(nfc_selects == 3 && closes == 3 && responses_sent == before + 1);
  passes++;

  reset_case();
  queued = request(1);
  assert(deliver(&queued) == 0);
  ns_reconnect_and_queue();
  drain_worker();
  assert(nfc_selects == 1 && closes == 1 && responses_sent == 1);
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
static void test_selection_result(int selected)
{
  static const struct
  {
    int error;
    uint8_t size;
    uint8_t sak;
    int expected;
  } cases[] =
  {
    {0, 0, 0, -EPROTO},
    {ETIMEDOUT, 4, 0, -ETIMEDOUT},
    {EPROTO, 4, 0, -EPROTO},
    {0, 6, 0, -EPROTO},
    {0, 4, 0x24, -EPROTO},
    {0, 4, 0, 0},
    {0, 7, 0, 0},
    {0, 10, 0x20, 0}
  };

  for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
      if (selected >= 0 && i != (unsigned int)selected) continue;
      reset_case();
      queued = request(1);
      read_error = cases[i].error;
      nfc_uid_size = cases[i].size;
      nfc_sak = cases[i].sak;
      assert(deliver(&queued) == 0);
      drain_worker();
      assert(last_wire.operation_status == cases[i].expected);
      assert(last_wire.present == (cases[i].expected == 0));
      assert(nfc_selects == 1 && reads == 0 && closes == 1);
      assert(sizeof(last_wire) == 40);
      assert(bknfc_rpc_response_valid(&last_wire));
      passes++;
    }
}
int main(int argc, char **argv)
{
  assert(bknfc_rpc_client_initialize() == 0);
  assert(bk7258_nfc_service_start() == 0);
  bknfc_device_created(&ap, &g_bknfc_client);
  bknfc_ns_bind(&cp, &g_bknfc_server, BKNFC_RPC_ENDPOINT, 1);
  if (argc == 2)
    {
      assert(argv[1][0] >= '0' && argv[1][0] <= '7' && argv[1][1] == 0);
      test_selection_result(argv[1][0] - '0');
      assert(passes == 1);
      puts("CONTRACT_PASS");
      return 0;
    }

  assert(argc == 1);
  test_selection_result(-1);
  test_client_backpressure();
  test_client_reconnect();
  test_replay_and_busy();
  test_old_worker_and_tokens();
  test_errors_close();
  test_namespace_rebind();
  printf("BKNFC_RPC_HOST_PASS cases=%u\n", passes);
  return 0;
}
