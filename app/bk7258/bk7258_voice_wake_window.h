/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __APP_BK7258_BK7258_VOICE_WAKE_WINDOW_H
#define __APP_BK7258_BK7258_VOICE_WAKE_WINDOW_H

#include "bk7258_voice_kws_frontend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BKVOICE_WAKE_FRAME_SAMPLES BKVOICE_KWS_HOP
enum bkvoice_wake_window_state_e
{
  BKVOICE_WAKE_WINDOW_LISTENING = 0,
  BKVOICE_WAKE_WINDOW_WAITING_QUIET,
  BKVOICE_WAKE_WINDOW_WAITING_SPEECH,
  BKVOICE_WAKE_WINDOW_CAPTURING,
  BKVOICE_WAKE_WINDOW_COMPLETE,
  BKVOICE_WAKE_WINDOW_FAULTED,
};

enum bkvoice_wake_window_event_e
{
  BKVOICE_WAKE_EVENT_NONE = 0,
  BKVOICE_WAKE_EVENT_SPEECH_STARTED,
  BKVOICE_WAKE_EVENT_END_OF_SPEECH,
  BKVOICE_WAKE_EVENT_NO_SPEECH,
  BKVOICE_WAKE_EVENT_MAX_DURATION,
};

struct bkvoice_wake_window_policy_s
{
  uint32_t minimum_speech_mean_abs;
  uint16_t speech_to_noise_q8;
  uint16_t speech_confirm_frames;
  uint16_t wake_quiet_frames;
  uint16_t silence_end_frames;
  uint16_t no_speech_frames;
  uint16_t maximum_turn_frames;
};

struct bkvoice_wake_window_snapshot_s
{
  enum bkvoice_wake_window_state_e state;
  uint32_t noise_mean_abs;
  uint32_t speech_threshold;
  uint32_t last_mean_abs;
  uint32_t post_trigger_frames;
  uint16_t speech_frames;
  uint16_t silence_frames;
  uint64_t last_frame_ms;
  uint64_t trigger_ms;
};

struct bkvoice_wake_window_s
{
  struct bkvoice_wake_window_policy_s policy;
  uint32_t noise_mean_abs;
  uint32_t last_mean_abs;
  uint32_t post_trigger_frames;
  uint16_t speech_frames;
  uint16_t silence_frames;
  uint64_t last_frame_ms;
  uint64_t trigger_ms;
  enum bkvoice_wake_window_state_e state;
  int last_error;
  bool timestamp_valid;
};

/* This App-only core opens no recorder and does not perform wake inference.
 * While LISTENING, the single AP audio owner supplies each ordered 20 ms PCM
 * frame alongside KWS for noise estimation. It retains no wake PCM.
 * trigger() starts a bounded quiet-boundary wait; feed() then separates the
 * wake tail from user speech and provides the live endpoint decision.
 * If endpoint conditions collide on one frame, confirmed speech or silence
 * wins over the corresponding timeout; maximum duration ends any remaining
 * active turn.  All API calls must come from that one serialized owner.
 *
 * Thresholds are workload policy and require target calibration.  This core
 * is not a wake-word model and its energy gate must never be used as one.
 */

int bkvoice_wake_window_initialize(
  struct bkvoice_wake_window_s *window,
  const struct bkvoice_wake_window_policy_s *policy);
void bkvoice_wake_window_uninitialize(struct bkvoice_wake_window_s *window);
void bkvoice_wake_window_reset(struct bkvoice_wake_window_s *window);
int bkvoice_wake_window_observe(struct bkvoice_wake_window_s *window,
                                const int16_t *pcm, size_t samples,
                                uint64_t end_ms);
int bkvoice_wake_window_trigger(struct bkvoice_wake_window_s *window,
                                uint64_t end_ms);
int bkvoice_wake_window_feed(
  struct bkvoice_wake_window_s *window, const int16_t *pcm, size_t samples,
  uint64_t end_ms, enum bkvoice_wake_window_event_e *event);
void bkvoice_wake_window_snapshot(
  const struct bkvoice_wake_window_s *window,
  struct bkvoice_wake_window_snapshot_s *snapshot);

#ifdef __cplusplus
}
#endif
#endif
