/****************************************************************************
 * app/bk7258/bk7258_voice_turn.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Transport- and hardware-independent BKVoice half-duplex turn arbiter.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_TURN_H
#define __APP_BK7258_BK7258_VOICE_TURN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum bkvoice_turn_state_e
{
  BKVOICE_TURN_IDLE = 0,
  BKVOICE_TURN_CAPTURING,
  BKVOICE_TURN_WAITING_TTS,
  BKVOICE_TURN_PLAYING,
  BKVOICE_TURN_FAULTED,
};

struct bkvoice_turn_token_s
{
  uint32_t boot_generation;
  uint32_t session_id;
  uint32_t turn_id;
  uint32_t sequence;
};

struct bkvoice_turn_limits_s
{
  uint64_t capture_timeout_ms;
  uint64_t waiting_tts_timeout_ms;
  uint64_t playback_timeout_ms;
  size_t audio_frame_bytes;
};

struct bkvoice_turn_audio_ops_s
{
  int (*mic_acquire)(void *context);
  int (*mic_prepare)(void *context);
  int (*mic_start)(void *context);
  int (*mic_stop)(void *context);
  int (*mic_drain)(void *context);
  int (*mic_release)(void *context);

  int (*dac_acquire)(void *context);
  int (*dac_prepare)(void *context);
  int (*dac_start)(void *context);
  ssize_t (*dac_write)(void *context, const uint8_t *pcm, size_t bytes);
  int (*dac_drain)(void *context);
  int (*dac_stop)(void *context);
  int (*dac_release)(void *context);
};

/* release() is the terminal ownership operation.  A backend may return
 * success only after the device is quiescent and no longer reserved, even
 * when an earlier stop/drain operation failed.  Otherwise it must fail and
 * the arbiter remains FAULTED until recover() completes the release.
 */

struct bkvoice_turn_snapshot_s
{
  struct bkvoice_turn_token_s active;
  enum bkvoice_turn_state_e state;
  uint32_t session_id;
  uint32_t last_session_id;
  uint32_t last_control_sequence;
  uint32_t last_downlink_sequence;
  uint64_t deadline_ms;
  int last_error;
  bool mic_acquired;
  bool mic_prepared;
  bool mic_started;
  bool dac_acquired;
  bool dac_prepared;
  bool dac_started;
};

struct bkvoice_turn_s
{
  struct bkvoice_turn_audio_ops_s ops;
  struct bkvoice_turn_limits_s limits;
  struct bkvoice_turn_token_s active;
  void *audio_context;
  uint32_t boot_generation;
  uint32_t session_id;
  uint32_t last_session_id;
  uint32_t last_turn_id;
  uint32_t last_control_sequence;
  uint32_t last_downlink_sequence;
  uint64_t deadline_ms;
  int last_error;
  enum bkvoice_turn_state_e state;
  bool mic_acquired;
  bool mic_prepared;
  bool mic_started;
  bool dac_acquired;
  bool dac_prepared;
  bool dac_started;
};

/* The owner must serialize all calls.  The arbiter deliberately owns no
 * mutex, task, transport, device path or board resource.  session_open() is
 * explicit and session IDs never move backwards.  Control and downlink event
 * sequences are independent arbiter-local counters: each starts at one and
 * advances by exactly one.  A transport that filters unrelated wire frames
 * must assign these counters after its own wire-sequence validation.  Any
 * event with a matching token and the exact next sequence consumes that
 * sequence even when the current state rejects the event.  Replays, gaps and
 * foreign tokens do not consume a sequence.  Any -EOVERFLOW result closes
 * the session and is terminal for it.
 */

int bkvoice_turn_initialize(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_audio_ops_s *ops,
  void *audio_context,
  const struct bkvoice_turn_limits_s *limits,
  uint32_t boot_generation);
int bkvoice_turn_session_open(struct bkvoice_turn_s *turn,
                              uint32_t session_id);
int bkvoice_turn_session_close(struct bkvoice_turn_s *turn, int reason);
int bkvoice_turn_ptt_down(
  struct bkvoice_turn_s *turn, uint32_t session_id,
  uint32_t control_sequence, uint64_t now_ms,
  struct bkvoice_turn_token_s *token);
int bkvoice_turn_ptt_up(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token,
  uint64_t now_ms);
int bkvoice_turn_tts_start(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token,
  uint64_t now_ms);
int bkvoice_turn_tts_audio(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token,
  const uint8_t *pcm, size_t bytes, uint64_t now_ms);
int bkvoice_turn_tts_end(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token);
int bkvoice_turn_cancel(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token, int reason);
int bkvoice_turn_timeout(struct bkvoice_turn_s *turn, uint64_t now_ms);
int bkvoice_turn_recover(struct bkvoice_turn_s *turn);
void bkvoice_turn_snapshot(
  const struct bkvoice_turn_s *turn,
  struct bkvoice_turn_snapshot_s *snapshot);
const char *bkvoice_turn_state_name(enum bkvoice_turn_state_e state);

#endif /* __APP_BK7258_BK7258_VOICE_TURN_H */
