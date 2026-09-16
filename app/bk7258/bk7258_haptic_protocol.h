/****************************************************************************
 * app/bk7258/bk7258_haptic_protocol.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_BK7258_HAPTIC_PROTOCOL_H
#define __APP_BK7258_BK7258_HAPTIC_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BKHAPTIC_RPC_MAGIC             0x424b4850u
#define BKHAPTIC_RPC_VERSION           1u
#define BKHAPTIC_RPC_ENDPOINT          "bkhaptic-v1"
#define BKHAPTIC_RPC_ENDPOINT_WAIT_MS  3000u
#define BKHAPTIC_RPC_SEND_WAIT_MS      1000u
#define BKHAPTIC_RPC_REPLY_WAIT_MS     3000u
#define BKHAPTIC_RPC_ATTEMPTS          2u

enum bkhaptic_rpc_command_e
{
  BKHAPTIC_RPC_STATUS = 1,
  BKHAPTIC_RPC_PULSE,
  BKHAPTIC_RPC_STOP,
  BKHAPTIC_RPC_RESPONSE = 0x8000
};

struct bkhaptic_rpc_request_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  uint32_t duration_ms;
  uint32_t reserved[2];
};

/* accepted_ms acknowledges the playback request only.  It is not a claim
 * that the asynchronous lower half energized hardware or completed a pulse.
 * ready means that the standard FF capability query succeeded.
 */

struct bkhaptic_rpc_response_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  int32_t status;
  uint32_t accepted_ms;
  uint32_t ready;
  uint32_t reserved;
};

_Static_assert(sizeof(struct bkhaptic_rpc_request_s) == 28,
               "bkhaptic request wire size changed");
_Static_assert(sizeof(struct bkhaptic_rpc_response_s) == 32,
               "bkhaptic response wire size changed");

static inline bool bkhaptic_rpc_request_valid(
  const struct bkhaptic_rpc_request_s *r)
{
  return r->magic == BKHAPTIC_RPC_MAGIC &&
         r->version == BKHAPTIC_RPC_VERSION && r->session && r->sequence &&
         !r->reserved[0] && !r->reserved[1] &&
         ((r->command == BKHAPTIC_RPC_PULSE &&
           r->duration_ms >= 1 && r->duration_ms <= 32767) ||
          ((r->command == BKHAPTIC_RPC_STATUS ||
            r->command == BKHAPTIC_RPC_STOP) && r->duration_ms == 0));
}

static inline bool bkhaptic_rpc_response_valid(
  const struct bkhaptic_rpc_response_s *r)
{
  unsigned int command = r->command & ~BKHAPTIC_RPC_RESPONSE;
  return r->magic == BKHAPTIC_RPC_MAGIC &&
         r->version == BKHAPTIC_RPC_VERSION && r->session && r->sequence &&
         (r->command & BKHAPTIC_RPC_RESPONSE) &&
         command >= BKHAPTIC_RPC_STATUS && command <= BKHAPTIC_RPC_STOP &&
         r->status <= 0 && r->ready <= 1 && !r->reserved &&
         (r->accepted_ms == 0 ||
          (command == BKHAPTIC_RPC_PULSE && r->status == 0 &&
           r->accepted_ms <= 32767));
}

static inline void bkhaptic_rpc_make_response(
  struct bkhaptic_rpc_response_s *r,
  const struct bkhaptic_rpc_request_s *request, int status)
{
  memset(r, 0, sizeof(*r));
  r->magic = BKHAPTIC_RPC_MAGIC;
  r->version = BKHAPTIC_RPC_VERSION;
  r->command = request->command | BKHAPTIC_RPC_RESPONSE;
  r->session = request->session;
  r->sequence = request->sequence;
  r->status = status;
}

int bkhaptic_rpc_client_initialize(void);
int bkhaptic_rpc_exchange(struct bkhaptic_rpc_request_s *request,
                         struct bkhaptic_rpc_response_s *response,
                         unsigned int timeout_ms);
#endif
