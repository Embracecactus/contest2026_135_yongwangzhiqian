/****************************************************************************
 * app/bk7258/bk7258_display_protocol.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned CP command to AP display-service wire contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_DISPLAY_PROTOCOL_H
#define __APP_BK7258_BK7258_DISPLAY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "bk7258_display_pack.h"

#define BKDISPLAY_RPC_MAGIC            0x4453504cu /* DSPL */
#define BKDISPLAY_RPC_VERSION          1u
#define BKDISPLAY_RPC_ENDPOINT         "bkdisplay-v1"
#define BKDISPLAY_RPC_ENDPOINT_WAIT_MS 3000u
#define BKDISPLAY_RPC_SEND_WAIT_MS     1000u
#define BKDISPLAY_RPC_REPLY_WAIT_MS    10000u
#define BKDISPLAY_RPC_ATTEMPTS         2u

#define BKDISPLAY_STATUS_SERVICE_READY    (1u << 0)
#define BKDISPLAY_STATUS_MAPPING_VERIFIED (1u << 1)
#define BKDISPLAY_STATUS_PACK_SELECTED    (1u << 2)

enum bkdisplay_rpc_command_e
{
  BKDISPLAY_RPC_STATUS = 1,
  BKDISPLAY_RPC_SET_EXPRESSION = 2,
  BKDISPLAY_RPC_SHOW_MAPPING_TEST = 3,
  BKDISPLAY_RPC_RESPONSE = 0x8000,
};

enum bkdisplay_rpc_state_e
{
  BKDISPLAY_RPC_STATE_STOPPED = 0,
  BKDISPLAY_RPC_STATE_WAITING_DEVICES = 1,
  BKDISPLAY_RPC_STATE_WAITING_ASSET = 2,
  BKDISPLAY_RPC_STATE_READY = 3,
  BKDISPLAY_RPC_STATE_ERROR = 4,
};

struct bkdisplay_rpc_request_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  char expression[BKDISPLAY_EXPRESSION_SIZE];
  uint16_t reserved;
};

struct bkdisplay_rpc_response_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  int32_t status;
  uint32_t state;
  int32_t last_error;
  uint32_t flags;
  uint32_t screen_count;
  uint32_t render_sequence;
  uint32_t pack_revision;
  char expression[BKDISPLAY_EXPRESSION_SIZE];
  uint16_t reserved;
  char pack_id[BKDISPLAY_PACK_ID_SIZE];
};

/* CP and AP are separate images.  Keep wire drift a compile-time failure.
 * All integer fields use the native little-endian BK7258 representation.
 */

_Static_assert(sizeof(struct bkdisplay_rpc_request_s) == 40,
               "bkdisplay request wire size changed");
_Static_assert(offsetof(struct bkdisplay_rpc_request_s, expression) == 16,
               "bkdisplay request expression offset changed");
_Static_assert(offsetof(struct bkdisplay_rpc_request_s, reserved) == 38,
               "bkdisplay request reserved offset changed");
_Static_assert(sizeof(struct bkdisplay_rpc_response_s) == 100,
               "bkdisplay response wire size changed");
_Static_assert(offsetof(struct bkdisplay_rpc_response_s, status) == 16,
               "bkdisplay response status offset changed");
_Static_assert(offsetof(struct bkdisplay_rpc_response_s, expression) == 44,
               "bkdisplay response expression offset changed");
_Static_assert(offsetof(struct bkdisplay_rpc_response_s, pack_id) == 68,
               "bkdisplay response pack offset changed");

int bkdisplay_rpc_client_initialize(void);
int bkdisplay_rpc_exchange(struct bkdisplay_rpc_request_s *request,
                           struct bkdisplay_rpc_response_s *response,
                           unsigned int timeout_ms);

#endif /* __APP_BK7258_BK7258_DISPLAY_PROTOCOL_H */
