/****************************************************************************
 * app/bk7258/bk7258_voice_ptt.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Joinable BKVoice PTT capture-worker owner.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_PTT_H
#define __APP_BK7258_BK7258_VOICE_PTT_H

#include "bk7258_voice_capture.h"
#include "bk7258_voice_turn.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct bkvoice_ptt_worker_config_s
{
  size_t stack_size;
  int priority;
  uint32_t join_timeout_ms;
};

struct bkvoice_ptt_snapshot_s
{
  struct bkvoice_turn_snapshot_s turn;
  struct bkvoice_capture_snapshot_s capture;
  struct bkvoice_turn_token_s token;
  int worker_result;
  int last_error;
  bool session_open;
  bool capture_ready;
  bool worker_joinable;
};

struct bkvoice_ptt_s
{
  struct bkvoice_turn_s turn;
  struct bkvoice_capture_s capture;
  struct bkvoice_capture_source_ops_s source_ops;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_worker_config_s worker_config;
  void *source_context;
  pthread_t worker;
  sem_t worker_done;
  volatile int worker_result;
  int last_error;
  bool initialized;
  bool capture_ready;
  bool worker_joinable;
};

/* The caller serializes every public operation.  ptt_down() starts a
 * joinable worker which executes bkvoice_capture_run().  ptt_up() first
 * interrupts that worker, waits for a bounded join, releases the MIC through
 * the turn arbiter, and only then lets the sink publish TURN_END.  A join
 * timeout leaves the worker and MIC pinned so the caller may retry; it never
 * destroys a recorder still referenced by the worker.  cancel(), timeout(),
 * and session_close() use the same stop/join/capture/turn cleanup order.
 * Control-sequence exhaustion closes the session after releasing resources.
 *
 * A session is opened only after a real companion sink is ready.  Physical
 * button callbacks must enqueue a bounded control event for the serialized
 * owner; they must not call these functions or block on the worker directly.
 */

int bkvoice_ptt_initialize(
  struct bkvoice_ptt_s *ptt,
  const struct bkvoice_turn_audio_ops_s *audio_ops,
  void *audio_context,
  const struct bkvoice_capture_source_ops_s *source_ops,
  void *source_context,
  const struct bkvoice_turn_limits_s *turn_limits,
  const struct bkvoice_ptt_worker_config_s *worker_config,
  uint32_t boot_generation);
int bkvoice_ptt_uninitialize(struct bkvoice_ptt_s *ptt);
int bkvoice_ptt_session_open(
  struct bkvoice_ptt_s *ptt, uint32_t session_id,
  const struct bkvoice_capture_sink_ops_s *sink_ops,
  void *sink_context);
int bkvoice_ptt_session_close(struct bkvoice_ptt_s *ptt, int reason);
int bkvoice_ptt_cancel(struct bkvoice_ptt_s *ptt, int reason);
int bkvoice_ptt_timeout(struct bkvoice_ptt_s *ptt, uint64_t now_ms);
int bkvoice_ptt_down(struct bkvoice_ptt_s *ptt, uint64_t now_ms,
                     struct bkvoice_turn_token_s *token);
int bkvoice_ptt_down_stream(
  struct bkvoice_ptt_s *ptt, uint64_t now_ms,
  bkvoice_capture_prefill_read_t read_frame, void *prefill_context,
  size_t prefill_frames, bkvoice_capture_frame_filter_t frame_filter,
  void *live_context, struct bkvoice_turn_token_s *token);
int bkvoice_ptt_up(struct bkvoice_ptt_s *ptt, uint64_t now_ms);
/* Read-only OTA admission predicate.  The serialized owner may admit a
 * direct OTA only when no capture worker or turn/audio resource remains.
 * An initialized PTT without a capture session is quiescent only if its turn
 * and worker are also idle. */
bool bkvoice_ptt_quiescent(const struct bkvoice_ptt_s *ptt);
void bkvoice_ptt_snapshot(const struct bkvoice_ptt_s *ptt,
                          struct bkvoice_ptt_snapshot_s *snapshot);

#endif /* __APP_BK7258_BK7258_VOICE_PTT_H */
