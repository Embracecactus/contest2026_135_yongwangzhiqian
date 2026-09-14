/****************************************************************************
 * app/bk7258/bk7258_voice_session.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Serialized BKVoice product session owner.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_SESSION_H
#define __APP_BK7258_BK7258_VOICE_SESSION_H

#include "bk7258_voice_gateway.h"
#include "bk7258_voice_ptt.h"

#include <stdbool.h>
#include <stdint.h>

struct bkvoice_session_config_s
{
  struct bkvoice_gateway_config_s gateway;
  uint32_t initial_downlink_credit;
};

struct bkvoice_session_snapshot_s
{
  struct bkvoice_gateway_snapshot_s gateway;
  struct bkvoice_ptt_snapshot_s ptt;
  int last_error;
  bool initialized;
  bool connected;
  bool ready;
  bool terminal_pending;
};

struct bkvoice_session_s
{
  struct bkvoice_gateway_s gateway;
  struct bkvoice_ptt_s *ptt;
  struct bkvoice_session_config_s config;
  volatile int terminal_state;
  uint32_t terminal_session_id;
  uint32_t terminal_turn_id;
  int terminal_reason;
  int last_error;
  bool initialized;
  bool connected;
  bool ready;
};

/* All public calls except interrupt() are made by one serialized product
 * owner.  The capture worker may concurrently use the gateway sink.  A
 * transport/link callback or physical-button callback must only wake that
 * owner; it must not call the PTT methods itself.
 *
 * connect() sends HELLO.  dispatch_frame() consumes WELCOME and then opens
 * the PTT capture sink and grants the initial downlink window.  A terminal
 * gateway callback is deferred until gateway dispatch has returned, avoiding
 * re-entry while the gateway lock is held.
 *
 * Before disconnect(), a product with a separate receive task calls
 * interrupt() and joins that task.  disconnect() then performs the required
 * capture/turn cleanup before closing the transport.
 */

int bkvoice_session_initialize(
  struct bkvoice_session_s *session, struct bkvoice_ptt_s *ptt,
  const struct bkvoice_transport_ops_s *transport_ops,
  void *transport_context, bkvoice_gateway_now_ms_t now_ms,
  void *clock_context, const struct bkvoice_session_config_s *config,
  uint32_t boot_generation);
int bkvoice_session_uninitialize(struct bkvoice_session_s *session);
int bkvoice_session_connect(struct bkvoice_session_s *session,
                            uint64_t deadline_ms);
int bkvoice_session_interrupt(struct bkvoice_session_s *session);
int bkvoice_session_disconnect(struct bkvoice_session_s *session,
                               int reason);
/* The RX worker calls gateway_receive_frame(&session->gateway, ...), then
 * queues its envelope.  Only the serialized owner calls this dispatcher;
 * stale envelopes return -ESTALE without affecting readiness or last_error.
 */

int bkvoice_session_dispatch_frame(
  struct bkvoice_session_s *session,
  const struct bkvoice_gateway_frame_s *frame);
int bkvoice_session_receive_one(struct bkvoice_session_s *session,
                                uint64_t deadline_ms);
int bkvoice_session_ptt_down(struct bkvoice_session_s *session,
                             uint64_t now_ms,
                             struct bkvoice_turn_token_s *token);
int bkvoice_session_ptt_down_prefill(
  struct bkvoice_session_s *session, uint64_t now_ms,
  bkvoice_capture_prefill_read_t read_frame, void *prefill_context,
  size_t prefill_frames, bkvoice_capture_frame_filter_t frame_filter,
  void *live_context, struct bkvoice_turn_token_s *token);
int bkvoice_session_ptt_up(struct bkvoice_session_s *session,
                           uint64_t now_ms);
int bkvoice_session_cancel(struct bkvoice_session_s *session, int reason);
int bkvoice_session_poll(struct bkvoice_session_s *session);
int bkvoice_session_timeout(struct bkvoice_session_s *session,
                            uint64_t now_ms);
void bkvoice_session_snapshot(struct bkvoice_session_s *session,
                              struct bkvoice_session_snapshot_s *snapshot);

#endif /* __APP_BK7258_BK7258_VOICE_SESSION_H */
