/****************************************************************************
 * app/bk7258/bk7258_voice_companion.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Transport-independent companion-v1 framing and turn-state contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_COMPANION_H
#define __APP_BK7258_BK7258_VOICE_COMPANION_H

#include <stddef.h>
#include <stdint.h>

#define BKVOICE_COMPANION_MAGIC             0x424b5631u /* BKV1 */
#define BKVOICE_COMPANION_VERSION           1u
#define BKVOICE_COMPANION_HEADER_BYTES      40u
#define BKVOICE_COMPANION_AUDIO_FRAME_BYTES 640u
#define BKVOICE_COMPANION_MAX_PAYLOAD       (64u * 1024u)
#define BKVOICE_COMPANION_MAX_WINDOW        (256u * 1024u)

#define BKVOICE_COMPANION_FLAG_SYNTHETIC    (1u << 0)
#define BKVOICE_COMPANION_FLAG_END_OF_STREAM (1u << 1)
#define BKVOICE_COMPANION_FLAG_MASK         \
  (BKVOICE_COMPANION_FLAG_SYNTHETIC |       \
   BKVOICE_COMPANION_FLAG_END_OF_STREAM)

enum bkvoice_companion_type_e
{
  BKVOICE_COMPANION_HELLO = 1,
  BKVOICE_COMPANION_WELCOME,
  BKVOICE_COMPANION_TURN_START,
  BKVOICE_COMPANION_AUDIO_UP,
  BKVOICE_COMPANION_TURN_END,
  BKVOICE_COMPANION_TTS_START,
  BKVOICE_COMPANION_AUDIO_DOWN,
  BKVOICE_COMPANION_TTS_END,
  BKVOICE_COMPANION_VISION_START,
  BKVOICE_COMPANION_VISION_CHUNK,
  BKVOICE_COMPANION_VISION_END,
  BKVOICE_COMPANION_CANCEL,
  BKVOICE_COMPANION_ACK,
  BKVOICE_COMPANION_WINDOW_UPDATE,
  BKVOICE_COMPANION_HEARTBEAT,
  BKVOICE_COMPANION_ERROR,
};

enum bkvoice_companion_state_e
{
  BKVOICE_COMPANION_DISCONNECTED = 0,
  BKVOICE_COMPANION_CONNECTING,
  BKVOICE_COMPANION_HELLO_SENT,
  BKVOICE_COMPANION_IDLE,
  BKVOICE_COMPANION_UPLINK,
  BKVOICE_COMPANION_THINKING,
  BKVOICE_COMPANION_DOWNLINK,
};

struct bkvoice_companion_header_s
{
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t flags;
  uint16_t header_len;
  uint16_t reserved;
  uint32_t payload_len;
  uint32_t boot_generation;
  uint32_t session_id;
  uint32_t turn_id;
  uint32_t sequence;
  uint64_t timestamp_ms;
};

struct bkvoice_companion_session_s
{
  uint32_t boot_generation;
  uint32_t session_id;
  uint32_t turn_id;
  uint32_t tx_sequence;
  uint32_t rx_sequence;
  uint32_t tx_window;
  uint32_t rx_window;
  uint32_t last_session_id;
  enum bkvoice_companion_state_e state;
};

int bkvoice_companion_encode(
  const struct bkvoice_companion_header_s *header,
  const void *payload, uint8_t *frame, size_t frame_size,
  size_t *encoded_size);
int bkvoice_companion_decode(
  const uint8_t *frame, size_t frame_size,
  struct bkvoice_companion_header_s *header,
  const uint8_t **payload);

int bkvoice_companion_session_init(
  struct bkvoice_companion_session_s *session,
  uint32_t boot_generation);
int bkvoice_companion_session_connect(
  struct bkvoice_companion_session_s *session);
void bkvoice_companion_session_disconnect(
  struct bkvoice_companion_session_s *session);
int bkvoice_companion_session_tx(
  struct bkvoice_companion_session_s *session, uint8_t type,
  uint16_t flags, const uint8_t *payload, uint32_t payload_len,
  uint64_t timestamp_ms,
  struct bkvoice_companion_header_s *header);
int bkvoice_companion_session_rx(
  struct bkvoice_companion_session_s *session,
  const struct bkvoice_companion_header_s *header,
  const uint8_t *payload);

/* A connection-scoped ERROR (turn_id == 0) invalidates the current session.
 * Any -EOVERFLOW result is likewise terminal for that session; the transport
 * must disconnect and establish a fresh monotonically numbered session.
 */

const char *bkvoice_companion_state_name(
  enum bkvoice_companion_state_e state);

#endif /* __APP_BK7258_BK7258_VOICE_COMPANION_H */
