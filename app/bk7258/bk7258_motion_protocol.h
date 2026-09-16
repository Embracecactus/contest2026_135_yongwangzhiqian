/****************************************************************************
 * app/bk7258/bk7258_motion_protocol.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned CP command to AP single-sample accelerometer service contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_MOTION_PROTOCOL_H
#define __APP_BK7258_BK7258_MOTION_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BKMOTION_RPC_MAGIC            0x424b4d4fu /* BKMO */
#define BKMOTION_RPC_VERSION          1u
#define BKMOTION_RPC_ENDPOINT         "bkmotion-v1"
#define BKMOTION_RPC_ENDPOINT_WAIT_MS 3000u
#define BKMOTION_RPC_SEND_WAIT_MS     1000u
#define BKMOTION_RPC_REPLY_WAIT_MS    10000u
#define BKMOTION_RPC_ATTEMPTS         2u

#define BKMOTION_FLAG_SAMPLE_VALID (1u << 0)

enum bkmotion_rpc_command_e
{
  BKMOTION_RPC_SAMPLE = 1,
  BKMOTION_RPC_RESPONSE = 0x8000,
};

struct bkmotion_rpc_request_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  uint32_t reserved[2];
};

/* Values are scaled SI acceleration in millimetres per second squared.
 * This response intentionally carries neither raw sensor registers nor an
 * I2C device identity.
 */

struct bkmotion_rpc_response_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  int32_t rpc_status;
  int32_t operation_status;
  uint32_t flags;
  uint32_t reserved0;
  uint64_t timestamp_us;
  int32_t x_mms2;
  int32_t y_mms2;
  int32_t z_mms2;
  int32_t sensor_status;
  uint32_t reserved[2];
};

_Static_assert(sizeof(struct bkmotion_rpc_request_s) == 24,
               "bkmotion request wire size changed");
_Static_assert(offsetof(struct bkmotion_rpc_request_s, session) == 8,
               "bkmotion request session offset changed");
_Static_assert(sizeof(struct bkmotion_rpc_response_s) == 64,
               "bkmotion response wire size changed");
_Static_assert(offsetof(struct bkmotion_rpc_response_s, rpc_status) == 16,
               "bkmotion response status offset changed");
_Static_assert(offsetof(struct bkmotion_rpc_response_s, timestamp_us) == 32,
               "bkmotion response timestamp offset changed");
_Static_assert(offsetof(struct bkmotion_rpc_response_s, x_mms2) == 40,
               "bkmotion response acceleration offset changed");

int bkmotion_rpc_client_initialize(void);
int bkmotion_rpc_exchange(struct bkmotion_rpc_request_s *request,
                          struct bkmotion_rpc_response_s *response,
                          unsigned int timeout_ms);

#endif /* __APP_BK7258_BK7258_MOTION_PROTOCOL_H */
