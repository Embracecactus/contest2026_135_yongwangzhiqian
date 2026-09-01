/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_companion.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_voice_companion.h"

static struct bkvoice_companion_header_s fake_rx(
  uint8_t type, uint16_t flags, uint32_t payload_len,
  uint32_t session_id, uint32_t turn_id, uint32_t sequence)
{
  struct bkvoice_companion_header_s header;

  memset(&header, 0, sizeof(header));
  header.magic = BKVOICE_COMPANION_MAGIC;
  header.version = BKVOICE_COMPANION_VERSION;
  header.type = type;
  header.flags = flags;
  header.header_len = BKVOICE_COMPANION_HEADER_BYTES;
  header.payload_len = payload_len;
  header.boot_generation = 7;
  header.session_id = session_id;
  header.turn_id = turn_id;
  header.sequence = sequence;
  header.timestamp_ms = 123456789u + sequence;
  return header;
}

static void put_be32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value >> 24);
  data[1] = (uint8_t)(value >> 16);
  data[2] = (uint8_t)(value >> 8);
  data[3] = (uint8_t)value;
}

static void test_codec(void)
{
  uint8_t frame[BKVOICE_COMPANION_HEADER_BYTES +
                BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  struct bkvoice_companion_header_s source;
  struct bkvoice_companion_header_s decoded;
  const uint8_t *payload;
  size_t encoded;

  memset(audio, 0x5a, sizeof(audio));
  source = fake_rx(BKVOICE_COMPANION_AUDIO_DOWN,
                   BKVOICE_COMPANION_FLAG_SYNTHETIC,
                   sizeof(audio), 0x11223344u, 0x55667788u,
                   0x99aabbccu);
  source.boot_generation = 0x0a0b0c0du;
  source.timestamp_ms = UINT64_C(0x0102030405060708);
  assert(bkvoice_companion_encode(&source, audio, frame, sizeof(frame),
                                  &encoded) == 0);
  assert(encoded == sizeof(frame));
  assert(frame[0] == 'B' && frame[1] == 'K' && frame[2] == 'V' &&
         frame[3] == '1');
  assert(frame[20] == 0x11 && frame[21] == 0x22 &&
         frame[22] == 0x33 && frame[23] == 0x44);
  assert(frame[6] == 0x00 && frame[7] == 0x01);
  assert(frame[8] == 0x00 && frame[9] == 0x28);
  assert(frame[12] == 0x00 && frame[13] == 0x00 &&
         frame[14] == 0x02 && frame[15] == 0x80);
  assert(frame[16] == 0x0a && frame[17] == 0x0b &&
         frame[18] == 0x0c && frame[19] == 0x0d);
  assert(frame[24] == 0x55 && frame[25] == 0x66 &&
         frame[26] == 0x77 && frame[27] == 0x88);
  assert(frame[28] == 0x99 && frame[29] == 0xaa &&
         frame[30] == 0xbb && frame[31] == 0xcc);
  assert(frame[32] == 0x01 && frame[33] == 0x02 &&
         frame[34] == 0x03 && frame[35] == 0x04 &&
         frame[36] == 0x05 && frame[37] == 0x06 &&
         frame[38] == 0x07 && frame[39] == 0x08);
  assert(bkvoice_companion_decode(frame, encoded, &decoded, &payload) == 0);
  assert(memcmp(&source, &decoded, sizeof(source)) == 0);
  assert(memcmp(payload, audio, sizeof(audio)) == 0);

  frame[0] = 0;
  assert(bkvoice_companion_decode(frame, encoded, &decoded, &payload) ==
         -EPROTO);
  frame[0] = 'B';
  assert(bkvoice_companion_decode(frame, encoded - 1u, &decoded, &payload) ==
         -EMSGSIZE);

  source.flags = 0;
  assert(bkvoice_companion_encode(&source, audio, frame, sizeof(frame),
                                  &encoded) == -EACCES);

  source.type = BKVOICE_COMPANION_VISION_CHUNK;
  source.turn_id = 1;
  source.payload_len = 4;
  assert(bkvoice_companion_encode(&source, audio, frame, sizeof(frame),
                                  &encoded) == -ENOTSUP);

  source.type = BKVOICE_COMPANION_AUDIO_UP;
  source.flags = BKVOICE_COMPANION_FLAG_SYNTHETIC;
  source.payload_len = sizeof(audio);
  assert(bkvoice_companion_encode(&source, audio, frame, sizeof(frame),
                                  &encoded) == -EPROTO);

  source.flags = 0x8000u;
  assert(bkvoice_companion_encode(&source, audio, frame, sizeof(frame),
                                  &encoded) == -EPROTO);
  source.flags = 0;
  source.reserved = 1;
  assert(bkvoice_companion_encode(&source, audio, frame, sizeof(frame),
                                  &encoded) == -EPROTO);
  source.reserved = 0;
  source.payload_len = BKVOICE_COMPANION_MAX_PAYLOAD + 1u;
  assert(bkvoice_companion_encode(&source, audio, frame, sizeof(frame),
                                  &encoded) == -EPROTO);
}

static void test_fake_server(void)
{
  struct bkvoice_companion_session_s session;
  struct bkvoice_companion_header_s tx;
  struct bkvoice_companion_header_s rx;
  uint8_t window[4];
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  uint32_t first_turn;
  uint32_t first_session;
  uint32_t second_session;

  memset(audio, 0, sizeof(audio));
  assert(bkvoice_companion_session_init(&session, 7) == 0);
  assert(session.state == BKVOICE_COMPANION_DISCONNECTED);
  assert(strcmp(bkvoice_companion_state_name(session.state),
                "disconnected") == 0);

  assert(bkvoice_companion_session_connect(&session) == 0);
  first_session = session.session_id;
  rx = fake_rx(BKVOICE_COMPANION_WELCOME, 0, 0, first_session, 0, 1);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -EPERM);
  assert(session.rx_sequence == 0);
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_HELLO,
                                      0, NULL, 0, 100, &tx) == 0);
  assert(tx.sequence == 1 && tx.turn_id == 0);
  assert(session.state == BKVOICE_COMPANION_HELLO_SENT);
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_HELLO,
                                      0, NULL, 0, 101, &tx) == -EPERM);

  rx = fake_rx(BKVOICE_COMPANION_WELCOME, 0, 0, first_session, 0, 1);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == 0);
  assert(session.state == BKVOICE_COMPANION_IDLE);

  put_be32(window, 2u * BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  rx = fake_rx(BKVOICE_COMPANION_WINDOW_UPDATE, 0, sizeof(window),
               first_session, 0, 2);
  assert(bkvoice_companion_session_rx(&session, &rx, window) == 0);
  assert(session.tx_window == 2u * BKVOICE_COMPANION_AUDIO_FRAME_BYTES);

  assert(bkvoice_companion_session_tx(&session,
                                      BKVOICE_COMPANION_TURN_START,
                                      0, NULL, 0, 200, &tx) == 0);
  first_turn = tx.turn_id;
  assert(first_turn != 0 && session.state == BKVOICE_COMPANION_UPLINK);

  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_AUDIO_UP,
                                      0, audio, sizeof(audio), 220,
                                      &tx) == 0);
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_AUDIO_UP,
                                      0, audio, sizeof(audio), 240,
                                      &tx) == 0);
  assert(session.tx_window == 0);
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_AUDIO_UP,
                                      0, audio, sizeof(audio), 260,
                                      &tx) == -EAGAIN);
  assert(session.tx_sequence == 4);

  put_be32(window, BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  rx = fake_rx(BKVOICE_COMPANION_WINDOW_UPDATE, 0, sizeof(window),
               first_session, first_turn, 3);
  assert(bkvoice_companion_session_rx(&session, &rx, window) == 0);
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_AUDIO_UP,
                                      0, audio, sizeof(audio), 260,
                                      &tx) == 0);
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_TURN_END,
                                      0, NULL, 0, 280, &tx) == 0);
  assert(session.state == BKVOICE_COMPANION_THINKING);

  put_be32(window, 2u * BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  assert(bkvoice_companion_session_tx(
           &session, BKVOICE_COMPANION_WINDOW_UPDATE, 0, window,
           sizeof(window), 290, &tx) == 0);
  assert(session.rx_window == 2u * BKVOICE_COMPANION_AUDIO_FRAME_BYTES);

  rx = fake_rx(BKVOICE_COMPANION_TTS_START,
               BKVOICE_COMPANION_FLAG_SYNTHETIC, 0,
               first_session, first_turn, 4);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == 0);
  assert(session.state == BKVOICE_COMPANION_DOWNLINK);
  rx = fake_rx(BKVOICE_COMPANION_AUDIO_DOWN,
               BKVOICE_COMPANION_FLAG_SYNTHETIC, sizeof(audio),
               first_session, first_turn, 5);
  assert(bkvoice_companion_session_rx(&session, &rx, audio) == 0);
  rx.sequence = 6;
  assert(bkvoice_companion_session_rx(&session, &rx, audio) == 0);
  assert(session.rx_window == 0);
  rx.sequence = 7;
  assert(bkvoice_companion_session_rx(&session, &rx, audio) == -ENOBUFS);
  assert(session.rx_sequence == 6);

  put_be32(window, BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  assert(bkvoice_companion_session_tx(
           &session, BKVOICE_COMPANION_WINDOW_UPDATE, 0, window,
           sizeof(window), 310, &tx) == 0);
  assert(bkvoice_companion_session_rx(&session, &rx, audio) == 0);
  rx = fake_rx(BKVOICE_COMPANION_TTS_END,
               BKVOICE_COMPANION_FLAG_SYNTHETIC, 0,
               first_session, first_turn, 8);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == 0);
  assert(session.state == BKVOICE_COMPANION_IDLE);

  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -EALREADY);
  rx = fake_rx(BKVOICE_COMPANION_HEARTBEAT, 0, 0,
               first_session, 0, 10);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -EPROTO);
  rx.sequence = 9;
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == 0);

  assert(bkvoice_companion_session_tx(&session,
                                      BKVOICE_COMPANION_TURN_START,
                                      0, NULL, 0, 320, &tx) == 0);
  assert(tx.turn_id == first_turn + 1u);
  rx = fake_rx(BKVOICE_COMPANION_ERROR, 0, 0,
               first_session, first_turn, 10);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -ESTALE);
  assert(session.state == BKVOICE_COMPANION_UPLINK);
  assert(session.rx_sequence == 9);
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_CANCEL,
                                      0, NULL, 0, 321, &tx) == 0);
  assert(session.state == BKVOICE_COMPANION_IDLE);
  rx = fake_rx(BKVOICE_COMPANION_TTS_START,
               BKVOICE_COMPANION_FLAG_SYNTHETIC, 0,
               first_session, first_turn, 10);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -ESTALE);
  assert(session.rx_sequence == 9);

  bkvoice_companion_session_disconnect(&session);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -ENOTCONN);
  assert(bkvoice_companion_session_connect(&session) == 0);
  second_session = session.session_id;
  assert(second_session == first_session + 1u);
  rx = fake_rx(BKVOICE_COMPANION_WELCOME, 0, 0,
               first_session, 0, 1);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -ESTALE);
  rx.session_id = second_session;
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -EPERM);
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_HELLO,
                                      0, NULL, 0, 400, &tx) == 0);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == 0);
  assert(session.state == BKVOICE_COMPANION_IDLE);

  put_be32(window, BKVOICE_COMPANION_MAX_WINDOW);
  rx = fake_rx(BKVOICE_COMPANION_WINDOW_UPDATE, 0, sizeof(window),
               second_session, 0, 2);
  assert(bkvoice_companion_session_rx(&session, &rx, window) == 0);
  put_be32(window, 1);
  rx.sequence = 3;
  assert(bkvoice_companion_session_rx(&session, &rx, window) ==
         -EOVERFLOW);
  assert(session.rx_sequence == 2);

  put_be32(window, BKVOICE_COMPANION_MAX_WINDOW);
  assert(bkvoice_companion_session_tx(
           &session, BKVOICE_COMPANION_WINDOW_UPDATE, 0, window,
           sizeof(window), 410, &tx) == 0);
  put_be32(window, 1);
  assert(bkvoice_companion_session_tx(
           &session, BKVOICE_COMPANION_WINDOW_UPDATE, 0, window,
           sizeof(window), 411, &tx) == -EOVERFLOW);

  session.turn_id = UINT32_MAX;
  assert(bkvoice_companion_session_tx(&session,
                                      BKVOICE_COMPANION_TURN_START,
                                      0, NULL, 0, 420, &tx) == -EOVERFLOW);
  session.turn_id = 0;
  session.tx_sequence = UINT32_MAX - 1u;
  assert(bkvoice_companion_session_tx(&session,
                                      BKVOICE_COMPANION_HEARTBEAT,
                                      0, NULL, 0, 430, &tx) == -EOVERFLOW);

  session.rx_sequence = UINT32_MAX - 1u;
  rx = fake_rx(BKVOICE_COMPANION_HEARTBEAT, 0, 0,
               second_session, 0, UINT32_MAX);
  assert(bkvoice_companion_session_rx(&session, &rx, NULL) == -EOVERFLOW);

  bkvoice_companion_session_disconnect(&session);
  session.last_session_id = UINT32_MAX;
  assert(bkvoice_companion_session_connect(&session) == -EOVERFLOW);
}

static void test_connection_error(void)
{
  struct bkvoice_companion_session_s session;
  struct bkvoice_companion_header_s header;
  uint32_t first_session;

  assert(bkvoice_companion_session_init(&session, 7) == 0);
  assert(bkvoice_companion_session_connect(&session) == 0);
  first_session = session.session_id;
  assert(bkvoice_companion_session_tx(&session, BKVOICE_COMPANION_HELLO,
                                      0, NULL, 0, 1, &header) == 0);
  header = fake_rx(BKVOICE_COMPANION_WELCOME, 0, 0,
                   first_session, 0, 1);
  assert(bkvoice_companion_session_rx(&session, &header, NULL) == 0);

  header = fake_rx(BKVOICE_COMPANION_ERROR, 0, 0,
                   first_session, 0, 2);
  assert(bkvoice_companion_session_rx(&session, &header, NULL) == 0);
  assert(session.state == BKVOICE_COMPANION_DISCONNECTED);
  assert(session.session_id == 0 && session.rx_sequence == 0);
  assert(bkvoice_companion_session_connect(&session) == 0);
  assert(session.session_id == first_session + 1u);
}

int main(void)
{
  test_codec();
  test_fake_server();
  test_connection_error();
  printf("BKVOICE_COMPANION_HOST_PASS\n");
  return 0;
}
