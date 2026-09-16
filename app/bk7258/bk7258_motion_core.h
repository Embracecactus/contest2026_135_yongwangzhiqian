/****************************************************************************
 * app/bk7258/bk7258_motion_core.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_MOTION_CORE_H
#define __APP_BK7258_BK7258_MOTION_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "bk7258_motion_protocol.h"

/* Keep the policy core independent of the NuttX sensor implementation.  The
 * AP adapter translates one public struct sensor_accel into this value type;
 * host tests therefore exercise the wire policy without private driver data.
 */

struct bkmotion_sample_s
{
  uint64_t timestamp_us;
  float x;
  float y;
  float z;
  int32_t status;
};

struct bkmotion_source_ops_s
{
  int (*open)(void *context);
  int (*read)(void *context, struct bkmotion_sample_s *sample);
  int (*close)(void *context);
};

bool bkmotion_rpc_request_valid(const struct bkmotion_rpc_request_s *request);
bool bkmotion_rpc_response_valid(const struct bkmotion_rpc_response_s *response);
void bkmotion_rpc_make_response(struct bkmotion_rpc_response_s *response,
                                const struct bkmotion_rpc_request_s *request,
                                int rpc_status);
int bkmotion_rpc_handle_request(const struct bkmotion_rpc_request_s *request,
                                struct bkmotion_rpc_response_s *response,
                                const struct bkmotion_source_ops_s *ops,
                                void *context);

#endif /* __APP_BK7258_BK7258_MOTION_CORE_H */
