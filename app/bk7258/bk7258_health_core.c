/****************************************************************************
 * app/bk7258/bk7258_health_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable request validation and partial device-health collection.
 ****************************************************************************/

#include "bk7258_health_core.h"

#include <errno.h>
#include <string.h>

static void bkhealth_first_error(int *first_error, int ret)
{
  if (*first_error == -ENODATA && ret < 0)
    {
      *first_error = ret;
    }
}

bool bkhealth_rpc_request_valid(
  const struct bkhealth_rpc_request_s *request)
{
  return request != NULL && request->magic == BKHEALTH_RPC_MAGIC &&
         request->version == BKHEALTH_RPC_VERSION &&
         request->command == BKHEALTH_RPC_STATUS &&
         request->session != 0 && request->sequence != 0 &&
         request->reserved[0] == 0 && request->reserved[1] == 0;
}

bool bkhealth_rpc_response_valid(
  const struct bkhealth_rpc_response_s *response)
{
  const uint32_t known_flags = BKHEALTH_FLAG_BATTERY_STATE_VALID |
    BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID |
    BKHEALTH_FLAG_TEMPERATURE_RAW_VALID |
    BKHEALTH_FLAG_TEMPERATURE_CALIBRATED |
    BKHEALTH_FLAG_TEMPERATURE_MILLICELSIUS_VALID;
  const uint32_t primary_flags = BKHEALTH_FLAG_BATTERY_STATE_VALID |
    BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID |
    BKHEALTH_FLAG_TEMPERATURE_RAW_VALID;
  uint32_t flags;

  if (response == NULL || response->magic != BKHEALTH_RPC_MAGIC ||
      response->version != BKHEALTH_RPC_VERSION ||
      response->command != BKHEALTH_RPC_RESPONSE ||
      response->session == 0 || response->sequence == 0 ||
      response->reserved[0] != 0 || response->reserved[1] != 0 ||
      (response->flags & ~known_flags) != 0)
    {
      return false;
    }

  flags = response->flags;
  if (response->rpc_status > 0 || response->operation_status > 0 ||
      response->battery_state_status > 0 ||
      response->battery_voltage_status > 0 ||
      response->temperature_status > 0)
    {
      return false;
    }

  if (response->rpc_status < 0)
    {
      return flags == 0 && response->operation_status < 0 &&
             response->battery_state_status < 0 &&
             response->battery_voltage_status < 0 &&
             response->temperature_status < 0;
    }

  if (((flags & BKHEALTH_FLAG_BATTERY_STATE_VALID) != 0) !=
      (response->battery_state_status == 0) ||
      ((flags & BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID) != 0) !=
      (response->battery_voltage_status == 0) ||
      ((flags & BKHEALTH_FLAG_TEMPERATURE_RAW_VALID) != 0) !=
      (response->temperature_status == 0))
    {
      return false;
    }

  if ((flags & BKHEALTH_FLAG_TEMPERATURE_MILLICELSIUS_VALID) != 0 &&
      (flags & (BKHEALTH_FLAG_TEMPERATURE_RAW_VALID |
                BKHEALTH_FLAG_TEMPERATURE_CALIBRATED)) !=
      (BKHEALTH_FLAG_TEMPERATURE_RAW_VALID |
       BKHEALTH_FLAG_TEMPERATURE_CALIBRATED))
    {
      return false;
    }

  if ((flags & BKHEALTH_FLAG_TEMPERATURE_CALIBRATED) != 0 &&
      (flags & BKHEALTH_FLAG_TEMPERATURE_MILLICELSIUS_VALID) == 0)
    {
      return false;
    }

  if ((response->operation_status == 0) !=
      ((flags & primary_flags) != 0))
    {
      return false;
    }

  if ((flags & BKHEALTH_FLAG_BATTERY_STATE_VALID) != 0 &&
      response->battery_state > BKHEALTH_BATTERY_DISCHARGING)
    {
      return false;
    }

  if ((flags & BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID) != 0 &&
      response->battery_voltage_mv < 0)
    {
      return false;
    }

  return true;
}

void bkhealth_rpc_make_response(
  struct bkhealth_rpc_response_s *response,
  const struct bkhealth_rpc_request_s *request, int rpc_status)
{
  if (response == NULL)
    {
      return;
    }

  memset(response, 0, sizeof(*response));
  response->magic = BKHEALTH_RPC_MAGIC;
  response->version = BKHEALTH_RPC_VERSION;
  response->command = BKHEALTH_RPC_RESPONSE;
  response->rpc_status = rpc_status;
  response->operation_status = -ENODATA;
  response->battery_state_status = -ENODATA;
  response->battery_voltage_status = -ENODATA;
  response->temperature_status = -ENODATA;
  if (request != NULL)
    {
      response->session = request->session;
      response->sequence = request->sequence;
    }
}

static void bkhealth_collect_battery(
  struct bkhealth_rpc_response_s *response,
  const struct bkhealth_source_ops_s *ops, void *context,
  int *first_error, bool *any_valid)
{
  uint32_t state = BKHEALTH_BATTERY_UNKNOWN;
  int32_t voltage_mv = 0;
  int ret;

  if (ops->battery_open == NULL)
    {
      response->battery_state_status = -ENOSYS;
      response->battery_voltage_status = -ENOSYS;
      bkhealth_first_error(first_error, -ENOSYS);
      return;
    }

  /* Refuse to acquire a resource that this source cannot release. */

  if (ops->battery_close == NULL)
    {
      response->battery_state_status = -EINVAL;
      response->battery_voltage_status = -EINVAL;
      bkhealth_first_error(first_error, -EINVAL);
      return;
    }

  ret = ops->battery_open(context);
  if (ret < 0)
    {
      response->battery_state_status = ret;
      response->battery_voltage_status = ret;
      bkhealth_first_error(first_error, ret);
      return;
    }

  ret = ops->battery_state == NULL ? -ENOSYS :
        ops->battery_state(context, &state);
  if (ret >= 0 && state > BKHEALTH_BATTERY_DISCHARGING)
    {
      ret = -ERANGE;
    }

  response->battery_state_status = ret;
  if (ret >= 0)
    {
      response->battery_state = state;
      response->flags |= BKHEALTH_FLAG_BATTERY_STATE_VALID;
      *any_valid = true;
    }
  else
    {
      bkhealth_first_error(first_error, ret);
    }

  ret = ops->battery_voltage_mv == NULL ? -ENOSYS :
        ops->battery_voltage_mv(context, &voltage_mv);
  if (ret >= 0 && voltage_mv < 0)
    {
      ret = -ERANGE;
    }

  response->battery_voltage_status = ret;
  if (ret >= 0)
    {
      response->battery_voltage_mv = voltage_mv;
      response->flags |= BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID;
      *any_valid = true;
    }
  else
    {
      bkhealth_first_error(first_error, ret);
    }

  /* Collection is serialized by the AP worker.  A close failure does not
   * invalidate values already returned, but the source clears its local
   * descriptor before closing so the next request can retry cleanly.
   */

  ret = ops->battery_close(context);
  bkhealth_first_error(first_error, ret);
}

static void bkhealth_collect_temperature(
  struct bkhealth_rpc_response_s *response,
  const struct bkhealth_source_ops_s *ops, void *context,
  int *first_error, bool *any_valid)
{
  struct bkhealth_temperature_sample_s sample;
  int ret;

  if (ops->temperature_read == NULL)
    {
      response->temperature_status = -ENOSYS;
      bkhealth_first_error(first_error, -ENOSYS);
      return;
    }

  memset(&sample, 0, sizeof(sample));
  ret = ops->temperature_read(context, &sample);
  if (ret < 0)
    {
      response->temperature_status = ret;
      bkhealth_first_error(first_error, ret);
      return;
    }

  if ((sample.flags & BKHEALTH_TEMPERATURE_RAW_VALID) == 0)
    {
      ret = (sample.flags & BKHEALTH_TEMPERATURE_CALIBRATED) != 0 ?
            -EPROTO : -ENODATA;
      response->temperature_status = ret;
      bkhealth_first_error(first_error, ret);
      return;
    }

  response->temperature_status = 0;
  response->temperature_raw_code = sample.raw_code;
  response->temperature_reference_raw = sample.reference_raw;
  response->temperature_generation = sample.generation;
  response->temperature_sequence = sample.sequence;
  response->flags |= BKHEALTH_FLAG_TEMPERATURE_RAW_VALID;
  *any_valid = true;

  if ((sample.flags & BKHEALTH_TEMPERATURE_CALIBRATED) != 0)
    {
      response->temperature_millicelsius =
        sample.temperature_millicelsius;
      response->flags |= BKHEALTH_FLAG_TEMPERATURE_CALIBRATED |
                         BKHEALTH_FLAG_TEMPERATURE_MILLICELSIUS_VALID;
    }
}

int bkhealth_rpc_handle_request(
  const struct bkhealth_rpc_request_s *request,
  struct bkhealth_rpc_response_s *response,
  const struct bkhealth_source_ops_s *ops, void *context)
{
  int first_error = -ENODATA;
  bool any_valid = false;

  if (response == NULL)
    {
      return -EINVAL;
    }

  if (!bkhealth_rpc_request_valid(request))
    {
      bkhealth_rpc_make_response(response, request, -EINVAL);
      return -EINVAL;
    }

  if (ops == NULL)
    {
      bkhealth_rpc_make_response(response, request, -EINVAL);
      return -EINVAL;
    }

  bkhealth_rpc_make_response(response, request, 0);
  bkhealth_collect_battery(response, ops, context,
                           &first_error, &any_valid);
  bkhealth_collect_temperature(response, ops, context,
                               &first_error, &any_valid);
  response->operation_status = any_valid ? 0 : first_error;
  return response->operation_status;
}
