/****************************************************************************
 * tests/host/bk7258/test_bk7258_nfc_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_nfc_core.h"

struct fixture_s
{
  unsigned int open_calls;
  unsigned int read_calls;
  unsigned int close_calls;
  int open_result;
  int read_result;
  int close_result;
  size_t read_length;
};

static int fixture_open(void *context)
{
  struct fixture_s *fixture = context;

  fixture->open_calls++;
  return fixture->open_result;
}

static int fixture_read(void *context, void *buffer, size_t length)
{
  struct fixture_s *fixture = context;

  fixture->read_calls++;
  fixture->read_length = length;
  if (fixture->read_result >= 0 && length != 0)
    {
      *(uint8_t *)buffer = 0xa5;
    }

  return fixture->read_result;
}

static int fixture_close(void *context)
{
  struct fixture_s *fixture = context;

  fixture->close_calls++;
  return fixture->close_result;
}

static const struct bknfc_source_ops_s g_ops =
{
  .open = fixture_open,
  .read = fixture_read,
  .close = fixture_close,
};

static struct bknfc_rpc_request_s make_request(void)
{
  struct bknfc_rpc_request_s request;

  memset(&request, 0, sizeof(request));
  request.magic = BKNFC_RPC_MAGIC;
  request.version = BKNFC_RPC_VERSION;
  request.command = BKNFC_RPC_SCAN;
  request.session = 4;
  request.sequence = 9;
  return request;
}

static void test_result(int read_result, uint32_t present)
{
  struct fixture_s fixture;
  struct bknfc_rpc_response_s response;
  struct bknfc_rpc_request_s request = make_request();

  memset(&fixture, 0, sizeof(fixture));
  fixture.read_result = read_result;
  assert(bknfc_rpc_handle_request(&request, &response, &g_ops, &fixture) == 0);
  assert(response.present == present);
  assert(response.operation_status == 0);
  assert(fixture.open_calls == 1);
  assert(fixture.read_calls == 1);
  assert(fixture.close_calls == 1);
  assert(fixture.read_length == 1);
  assert(bknfc_rpc_response_valid(&response));
}

static void test_errors_and_release(void)
{
  struct fixture_s fixture;
  struct bknfc_rpc_response_s response;
  struct bknfc_rpc_request_s request = make_request();

  memset(&fixture, 0, sizeof(fixture));
  fixture.open_result = -ENOENT;
  assert(bknfc_rpc_handle_request(&request, &response, &g_ops, &fixture) ==
         -ENOENT);
  assert(fixture.read_calls == 0 && fixture.close_calls == 0);
  assert(response.present == 0 && bknfc_rpc_response_valid(&response));

  memset(&fixture, 0, sizeof(fixture));
  fixture.read_result = -EIO;
  assert(bknfc_rpc_handle_request(&request, &response, &g_ops, &fixture) ==
         -EIO);
  assert(fixture.close_calls == 1);
  assert(response.present == 0 && bknfc_rpc_response_valid(&response));

  memset(&fixture, 0, sizeof(fixture));
  fixture.read_result = 1;
  fixture.close_result = -EIO;
  assert(bknfc_rpc_handle_request(&request, &response, &g_ops, &fixture) ==
         -EIO);
  assert(fixture.close_calls == 1);
  assert(response.present == 0 && response.operation_status == -EIO);
  assert(bknfc_rpc_response_valid(&response));
}

static void test_no_unreleasable_open(void)
{
  struct fixture_s fixture;
  struct bknfc_source_ops_s ops = g_ops;
  struct bknfc_rpc_response_s response;
  struct bknfc_rpc_request_s request = make_request();

  memset(&fixture, 0, sizeof(fixture));
  ops.close = NULL;
  assert(bknfc_rpc_handle_request(&request, &response, &ops, &fixture) ==
         -EINVAL);
  assert(fixture.open_calls == 0 && fixture.read_calls == 0);
  assert(bknfc_rpc_response_valid(&response));
}

static void test_wire_and_privacy_contract(void)
{
  struct bknfc_rpc_response_s response;
  struct bknfc_rpc_request_s request = make_request();

  assert(bknfc_rpc_request_valid(&request));
  request.reserved[0] = 1;
  assert(!bknfc_rpc_request_valid(&request));
  request = make_request();
  request.reserved[1] = 1;
  assert(!bknfc_rpc_request_valid(&request));

  bknfc_rpc_make_response(&response, &request, -EBUSY);
  assert(bknfc_rpc_response_valid(&response));
  assert(sizeof(response) == 40); /* No UID/card-ID field exists. */

  response.reserved[0] = 1;
  assert(!bknfc_rpc_response_valid(&response));
  response.reserved[0] = 0;
  response.rpc_status = 1;
  assert(!bknfc_rpc_response_valid(&response));
  response.rpc_status = -EBUSY;
  response.operation_status = 0;
  assert(!bknfc_rpc_response_valid(&response));
  response.operation_status = -EBUSY;
  response.present = 1;
  assert(!bknfc_rpc_response_valid(&response));
  response.present = 0;
  assert(bknfc_rpc_response_valid(&response));
}

static int fixture_hce(void *context)
{
  struct fixture_s *fixture = context;
  return fixture->read_result;
}

static void test_hce_dispatch(void)
{
  struct bknfc_rpc_request_s request = make_request();
  struct bknfc_rpc_response_s response;
  struct bknfc_source_ops_s ops = g_ops;
  struct fixture_s fixture = {0};
  request.command = BKNFC_RPC_HCE;
  ops.hce = fixture_hce;
  assert(bknfc_rpc_handle_request(&request, &response, &ops, &fixture) == 0);
  assert(response.present == 1 && fixture.read_calls == 0);
  assert(fixture.open_calls == 1 && fixture.close_calls == 1);
  fixture.read_result = -EPROTO;
  assert(bknfc_rpc_handle_request(&request, &response, &ops, &fixture) == -EPROTO);
  assert(response.present == 0 && bknfc_rpc_response_valid(&response));
  ops.hce = NULL;
  assert(bknfc_rpc_handle_request(&request, &response, &ops, &fixture) == -ENOSYS);
  assert(response.present == 0 && fixture.close_calls == 3);
}

static void test_failed_sample_does_not_reuse_previous_result(void)
{
  struct fixture_s fixture = {0};
  struct bknfc_rpc_request_s request = make_request();
  struct bknfc_rpc_response_s response;

  fixture.read_result = 1;
  assert(bknfc_rpc_handle_request(&request, &response, &g_ops,
                                    &fixture) == 0);
  request.session++;
  request.sequence++;
  fixture.read_result = -EIO;
  assert(bknfc_rpc_handle_request(&request, &response, &g_ops,
                                    &fixture) == -EIO);
  assert(response.session == request.session &&
         response.sequence == request.sequence);
  assert(response.present == 0);
  assert(bknfc_rpc_response_valid(&response));
  assert(fixture.open_calls == 2 && fixture.close_calls == 2);

  /* A malformed query must not open hardware or sample a sensor. */

  request.sequence = 0;
  assert(bknfc_rpc_handle_request(&request, &response, &g_ops,
                                    &fixture) < 0);
  assert(fixture.open_calls == 2 && fixture.read_calls == 2 &&
         fixture.close_calls == 2);
}

int main(void)
{
  test_failed_sample_does_not_reuse_previous_result();
  test_hce_dispatch();
  test_result(1, 1);
  test_result(0, 1);
  test_result(-EAGAIN, 0);
  test_errors_and_release();
  test_no_unreleasable_open();
  test_wire_and_privacy_contract();
  puts("BKNFC_CORE_HOST_TEST_PASS");
  return 0;
}
