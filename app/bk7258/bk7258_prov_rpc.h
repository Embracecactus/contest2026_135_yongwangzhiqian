/****************************************************************************
 * app/bk7258/bk7258_prov_rpc.h
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP/AP wire contract for the bounded per-device identity supply channel.
 * The CP console command receives one BPI1 record and forwards it to the
 * AP-owned provisioning store, which owns the on-chip filesystem worker and
 * the durable identity publication.
 ****************************************************************************/
#ifndef __APP_BK7258_BK7258_PROV_RPC_H
#define __APP_BK7258_BK7258_PROV_RPC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BKPROV_RPC_MAGIC             0x42505256u /* "BPRV" */
#define BKPROV_RPC_VERSION           1u
#define BKPROV_RPC_ENDPOINT          "bkprov-v1"
#define BKPROV_RPC_ENDPOINT_WAIT_MS  3000u
#define BKPROV_RPC_SEND_WAIT_MS      1000u
#define BKPROV_RPC_REPLY_WAIT_MS     15000u
#define BKPROV_RPC_RECORD_MIN        48u
#define BKPROV_RPC_RECORD_MAX        8192u
#define BKPROV_RPC_CHUNK_BYTES       256u

enum bkprov_rpc_command_e
{
  BKPROV_RPC_BEGIN = 1,
  BKPROV_RPC_DATA,
  BKPROV_RPC_COMMIT,
  BKPROV_RPC_STATUS,
  BKPROV_RPC_RESPONSE = 0x8000
};

/* BEGIN carries the record size, DATA one bounded chunk at an absolute
 * offset, COMMIT the publication request and STATUS a read-only probe.
 * The client never reuses a sequence number and the server accepts only a
 * contiguous transfer inside one session.
 */

struct bkprov_rpc_frame_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  uint32_t offset;
  uint32_t total;
  uint16_t length;
  uint16_t reserved;
  uint8_t data[BKPROV_RPC_CHUNK_BYTES];
};

/* accepted is the number of record bytes the server holds after this frame
 * (the identity size for a successful STATUS probe), detail carries a second
 * code when the status alone is ambiguous.  A negative status is a failure.
 */

struct bkprov_rpc_answer_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  int32_t status;
  uint32_t accepted;
  uint32_t detail;
  uint32_t reserved;
};

static inline bool bkprov_rpc_frame_valid(
  const struct bkprov_rpc_frame_s *frame)
{
  if (frame->magic != BKPROV_RPC_MAGIC ||
      frame->version != BKPROV_RPC_VERSION ||
      frame->session == 0 || frame->sequence == 0 || frame->reserved != 0)
    {
      return false;
    }

  switch (frame->command)
    {
      case BKPROV_RPC_BEGIN:
        return frame->offset == 0 && frame->length == 0 &&
               frame->total >= BKPROV_RPC_RECORD_MIN &&
               frame->total <= BKPROV_RPC_RECORD_MAX;

      case BKPROV_RPC_DATA:
        return frame->length >= 1 &&
               frame->length <= BKPROV_RPC_CHUNK_BYTES &&
               frame->total <= BKPROV_RPC_RECORD_MAX &&
               frame->offset < frame->total &&
               frame->offset + frame->length <= frame->total;

      case BKPROV_RPC_COMMIT:
        return frame->length == 0 && frame->offset == frame->total &&
               frame->total <= BKPROV_RPC_RECORD_MAX;

      case BKPROV_RPC_STATUS:
        return frame->offset == 0 && frame->length == 0 && frame->total == 0;

      default:
        return false;
    }
}

static inline bool bkprov_rpc_answer_valid(
  const struct bkprov_rpc_answer_s *answer)
{
  unsigned int command = answer->command & ~BKPROV_RPC_RESPONSE;

  return answer->magic == BKPROV_RPC_MAGIC &&
         answer->version == BKPROV_RPC_VERSION &&
         answer->session != 0 && answer->sequence != 0 &&
         (answer->command & BKPROV_RPC_RESPONSE) != 0 &&
         command >= BKPROV_RPC_BEGIN && command <= BKPROV_RPC_STATUS &&
         answer->status <= 0 && answer->reserved == 0;
}

static inline void bkprov_rpc_make_answer(
  struct bkprov_rpc_answer_s *answer,
  const struct bkprov_rpc_frame_s *frame, int status)
{
  memset(answer, 0, sizeof(*answer));
  answer->magic = BKPROV_RPC_MAGIC;
  answer->version = BKPROV_RPC_VERSION;
  answer->command = frame->command | BKPROV_RPC_RESPONSE;
  answer->session = frame->session;
  answer->sequence = frame->sequence;
  answer->status = status;
}

_Static_assert(sizeof(struct bkprov_rpc_frame_s) == 284,
               "bkprov frame wire size changed");
_Static_assert(sizeof(struct bkprov_rpc_answer_s) == 32,
               "bkprov answer wire size changed");

int bkprov_rpc_client_initialize(void);
int bkprov_rpc_exchange(struct bkprov_rpc_frame_s *frame,
                        struct bkprov_rpc_answer_s *answer,
                        unsigned int timeout_ms);

/* Supply one caller-validated BPI1 record; returns 0 only after the AP store
 * reports durable publication, and -EEXIST when a different identity exists.
 */

int bkprov_rpc_supply(const uint8_t *record, size_t size);

/* 0 with *size set when an identity is present, -ENOENT when positively
 * absent.  The record itself is never returned over this channel.
 */

int bkprov_rpc_status(size_t *size);

#endif /* __APP_BK7258_BK7258_PROV_RPC_H */
