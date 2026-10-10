/****************************************************************************
 * tests/host/bk7258/test_bk7258_health_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host contract tests for BKHealth validity and partial-source policy.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_health_core.h"

struct bkhealth_fixture_s
{
  unsigned int open_calls;
  unsigned int state_calls;
  unsigned int voltage_calls;
  unsigned int close_calls;
  unsigned int temperature_calls;
  int open_result;
  int state_result;
  int voltage_result;
  int close_result;
  int temperature_result;
  uint32_t state;
  int32_t voltage_mv;
  struct bkhealth_temperature_sample_s temperature;
};

static int fixture_battery_open(void *context)
{
  struct bkhealth_fixture_s *fixture = context;

  fixture->open_calls++;
  return fixture->open_result;
}

static int fixture_battery_state(void *context, uint32_t *state)
{
  struct bkhealth_fixture_s *fixture = context;

  fixture->state_calls++;
  if (fixture->state_result >= 0)
    {
      *state = fixture->state;
    }

  return fixture->state_result;
}

static int fixture_battery_voltage(void *context, int32_t *voltage_mv)
{
  struct bkhealth_fixture_s *fixture = context;

  fixture->voltage_calls++;
  if (fixture->voltage_result >= 0)
    {
      *voltage_mv = fixture->voltage_mv;
    }

  return fixture->voltage_result;
}

static int fixture_battery_close(void *context)
{
  struct bkhealth_fixture_s *fixture = context;

  fixture->close_calls++;
  return fixture->close_result;
}

static int fixture_temperature_read(
  void *context, struct bkhealth_temperature_sample_s *sample)
{
  struct bkhealth_fixture_s *fixture = context;

  fixture->temperature_calls++;
  if (fixture->temperature_result >= 0)
    {
      *sample = fixture->temperature;
    }

  return fixture->temperature_result;
}

static const struct bkhealth_source_ops_s g_source_ops =
{
  .battery_open = fixture_battery_open,
  .battery_state = fixture_battery_state,
  .battery_voltage_mv = fixture_battery_voltage,
  .battery_close = fixture_battery_close,
  .temperature_read = fixture_temperature_read,
};

static struct bkhealth_rpc_request_s make_request(void)
{
  struct bkhealth_rpc_request_s request;

  memset(&request, 0, sizeof(request));
  request.magic = BKHEALTH_RPC_MAGIC;
  request.version = BKHEALTH_RPC_VERSION;
  request.command = BKHEALTH_RPC_STATUS;
  request.session = 7;
  request.sequence = 9;
  return request;
}

static void test_request_validation(void)
{
  struct bkhealth_rpc_request_s request = make_request();

  assert(bkhealth_rpc_request_valid(&request));
  assert(!bkhealth_rpc_request_valid(NULL));

  request.magic++;
  assert(!bkhealth_rpc_request_valid(&request));
  request = make_request();
  request.version++;
  assert(!bkhealth_rpc_request_valid(&request));
  request = make_request();
  request.command = 99;
  assert(!bkhealth_rpc_request_valid(&request));
  request = make_request();
  request.session = 0;
  assert(!bkhealth_rpc_request_valid(&request));
  request = make_request();
  request.sequence = 0;
  assert(!bkhealth_rpc_request_valid(&request));
  request = make_request();
  request.reserved[1] = 1;
  assert(!bkhealth_rpc_request_valid(&request));
}

static void test_complete_snapshot(void)
{
  struct bkhealth_fixture_s fixture;
  struct bkhealth_rpc_request_s request = make_request();
  struct bkhealth_rpc_response_s response;
  uint32_t expected_flags;

  memset(&fixture, 0, sizeof(fixture));
  fixture.state = BKHEALTH_BATTERY_FULL;
  fixture.voltage_mv = 3750;
  fixture.temperature.flags = BKHEALTH_TEMPERATURE_RAW_VALID |
                              BKHEALTH_TEMPERATURE_CALIBRATED;
  fixture.temperature.raw_code = 531;
  fixture.temperature.reference_raw = 565;
  fixture.temperature.temperature_millicelsius = 32391;
  fixture.temperature.generation = 3;
  fixture.temperature.sequence = 11;

  assert(bkhealth_rpc_handle_request(&request, &response,
                                     &g_source_ops, &fixture) == 0);
  expected_flags = BKHEALTH_FLAG_BATTERY_STATE_VALID |
                   BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID |
                   BKHEALTH_FLAG_TEMPERATURE_RAW_VALID |
                   BKHEALTH_FLAG_TEMPERATURE_CALIBRATED |
                   BKHEALTH_FLAG_TEMPERATURE_MILLICELSIUS_VALID;
  assert(response.magic == BKHEALTH_RPC_MAGIC);
  assert(response.version == BKHEALTH_RPC_VERSION);
  assert(response.command == BKHEALTH_RPC_RESPONSE);
  assert(response.session == request.session);
  assert(response.sequence == request.sequence);
  assert(response.rpc_status == 0);
  assert(response.operation_status == 0);
  assert(response.flags == expected_flags);
  assert(response.battery_state == BKHEALTH_BATTERY_FULL);
  assert(response.battery_voltage_mv == 3750);
  assert(response.temperature_raw_code == 531);
  assert(response.temperature_reference_raw == 565);
  assert(response.temperature_millicelsius == 32391);
  assert(response.temperature_generation == 3);
  assert(response.temperature_sequence == 11);
  assert(fixture.open_calls == 1);
  assert(fixture.state_calls == 1);
  assert(fixture.voltage_calls == 1);
  assert(fixture.close_calls == 1);
  assert(fixture.temperature_calls == 1);
  assert(bkhealth_rpc_response_valid(&response));
}

static void test_partial_snapshot_keeps_raw_only(void)
{
  struct bkhealth_fixture_s fixture;
  struct bkhealth_rpc_request_s request = make_request();
  struct bkhealth_rpc_response_s response;

  memset(&fixture, 0, sizeof(fixture));
  fixture.open_result = -ENOENT;
  fixture.temperature.flags = BKHEALTH_TEMPERATURE_RAW_VALID;
  fixture.temperature.raw_code = 542;
  fixture.temperature.reference_raw = 0;
  fixture.temperature.temperature_millicelsius = 99999;

  assert(bkhealth_rpc_handle_request(&request, &response,
                                     &g_source_ops, &fixture) == 0);
  assert(response.operation_status == 0);
  assert(response.battery_state_status == -ENOENT);
  assert(response.battery_voltage_status == -ENOENT);
  assert(response.temperature_status == 0);
  assert(response.flags == BKHEALTH_FLAG_TEMPERATURE_RAW_VALID);
  assert(response.temperature_raw_code == 542);
  assert(response.temperature_millicelsius == 0);
  assert(fixture.open_calls == 1);
  assert(fixture.close_calls == 0);
  assert(bkhealth_rpc_response_valid(&response));
}

static void test_independent_battery_fields(void)
{
  struct bkhealth_fixture_s fixture;
  struct bkhealth_rpc_request_s request = make_request();
  struct bkhealth_rpc_response_s response;

  memset(&fixture, 0, sizeof(fixture));
  fixture.state_result = -EIO;
  fixture.voltage_mv = 4012;
  fixture.temperature_result = -EAGAIN;

  assert(bkhealth_rpc_handle_request(&request, &response,
                                     &g_source_ops, &fixture) == 0);
  assert(response.battery_state_status == -EIO);
  assert(response.battery_voltage_status == 0);
  assert(response.battery_voltage_mv == 4012);
  assert(response.temperature_status == -EAGAIN);
  assert(response.flags == BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID);
  assert(fixture.close_calls == 1);
}

static void test_total_failure_returns_first_error(void)
{
  struct bkhealth_fixture_s fixture;
  struct bkhealth_rpc_request_s request = make_request();
  struct bkhealth_rpc_response_s response;

  memset(&fixture, 0, sizeof(fixture));
  fixture.open_result = -ENOENT;
  fixture.temperature_result = -EAGAIN;

  assert(bkhealth_rpc_handle_request(&request, &response,
                                     &g_source_ops, &fixture) == -ENOENT);
  assert(response.rpc_status == 0);
  assert(response.operation_status == -ENOENT);
  assert(response.flags == 0);
  assert(fixture.close_calls == 0);
}

static void test_invalid_values_are_not_exposed(void)
{
  struct bkhealth_fixture_s fixture;
  struct bkhealth_rpc_request_s request = make_request();
  struct bkhealth_rpc_response_s response;

  memset(&fixture, 0, sizeof(fixture));
  fixture.state = BKHEALTH_BATTERY_DISCHARGING + 1u;
  fixture.voltage_mv = -1;
  fixture.temperature.flags = BKHEALTH_TEMPERATURE_CALIBRATED;
  fixture.temperature.temperature_millicelsius = 25000;

  assert(bkhealth_rpc_handle_request(&request, &response,
                                     &g_source_ops, &fixture) == -ERANGE);
  assert(response.battery_state_status == -ERANGE);
  assert(response.battery_voltage_status == -ERANGE);
  assert(response.temperature_status == -EPROTO);
  assert(response.flags == 0);
  assert(response.temperature_millicelsius == 0);
  assert(fixture.close_calls == 1);
}

static void test_missing_close_never_opens_battery(void)
{
  struct bkhealth_fixture_s fixture;
  struct bkhealth_source_ops_s ops = g_source_ops;
  struct bkhealth_rpc_request_s request = make_request();
  struct bkhealth_rpc_response_s response;

  memset(&fixture, 0, sizeof(fixture));
  fixture.temperature_result = -EAGAIN;
  ops.battery_close = NULL;

  assert(bkhealth_rpc_handle_request(&request, &response,
                                     &ops, &fixture) == -EINVAL);
  assert(response.battery_state_status == -EINVAL);
  assert(response.battery_voltage_status == -EINVAL);
  assert(fixture.open_calls == 0);
}

static void test_invalid_request_gets_correlated_rpc_error(void)
{
  struct bkhealth_rpc_request_s request = make_request();
  struct bkhealth_rpc_response_s response;

  request.reserved[0] = 1;
  assert(bkhealth_rpc_handle_request(&request, &response,
                                     NULL, NULL) == -EINVAL);
  assert(response.rpc_status == -EINVAL);
  assert(response.operation_status == -ENODATA);
  assert(response.session == request.session);
  assert(response.sequence == request.sequence);
  assert(response.reserved[0] == 0);
  assert(response.reserved[1] == 0);
  assert(bkhealth_rpc_handle_request(NULL, &response,
                                     NULL, NULL) == -EINVAL);
  assert(response.session == 0);
  assert(response.sequence == 0);
  assert(bkhealth_rpc_handle_request(&request, NULL,
                                     NULL, NULL) == -EINVAL);
}

static void test_response_validation_rejects_malformed_wire(void)
{
  struct bkhealth_rpc_request_s request = make_request();
  struct bkhealth_rpc_response_s response;

  memset(&response, 0, sizeof(response));
  response.magic = BKHEALTH_RPC_MAGIC;
  response.version = BKHEALTH_RPC_VERSION;
  response.command = BKHEALTH_RPC_RESPONSE;
  response.session = 1;
  response.sequence = 1;
  response.rpc_status = 0;
  response.operation_status = 0;
  response.battery_state_status = -ENODATA;
  response.battery_voltage_status = -ENODATA;
  response.temperature_status = 0;
  response.flags = BKHEALTH_FLAG_TEMPERATURE_RAW_VALID;
  assert(bkhealth_rpc_response_valid(&response));

  response.rpc_status = -EIO;
  assert(!bkhealth_rpc_response_valid(&response));
  response.rpc_status = 0;

  response.reserved[0] = 1;
  assert(!bkhealth_rpc_response_valid(&response));
  response.reserved[0] = 0;

  response.flags |= 1u << 12;
  assert(!bkhealth_rpc_response_valid(&response));

  response.flags = BKHEALTH_FLAG_TEMPERATURE_RAW_VALID;
  response.temperature_status = -EIO;
  assert(!bkhealth_rpc_response_valid(&response));
  response.temperature_status = 0;

  response.flags = BKHEALTH_FLAG_TEMPERATURE_MILLICELSIUS_VALID;
  assert(!bkhealth_rpc_response_valid(&response));

  response.flags = BKHEALTH_FLAG_TEMPERATURE_CALIBRATED;
  assert(!bkhealth_rpc_response_valid(&response));

  response.flags = BKHEALTH_FLAG_TEMPERATURE_RAW_VALID;
  response.operation_status = -EIO;
  assert(!bkhealth_rpc_response_valid(&response));
  response.operation_status = 0;

  response.flags = BKHEALTH_FLAG_BATTERY_STATE_VALID;
  response.battery_state_status = 0;
  response.temperature_status = -ENODATA;
  response.battery_state = BKHEALTH_BATTERY_DISCHARGING + 1u;
  assert(!bkhealth_rpc_response_valid(&response));

  response.flags = BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID;
  response.battery_state_status = -ENODATA;
  response.battery_voltage_status = 0;
  response.battery_voltage_mv = -1;
  assert(!bkhealth_rpc_response_valid(&response));

  response.battery_voltage_mv = 3800;
  assert(bkhealth_rpc_response_valid(&response));
  response.session = 0;
  assert(!bkhealth_rpc_response_valid(&response));

  bkhealth_rpc_make_response(&response, &request, -EBUSY);
  assert(bkhealth_rpc_response_valid(&response));
}

int main(void)
{
  {
    struct bkhealth_rpc_request_s request = make_request();
    struct bkhealth_rpc_response_s response;
    struct bkhealth_fixture_s fixture = {0};

    /* Read-only resource query must not acquire ADC or resample peripherals. */
    request.command = 3;
    assert(bkhealth_rpc_handle_request(&request, &response,
                                      &g_source_ops, &fixture) == 0);
    assert(fixture.open_calls == 0 && fixture.temperature_calls == 0);
    assert(response.command == 0x8002);
    assert(bkhealth_rpc_response_valid(&response));
    response.flags = BKHEALTH_FLAG_BATTERY_STATE_VALID;
    assert(!bkhealth_rpc_response_valid(&response));
  }
  test_request_validation();
  test_complete_snapshot();
  test_partial_snapshot_keeps_raw_only();
  test_independent_battery_fields();
  test_total_failure_returns_first_error();
  test_invalid_values_are_not_exposed();
  test_missing_close_never_opens_battery();
  test_invalid_request_gets_correlated_rpc_error();
  test_response_validation_rejects_malformed_wire();
  puts("BKHEALTH_CORE_HOST_TEST_PASS");
  return 0;
}
