/****************************************************************************
 * app/bk7258/bk7258_vision_protocol.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned CP command to AP snapshot/local recording service contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VISION_PROTOCOL_H
#define __APP_BK7258_BK7258_VISION_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BKVISION_RPC_MAGIC            0x424b564eu /* BKVN */
#define BKVISION_RPC_VERSION          2u
#define BKVISION_RPC_ENDPOINT         "bkvision-v2"
#define BKVISION_RECORD_MAX_SECONDS   60u
#define BKVISION_RECORD_NAME_FORMAT   "video-%08lx-%08lx.avi"
#define BKVISION_RPC_ENDPOINT_WAIT_MS 3000u
#define BKVISION_RPC_SEND_WAIT_MS     1000u
#define BKVISION_RPC_REPLY_WAIT_MS    10000u
#define BKVISION_RPC_ATTEMPTS         2u

/* V4L2_PIX_FMT_JPEG expressed as an endian-stable wire value. */

#define BKVISION_PIXEL_FORMAT_JPEG    0x4745504au

#define BKVISION_FLAG_JPEG_SOI (1u << 0)
#define BKVISION_FLAG_JPEG_EOI (1u << 1)
#define BKVISION_FLAG_V4L2_ERROR (1u << 2)
#define BKVISION_FLAG_RECORDED (1u << 3)
#define BKVISION_FLAG_CHECKED  (1u << 7)

enum bkvision_rpc_command_e
{
  BKVISION_RPC_SNAPSHOT = 1,
  BKVISION_RPC_RECORD = 2,
  /* Values 3..5 remain retired; never reuse wire command values. */
  BKVISION_RPC_CHECK_RECORD = 7,
  BKVISION_RPC_RESPONSE = 0x8000
};

struct bkvision_rpc_request_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session_id;
  uint32_t sequence;
  uint32_t duration_ms;
  uint32_t reserved;
};

struct bkvision_rpc_response_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session_id;
  uint32_t sequence;
  int32_t rpc_status;
  int32_t operation_status;
  uint32_t width;
  uint32_t height;
  uint32_t pixel_format;
  uint32_t bytes_used;
  uint32_t capture_sequence;
  uint32_t flags;
  uint32_t reserved[2];
};

/* No pixels or pointers cross RPMsg. RECORD success uses reserved[0] for
 * frames and reserved[1] for elapsed milliseconds; bytes_used is AVI size.
 * Its filename is derived from the echoed session_id and sequence.
 * CHECK_RECORD addresses the same generated filename and mounts read-only.
 * Its response reports header frames in reserved[0], full-file FNV-1a in
 * reserved[1], and bytes_used. It is an inspection, not a JPEG decode.
 * Only one generated AVI filename is addressable, with no paths or wildcards.
 */

_Static_assert(sizeof(struct bkvision_rpc_request_s) == 24,
               "bkvision request wire size changed");
_Static_assert(sizeof(struct bkvision_rpc_response_s) == 56,
               "bkvision response wire size changed");
_Static_assert(offsetof(struct bkvision_rpc_response_s, rpc_status) == 16,
               "bkvision response status offset changed");
_Static_assert(offsetof(struct bkvision_rpc_response_s, flags) == 44,
               "bkvision response flags offset changed");

int bkvision_rpc_client_initialize(void);
int bkvision_rpc_exchange(struct bkvision_rpc_request_s *request,
                          struct bkvision_rpc_response_s *response,
                          unsigned int timeout_ms);

#endif /* __APP_BK7258_BK7258_VISION_PROTOCOL_H */
