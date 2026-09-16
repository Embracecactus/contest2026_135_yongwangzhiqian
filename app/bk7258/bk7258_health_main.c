/****************************************************************************
 * app/bk7258/bk7258_health_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-visible read-only command for the AP-owned BKHealth service.
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_health_protocol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bkhealth_usage(void)
{
  fprintf(stderr, "usage: bkhealth status\n");
}

static const char *bkhealth_battery_state_name(uint32_t state)
{
  switch (state)
    {
      case BKHEALTH_BATTERY_UNKNOWN:
        return "unknown";
      case BKHEALTH_BATTERY_FAULT:
        return "fault";
      case BKHEALTH_BATTERY_IDLE:
        return "idle";
      case BKHEALTH_BATTERY_FULL:
        return "full";
      case BKHEALTH_BATTERY_CHARGING:
        return "charging";
      case BKHEALTH_BATTERY_DISCHARGING:
        return "discharging";
      default:
        return "invalid";
    }
}

static int bkhealth_status(void)
{
  struct bkhealth_rpc_request_s request;
  struct bkhealth_rpc_response_s response;
  bool state_valid;
  bool voltage_valid;
  bool raw_valid;
  bool temperature_valid;
  int ret;

  memset(&request, 0, sizeof(request));
  memset(&response, 0, sizeof(response));
  request.command = BKHEALTH_RPC_STATUS;

  ret = bkhealth_rpc_exchange(&request, &response,
                              BKHEALTH_RPC_REPLY_WAIT_MS);
  if (ret < 0)
    {
      fprintf(stderr, "BKHEALTH RPC FAIL ret=%d\n", ret);
      return ret;
    }

  if (response.rpc_status < 0)
    {
      fprintf(stderr, "BKHEALTH STATUS FAIL rpc=%ld operation=%ld\n",
              (long)response.rpc_status,
              (long)response.operation_status);
      return response.rpc_status;
    }

  state_valid =
    (response.flags & BKHEALTH_FLAG_BATTERY_STATE_VALID) != 0;
  voltage_valid =
    (response.flags & BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID) != 0;
  raw_valid =
    (response.flags & BKHEALTH_FLAG_TEMPERATURE_RAW_VALID) != 0;
  temperature_valid =
    (response.flags &
     BKHEALTH_FLAG_TEMPERATURE_MILLICELSIUS_VALID) != 0;

  printf("BKHEALTH STATUS operation=%ld ",
         (long)response.operation_status);
  if (state_valid)
    {
      printf("battery_state=%s(%lu) ",
             bkhealth_battery_state_name(response.battery_state),
             (unsigned long)response.battery_state);
    }
  else
    {
      printf("battery_state=unavailable(state_ret=%ld) ",
             (long)response.battery_state_status);
    }

  if (voltage_valid)
    {
      printf("voltage_mV=%ld ", (long)response.battery_voltage_mv);
    }
  else
    {
      printf("voltage_mV=unavailable(voltage_ret=%ld) ",
             (long)response.battery_voltage_status);
    }

  printf("percent=unavailable ");
  if (raw_valid)
    {
      printf("temperature_raw=%ld reference_raw=%ld generation=%lu "
             "sequence=%lu ",
             (long)response.temperature_raw_code,
             (long)response.temperature_reference_raw,
             (unsigned long)response.temperature_generation,
             (unsigned long)response.temperature_sequence);
    }
  else
    {
      printf("temperature_raw=unavailable(temp_ret=%ld) ",
             (long)response.temperature_status);
    }

  if (temperature_valid)
    {
      printf("temperature_mC=%ld calibrated=yes\n",
             (long)response.temperature_millicelsius);
    }
  else
    {
      printf("temperature_mC=unavailable calibrated=no\n");
    }

  return response.operation_status;
}

int main(int argc, char **argv)
{
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      ret = bkhealth_status();
    }
  else
    {
      bkhealth_usage();
      return EXIT_FAILURE;
    }

  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
