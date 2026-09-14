/****************************************************************************
 * app/bk7258/bk7258_voice_capture.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Task- and transport-independent BKVoice capture/uplink frame pump.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_CAPTURE_H
#define __APP_BK7258_BK7258_VOICE_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "bk7258_voice_companion.h"
#include "bk7258_voice_turn.h"

#define BKVOICE_CAPTURE_FRAME_BYTES BKVOICE_COMPANION_AUDIO_FRAME_BYTES
#define BKVOICE_CAPTURE_MAX_PREFILL_FRAMES 50u

typedef int (*bkvoice_capture_prefill_read_t)(
  void *context, size_t frame_index,
  uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES]);

typedef int (*bkvoice_capture_frame_filter_t)(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES]);

enum bkvoice_capture_state_e
{
  BKVOICE_CAPTURE_IDLE = 0,
  BKVOICE_CAPTURE_RUNNING,
  BKVOICE_CAPTURE_STOPPING,
  BKVOICE_CAPTURE_STOPPED,
  BKVOICE_CAPTURE_FAULTED,
};

struct bkvoice_capture_source_ops_s
{
  int (*attach)(void *context);
  ssize_t (*read)(void *context, void *pcm, size_t bytes);
  int (*interrupt)(void *context);
  int (*detach)(void *context);
};

struct bkvoice_capture_sink_ops_s
{
  int (*start)(void *context,
               const struct bkvoice_turn_token_s *token);
  /* Zero accepts a frame and continues; positive accepts the final live
   * frame. run() then detaches without publishing end or releasing the MIC:
   * the owner must still join and complete the turn. Prefill requires zero.
   */
  int (*audio)(void *context,
               const struct bkvoice_turn_token_s *token,
               const uint8_t *pcm, size_t bytes);
  int (*end)(void *context,
             const struct bkvoice_turn_token_s *token);
  int (*cancel)(void *context,
                const struct bkvoice_turn_token_s *token,
                int reason);
};

struct bkvoice_capture_snapshot_s
{
  struct bkvoice_turn_token_s token;
  enum bkvoice_capture_state_e state;
  uint32_t frames_sent;
  uint32_t prefill_frames_sent;
  uint32_t bytes_sent;
  size_t partial_bytes_discarded;
  int last_error;
  bool source_attached;
  bool sink_started;
  bool stop_requested;
  bool run_started;
};

struct bkvoice_capture_s
{
  struct bkvoice_capture_source_ops_s source_ops;
  struct bkvoice_capture_sink_ops_s sink_ops;
  struct bkvoice_turn_token_s token;
  void *source_context;
  void *sink_context;
  bkvoice_capture_frame_filter_t frame_filter;
  void *frame_filter_context;
  uint8_t frame[BKVOICE_CAPTURE_FRAME_BYTES];
  uint32_t frames_sent;
  uint32_t prefill_frames_sent;
  uint32_t bytes_sent;
  size_t frame_fill;
  size_t partial_bytes_discarded;
  int last_error;
  enum bkvoice_capture_state_e state;
  bool source_attached;
  bool sink_started;
  bool stop_requested;
  bool run_started;
};

/* The product owner serializes initialize/start/complete/cancel.  run() is
 * executed by the capture worker.  A PTT-release owner calls request_stop(),
 * then joins run(), asks the turn arbiter to drain/release the MIC, and only
 * then calls complete() to publish TURN_END.  This ordering prevents a fast
 * Gateway downlink from racing a still-reserved microphone.
 */

int bkvoice_capture_initialize(
  struct bkvoice_capture_s *capture,
  const struct bkvoice_capture_source_ops_s *source_ops,
  void *source_context,
  const struct bkvoice_capture_sink_ops_s *sink_ops,
  void *sink_context);
int bkvoice_capture_start(
  struct bkvoice_capture_s *capture,
  const struct bkvoice_turn_token_s *token);
/* Supply oldest-to-newest history after start() has emitted TURN_START and
 * before run() starts the live reader.  This synchronous owner-only phase is
 * bounded to one second of product PCM.  A callback or sink failure stops and
 * detaches the source, cancels the matching sink turn, and preserves the first
 * error for the product owner.
 */

int bkvoice_capture_prefill(
  struct bkvoice_capture_s *capture,
  bkvoice_capture_prefill_read_t read_frame, void *context,
  size_t frames);
/* Decide before a complete live frame reaches the sink: positive accepts,
 * zero discards, negative fails the capture. Prefill is a separate explicit
 * input and is excluded. The callback may publish a bounded event but must
 * not stop, complete or cancel capture from the worker context.
 */

int bkvoice_capture_set_frame_filter(
  struct bkvoice_capture_s *capture,
  bkvoice_capture_frame_filter_t observer, void *context);
int bkvoice_capture_run(struct bkvoice_capture_s *capture);
int bkvoice_capture_request_stop(struct bkvoice_capture_s *capture);
int bkvoice_capture_complete(struct bkvoice_capture_s *capture);
int bkvoice_capture_cancel(struct bkvoice_capture_s *capture, int reason);
void bkvoice_capture_snapshot(
  const struct bkvoice_capture_s *capture,
  struct bkvoice_capture_snapshot_s *snapshot);
const char *bkvoice_capture_state_name(enum bkvoice_capture_state_e state);

#endif /* __APP_BK7258_BK7258_VOICE_CAPTURE_H */
