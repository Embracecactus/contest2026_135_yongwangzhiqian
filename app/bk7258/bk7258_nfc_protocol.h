/****************************************************************************
 * app/bk7258/bk7258_nfc_protocol.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned CP command to AP NFC-presence service wire contract.
 ****************************************************************************/
#ifndef __APP_BK7258_BK7258_NFC_PROTOCOL_H
#define __APP_BK7258_BK7258_NFC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BKNFC_RPC_MAGIC            0x424b4e46u /* BKNF */
#define BKNFC_RPC_VERSION          1u
#define BKNFC_RPC_ENDPOINT         "bknfc-v1"
#define BKNFC_RPC_ENDPOINT_WAIT_MS 3000u
#define BKNFC_RPC_SEND_WAIT_MS     1000u
#define BKNFC_RPC_REPLY_WAIT_MS    10000u
#define BKNFC_RPC_ATTEMPTS         2u

enum bknfc_rpc_command_e
{
  BKNFC_RPC_SCAN = 1,
  BKNFC_RPC_HCE = 2, /* App SELECT response, never ownership approval */
  BKNFC_RPC_RESPONSE = 0x8000,
};

/* Privacy boundary: this response intentionally has no UID, card ID, or
 * payload field.  Presence alone is not an identity or authorization claim.
 */
struct bknfc_rpc_request_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  uint32_t reserved[2];
};

struct bknfc_rpc_response_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  int32_t rpc_status;
  int32_t operation_status;
  uint32_t present;
  uint32_t reserved[3];
};

_Static_assert(sizeof(struct bknfc_rpc_request_s) == 24,
               "bknfc request wire size changed");
_Static_assert(offsetof(struct bknfc_rpc_request_s, session) == 8,
               "bknfc request session offset changed");
_Static_assert(sizeof(struct bknfc_rpc_response_s) == 40,
               "bknfc response wire size changed");
_Static_assert(offsetof(struct bknfc_rpc_response_s, rpc_status) == 16,
               "bknfc response status offset changed");
_Static_assert(offsetof(struct bknfc_rpc_response_s, present) == 24,
               "bknfc response presence offset changed");

int bknfc_rpc_client_initialize(void);
int bknfc_rpc_exchange(struct bknfc_rpc_request_s *request,
                       struct bknfc_rpc_response_s *response,
                       unsigned int timeout_ms);
#endif /* __APP_BK7258_BK7258_NFC_PROTOCOL_H */
