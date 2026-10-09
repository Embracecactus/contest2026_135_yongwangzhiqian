/****************************************************************************
 * tests/host/bk7258/test_bk7258_motion_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_motion_core.h"

struct fixture_s
{
  unsigned int open_calls;
  unsigned int read_calls;
  unsigned int close_calls;
  int open_result;
  int read_result;
  int close_result;
  struct bkmotion_sample_s sample;
};

static int fixture_open(void *context)
{
  struct fixture_s *fixture = context;
  fixture->open_calls++;
  return fixture->open_result;
}

static int fixture_read(void *context, struct bkmotion_sample_s *sample)
{
  struct fixture_s *fixture = context;
  fixture->read_calls++;
  if (fixture->read_result >= 0)
    {
      *sample = fixture->sample;
    }
  return fixture->read_result;
}

static int fixture_close(void *context)
{
  struct fixture_s *fixture = context;
  fixture->close_calls++;
  return fixture->close_result;
}

static const struct bkmotion_source_ops_s g_ops =
{
  .open = fixture_open,
  .read = fixture_read,
  .close = fixture_close,
};

static struct bkmotion_rpc_request_s make_request(void)
{
  struct bkmotion_rpc_request_s request;

  memset(&request, 0, sizeof(request));
  request.magic = BKMOTION_RPC_MAGIC;
  request.version = BKMOTION_RPC_VERSION;
  request.command = BKMOTION_RPC_SAMPLE;
  request.session = 7;
  request.sequence = 11;
  return request;
}

static void test_valid_positive_and_negative_samples(void)
{
  struct fixture_s fixture;
  struct bkmotion_rpc_response_s response;
  struct bkmotion_rpc_request_s request = make_request();

  memset(&fixture, 0, sizeof(fixture));
  fixture.sample.timestamp_us = 123456;
  fixture.sample.x = 1.25f;
  fixture.sample.y = -2.5f;
  fixture.sample.z = 0.001f;
  fixture.sample.status = 0;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) == 0);
  assert(fixture.open_calls == 1 && fixture.read_calls == 1 &&
         fixture.close_calls == 1);
  assert(response.timestamp_us == fixture.sample.timestamp_us);
  assert(response.x_mms2 == 1250 && response.y_mms2 == -2500 &&
         response.z_mms2 == 1);
  assert((response.flags & BKMOTION_FLAG_SAMPLE_VALID) != 0);
  assert(bkmotion_rpc_response_valid(&response));
}

static void test_zero_timestamp_and_invalid_floats(void)
{
  struct fixture_s fixture;
  struct bkmotion_rpc_response_s response;
  struct bkmotion_rpc_request_s request = make_request();

  memset(&fixture, 0, sizeof(fixture));
  fixture.sample.timestamp_us = 0;
  fixture.sample.x = 0.0f;
  fixture.sample.y = -0.0f;
  fixture.sample.z = 9.80665f;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) ==
         -ERANGE);
  assert(response.timestamp_us == 0 && response.flags == 0);
  assert(fixture.open_calls == 1 && fixture.read_calls == 1 &&
         fixture.close_calls == 1);

  fixture.sample.timestamp_us = 123;
  fixture.sample.x = NAN;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) < 0);
  assert((response.flags & BKMOTION_FLAG_SAMPLE_VALID) == 0);
  fixture.sample.x = INFINITY;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) < 0);
  assert((response.flags & BKMOTION_FLAG_SAMPLE_VALID) == 0);
  fixture.sample.x = -INFINITY;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) < 0);
  assert((response.flags & BKMOTION_FLAG_SAMPLE_VALID) == 0);
}

static void test_scale_overflow_and_source_errors(void)
{
  struct fixture_s fixture;
  struct bkmotion_rpc_response_s response;
  struct bkmotion_rpc_request_s request = make_request();
  struct bkmotion_source_ops_s ops = g_ops;

  memset(&fixture, 0, sizeof(fixture));
  fixture.sample.timestamp_us = 123;
  fixture.sample.x = 2147484.0f;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) < 0);
  assert((response.flags & BKMOTION_FLAG_SAMPLE_VALID) == 0);

  fixture.sample.x = -2147484.0f;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) < 0);
  assert((response.flags & BKMOTION_FLAG_SAMPLE_VALID) == 0);

  memset(&fixture, 0, sizeof(fixture));
  fixture.open_result = -ENOENT;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) ==
         -ENOENT);
  assert(fixture.open_calls == 1 && fixture.read_calls == 0 &&
         fixture.close_calls == 0);

  memset(&fixture, 0, sizeof(fixture));
  fixture.read_result = -EIO;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) ==
         -EIO);
  assert(fixture.open_calls == 1 && fixture.read_calls == 1 &&
         fixture.close_calls == 1);

  memset(&fixture, 0, sizeof(fixture));
  fixture.sample.timestamp_us = 123;
  fixture.close_result = -EIO;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops, &fixture) ==
         -EIO);
  assert(fixture.open_calls == 1 && fixture.read_calls == 1 &&
         fixture.close_calls == 1);

  ops.open = NULL;
  assert(bkmotion_rpc_handle_request(&request, &response, &ops, &fixture) ==
         -EINVAL);
  ops = g_ops;
  ops.read = NULL;
  memset(&fixture, 0, sizeof(fixture));
  assert(bkmotion_rpc_handle_request(&request, &response, &ops, &fixture) ==
         -EINVAL);
  assert(fixture.open_calls == 0 && fixture.close_calls == 0);
  ops = g_ops;
  ops.close = NULL;
  assert(bkmotion_rpc_handle_request(&request, &response, &ops, &fixture) ==
         -EINVAL);
}

static void test_wire_fields_and_flags(void)
{
  struct bkmotion_rpc_request_s request = make_request();
  struct bkmotion_rpc_response_s response;

  assert(bkmotion_rpc_request_valid(&request));
  request.reserved[0] = 1;
  assert(!bkmotion_rpc_request_valid(&request));
  request = make_request();
  request.command = BKMOTION_RPC_RESPONSE;
  assert(!bkmotion_rpc_request_valid(&request));

  bkmotion_rpc_make_response(&response, &request, -EBUSY);
  assert(bkmotion_rpc_response_valid(&response));
  response.reserved[0] = 1;
  assert(!bkmotion_rpc_response_valid(&response));
  response.reserved[0] = 0;
  response.flags = BKMOTION_FLAG_SAMPLE_VALID;
  response.x_mms2 = INT32_MAX;
  response.y_mms2 = INT32_MIN;
  response.z_mms2 = 0;
  assert(!bkmotion_rpc_response_valid(&response));
}

static void test_failed_sample_does_not_reuse_previous_result(void)
{
  struct fixture_s fixture = {0};
  struct bkmotion_rpc_request_s request = make_request();
  struct bkmotion_rpc_response_s response;

  fixture.sample.timestamp_us = 321;
  fixture.sample.x = 1.0f;
  fixture.sample.z = 9.8f;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops,
                                    &fixture) == 0);
  request.session++;
  request.sequence++;
  fixture.read_result = -EIO;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops,
                                    &fixture) == -EIO);
  assert(response.session == request.session &&
         response.sequence == request.sequence);
  assert(response.flags == 0 && response.timestamp_us == 0);
  assert(response.x_mms2 == 0 && response.y_mms2 == 0 &&
         response.z_mms2 == 0);
  assert(bkmotion_rpc_response_valid(&response));
  assert(fixture.open_calls == 2 && fixture.close_calls == 2);

  /* A malformed query must not open hardware or sample a sensor. */

  request.sequence = 0;
  assert(bkmotion_rpc_handle_request(&request, &response, &g_ops,
                                    &fixture) < 0);
  assert(fixture.open_calls == 2 && fixture.read_calls == 2 &&
         fixture.close_calls == 2);
}

int main(void)
{
  test_failed_sample_does_not_reuse_previous_result();
  test_valid_positive_and_negative_samples();
  test_zero_timestamp_and_invalid_floats();
  test_scale_overflow_and_source_errors();
  test_wire_fields_and_flags();
  puts("BK7258_MOTION_CORE_TEST_PASS");
  return 0;
}
