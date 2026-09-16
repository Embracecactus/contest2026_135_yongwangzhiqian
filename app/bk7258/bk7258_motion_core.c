/****************************************************************************
 * app/bk7258/bk7258_motion_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable single-sample motion policy.
 ****************************************************************************/

#include "bk7258_motion_core.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static bool bkmotion_scaled_valid(float value)
{
  double scaled = (double)value * 1000.0;

  return isfinite(value) && scaled >= (double)INT32_MIN &&
         scaled <= (double)INT32_MAX;
}

static int32_t bkmotion_scale_mms2(float value)
{
  return (int32_t)((double)value * 1000.0);
}

bool bkmotion_rpc_request_valid(const struct bkmotion_rpc_request_s *request)
{
  return request != NULL && request->magic == BKMOTION_RPC_MAGIC &&
         request->version == BKMOTION_RPC_VERSION &&
         request->command == BKMOTION_RPC_SAMPLE && request->session != 0 &&
         request->sequence != 0 && request->reserved[0] == 0 &&
         request->reserved[1] == 0;
}

bool bkmotion_rpc_response_valid(const struct bkmotion_rpc_response_s *response)
{
  if (response == NULL || response->magic != BKMOTION_RPC_MAGIC ||
      response->version != BKMOTION_RPC_VERSION ||
      response->command != BKMOTION_RPC_RESPONSE || response->session == 0 ||
      response->sequence == 0 || response->reserved0 != 0 ||
      response->reserved[0] != 0 || response->reserved[1] != 0 ||
      response->rpc_status > 0 || response->operation_status > 0 ||
      (response->flags & ~BKMOTION_FLAG_SAMPLE_VALID) != 0)
    {
      return false;
    }

  if (response->rpc_status < 0 || response->operation_status < 0)
    {
      return response->flags == 0 && response->timestamp_us == 0 &&
             response->x_mms2 == 0 && response->y_mms2 == 0 &&
             response->z_mms2 == 0 && response->sensor_status == 0;
    }

  return response->flags == BKMOTION_FLAG_SAMPLE_VALID &&
         response->timestamp_us != 0;
}

void bkmotion_rpc_make_response(struct bkmotion_rpc_response_s *response,
                                const struct bkmotion_rpc_request_s *request,
                                int rpc_status)
{
  if (response == NULL)
    {
      return;
    }

  memset(response, 0, sizeof(*response));
  response->magic = BKMOTION_RPC_MAGIC;
  response->version = BKMOTION_RPC_VERSION;
  response->command = BKMOTION_RPC_RESPONSE;
  response->rpc_status = rpc_status;
  response->operation_status = rpc_status < 0 ? rpc_status : -ENODATA;
  if (request != NULL)
    {
      response->session = request->session;
      response->sequence = request->sequence;
    }
}

int bkmotion_rpc_handle_request(const struct bkmotion_rpc_request_s *request,
                                struct bkmotion_rpc_response_s *response,
                                const struct bkmotion_source_ops_s *ops,
                                void *context)
{
  struct bkmotion_sample_s sample;
  int result;
  int close_result;

  if (response == NULL)
    {
      return -EINVAL;
    }

  if (!bkmotion_rpc_request_valid(request) || ops == NULL ||
      ops->open == NULL || ops->read == NULL || ops->close == NULL)
    {
      bkmotion_rpc_make_response(response, request, -EINVAL);
      return -EINVAL;
    }

  bkmotion_rpc_make_response(response, request, 0);
  result = ops->open(context);
  if (result < 0)
    {
      response->operation_status = result;
      return result;
    }

  memset(&sample, 0, sizeof(sample));
  result = ops->read(context, &sample);
  if (result >= 0 &&
      (sample.timestamp_us == 0 || !bkmotion_scaled_valid(sample.x) ||
       !bkmotion_scaled_valid(sample.y) || !bkmotion_scaled_valid(sample.z)))
    {
      result = -ERANGE;
    }

  close_result = ops->close(context);
  /* The operation result is the first error after a successful open.  Close
   * is still mandatory, but may only become the reported error when reading
   * and sample validation succeeded.
   */

  if (result < 0)
    {
      response->operation_status = result;
      return result;
    }

  if (close_result < 0)
    {
      response->operation_status = close_result;
      return close_result;
    }

  response->timestamp_us = sample.timestamp_us;
  response->x_mms2 = bkmotion_scale_mms2(sample.x);
  response->y_mms2 = bkmotion_scale_mms2(sample.y);
  response->z_mms2 = bkmotion_scale_mms2(sample.z);
  response->sensor_status = sample.status;
  response->flags = BKMOTION_FLAG_SAMPLE_VALID;
  response->operation_status = 0;
  return 0;
}
