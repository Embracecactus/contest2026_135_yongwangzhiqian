/****************************************************************************
 * app/bk7258/bk7258_voice_session.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_voice_session.h"

#include <errno.h>
#include <string.h>

enum bkvoice_terminal_state_e
{
  BKVOICE_TERMINAL_EMPTY = 0,
  BKVOICE_TERMINAL_WRITING,
  BKVOICE_TERMINAL_READY,
};

static void bkvoice_session_first_error(int *first, int ret)
{
  if (*first >= 0 && ret < 0)
    {
      *first = ret;
    }
}

static int bkvoice_session_tts_start(
  void *context, const struct bkvoice_turn_token_s *token, uint64_t now_ms)
{
  struct bkvoice_session_s *session = context;

  if (session == NULL || !session->ready)
    {
      return -ENOTCONN;
    }

  return bkvoice_turn_tts_start(&session->ptt->turn, token, 16000, now_ms);
}

static int bkvoice_session_tts_audio(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t *pcm, size_t bytes, uint64_t now_ms)
{
  struct bkvoice_session_s *session = context;

  if (session == NULL || !session->ready)
    {
      return -ENOTCONN;
    }

  return bkvoice_turn_tts_audio(&session->ptt->turn, token, pcm, bytes,
                                now_ms);
}

static int bkvoice_session_tts_end(
  void *context, const struct bkvoice_turn_token_s *token)
{
  struct bkvoice_session_s *session = context;

  if (session == NULL || !session->ready)
    {
      return -ENOTCONN;
    }

  return bkvoice_turn_tts_end(&session->ptt->turn, token);
}

static int bkvoice_session_terminal(void *context, uint32_t session_id,
                                    uint32_t turn_id, int reason)
{
  struct bkvoice_session_s *session = context;
  int expected = BKVOICE_TERMINAL_EMPTY;

  if (session == NULL || session_id == 0 || turn_id == 0 || reason >= 0)
    {
      return -EINVAL;
    }

  if (!__atomic_compare_exchange_n(&session->terminal_state, &expected,
                                   BKVOICE_TERMINAL_WRITING, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return expected == BKVOICE_TERMINAL_READY ||
             expected == BKVOICE_TERMINAL_WRITING ? 0 : -EIO;
    }

  session->terminal_session_id = session_id;
  session->terminal_turn_id = turn_id;
  session->terminal_reason = reason;
  __atomic_store_n(&session->terminal_state, BKVOICE_TERMINAL_READY,
                   __ATOMIC_RELEASE);
  return 0;
}

static int bkvoice_session_volume(void *context, bool set,
                                  unsigned int requested,
                                  unsigned int *observed)
{
  struct bkvoice_session_s *session = context;
  if (!session->ready || session->ptt->turn.ops.volume == NULL)
    {
      return -ENOTSUP;
    }

  return session->ptt->turn.ops.volume(session->ptt->turn.audio_context,
                                      set, requested, observed);
}

static const struct bkvoice_gateway_downlink_ops_s g_downlink_ops =
{
  .tts_start = bkvoice_session_tts_start,
  .tts_audio = bkvoice_session_tts_audio,
  .tts_end = bkvoice_session_tts_end,
  .terminal = bkvoice_session_terminal,
};

static int bkvoice_session_service_terminal(struct bkvoice_session_s *session)
{
  struct bkvoice_turn_snapshot_s turn;
  uint32_t terminal_session_id;
  uint32_t terminal_turn_id;
  int terminal_reason;
  int expected = BKVOICE_TERMINAL_READY;
  int ret = 0;

  if (!__atomic_compare_exchange_n(&session->terminal_state, &expected,
                                   BKVOICE_TERMINAL_WRITING, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  terminal_session_id = session->terminal_session_id;
  terminal_turn_id = session->terminal_turn_id;
  terminal_reason = session->terminal_reason;
  bkvoice_turn_snapshot(&session->ptt->turn, &turn);

  if (session->ready && terminal_reason == -ECANCELED &&
      turn.state != BKVOICE_TURN_FAULTED &&
      turn.state != BKVOICE_TURN_IDLE &&
      turn.active.session_id == terminal_session_id &&
      turn.active.turn_id == terminal_turn_id)
    {
      ret = bkvoice_ptt_cancel(session->ptt, terminal_reason);
    }
  else if (session->ready &&
           (terminal_reason != -ECANCELED ||
            turn.state == BKVOICE_TURN_FAULTED))
    {
      ret = bkvoice_ptt_session_close(session->ptt, terminal_reason);
      if (ret >= 0 || !session->ptt->capture_ready)
        {
          session->ready = false;
        }
    }

  /* A remote CANCEL receipt is sent only after the owner has joined capture
   * and released audio.  Posting the terminal event is not completion.
   */

  if (ret >= 0 && session->ready && terminal_reason == -ECANCELED)
    {
      bkvoice_turn_snapshot(&session->ptt->turn, &turn);
      if (turn.state == BKVOICE_TURN_IDLE && !session->ptt->worker_joinable)
        {
          ret = bkvoice_gateway_ack_stopped(&session->gateway,
                                           terminal_session_id,
                                           terminal_turn_id);
        }
    }

  session->terminal_session_id = 0;
  session->terminal_turn_id = 0;
  session->terminal_reason = 0;
  __atomic_store_n(&session->terminal_state, BKVOICE_TERMINAL_EMPTY,
                   __ATOMIC_RELEASE);
  return ret;
}

static int bkvoice_session_admit_ready(struct bkvoice_session_s *session)
{
  struct bkvoice_gateway_snapshot_s gateway;
  int ret;

  if (session->ready)
    {
      return 0;
    }

  bkvoice_gateway_snapshot(&session->gateway, &gateway);
  if (gateway.faulted)
    {
      return gateway.last_error < 0 ? gateway.last_error : -EIO;
    }

  if (gateway.companion_state != BKVOICE_COMPANION_IDLE ||
      gateway.session_id == 0)
    {
      return 0;
    }

  ret = bkvoice_ptt_session_open(session->ptt, gateway.session_id,
                                 bkvoice_gateway_capture_sink_ops(),
                                 &session->gateway);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_gateway_grant_downlink(
    &session->gateway, session->config.initial_downlink_credit);
  if (ret < 0)
    {
      (void)bkvoice_ptt_session_close(session->ptt, ret);
      return ret;
    }

  session->ready = true;
  return 0;
}

int bkvoice_session_initialize(
  struct bkvoice_session_s *session, struct bkvoice_ptt_s *ptt,
  const struct bkvoice_transport_ops_s *transport_ops,
  void *transport_context, bkvoice_gateway_now_ms_t now_ms,
  void *clock_context, const struct bkvoice_session_config_s *config,
  uint32_t boot_generation)
{
  struct bkvoice_gateway_downlink_ops_s downlink_ops = g_downlink_ops;
  int ret;

  if (session == NULL || ptt == NULL || !ptt->initialized ||
      config == NULL || config->gateway.io_timeout_ms == 0 ||
      config->initial_downlink_credit == 0 ||
      config->initial_downlink_credit > BKVOICE_COMPANION_MAX_WINDOW)
    {
      return -EINVAL;
    }

  memset(session, 0, sizeof(*session));
  downlink_ops.volume = ptt->turn.ops.volume != NULL ?
                        bkvoice_session_volume : NULL;
  ret = bkvoice_gateway_initialize(
    &session->gateway, transport_ops, transport_context, &downlink_ops,
    session, now_ms, clock_context, &config->gateway, boot_generation);
  if (ret < 0)
    {
      return ret;
    }

  session->ptt = ptt;
  memcpy(&session->config, config, sizeof(*config));
  session->initialized = true;
  return 0;
}

int bkvoice_session_uninitialize(struct bkvoice_session_s *session)
{
  int ret;

  if (session == NULL || !session->initialized)
    {
      return -EINVAL;
    }

  if (session->connected || session->ready || session->ptt->capture_ready)
    {
      return -EBUSY;
    }

  ret = bkvoice_gateway_uninitialize(&session->gateway);
  if (ret < 0)
    {
      return ret;
    }

  memset(session, 0, sizeof(*session));
  return 0;
}

int bkvoice_session_connect(struct bkvoice_session_s *session,
                            uint64_t deadline_ms)
{
  int ret;

  if (session == NULL || !session->initialized)
    {
      return -EINVAL;
    }

  if (session->connected)
    {
      return -EALREADY;
    }

  ret = bkvoice_gateway_connect(&session->gateway, deadline_ms);
  session->last_error = ret;
  if (ret >= 0)
    {
      session->connected = true;
    }

  return ret;
}

int bkvoice_session_interrupt(struct bkvoice_session_s *session)
{
  if (session == NULL || !session->initialized)
    {
      return -EINVAL;
    }

  return bkvoice_gateway_interrupt(&session->gateway);
}

int bkvoice_session_disconnect(struct bkvoice_session_s *session,
                               int reason)
{
  int first = 0;
  int ret;

  if (session == NULL || !session->initialized)
    {
      return -EINVAL;
    }

  if (reason >= 0)
    {
      reason = -ENOTCONN;
    }

  ret = bkvoice_session_service_terminal(session);
  bkvoice_session_first_error(&first, ret);
  if (session->ptt->capture_ready)
    {
      ret = bkvoice_ptt_session_close(session->ptt, reason);
      if (ret < 0 && session->ptt->capture_ready)
        {
          session->last_error = ret;
          return ret;
        }

      bkvoice_session_first_error(&first, ret);
    }

  session->ready = false;
  ret = bkvoice_gateway_disconnect(&session->gateway, reason);
  if (ret < 0)
    {
      session->last_error = ret;
      return ret;
    }

  (void)bkvoice_session_service_terminal(session);
  session->connected = false;
  session->last_error = first < 0 ? first : reason;
  return first;
}

int bkvoice_session_poll(struct bkvoice_session_s *session)
{
  struct bkvoice_turn_snapshot_s before;
  int ret;

  if (session == NULL || !session->initialized || !session->ready)
    {
      return 0;
    }

  bkvoice_turn_snapshot(&session->ptt->turn, &before);
  ret = bkvoice_turn_poll(&session->ptt->turn);
  if (ret >= 0 && before.state == BKVOICE_TURN_DRAINING &&
      session->ptt->turn.state == BKVOICE_TURN_IDLE)
    {
      ret = bkvoice_gateway_ack_stopped(&session->gateway,
                                        before.active.session_id,
                                        before.active.turn_id);
    }

  return ret;
}

int bkvoice_session_dispatch_frame(
  struct bkvoice_session_s *session,
  const struct bkvoice_gateway_frame_s *frame)
{
  int first;
  int ret;

  if (session == NULL || !session->initialized || !session->connected)
    {
      return -ENOTCONN;
    }

  if (frame == NULL)
    {
      return -EINVAL;
    }

  /* connect/disconnect and dispatch share this owner.  Reject stale work
   * before servicing terminal state or updating the new session's error.
   */

  if (frame->generation == 0 ||
      frame->generation != session->gateway.connection_generation)
    {
      return -ESTALE;
    }

  first = bkvoice_gateway_dispatch_frame(&session->gateway, frame);
  ret = bkvoice_session_service_terminal(session);
  bkvoice_session_first_error(&first, ret);
  if (first >= 0)
    {
      ret = bkvoice_session_admit_ready(session);
      bkvoice_session_first_error(&first, ret);
    }

  session->last_error = first;
  return first;
}

int bkvoice_session_receive_one(struct bkvoice_session_s *session,
                                uint64_t deadline_ms)
{
  struct bkvoice_gateway_frame_s frame;
  int ret;

  if (session == NULL || !session->initialized || !session->connected)
    {
      return -ENOTCONN;
    }

  ret = bkvoice_gateway_receive_frame(&session->gateway, &frame, deadline_ms);
  if (frame.generation == 0)
    {
      return ret;
    }

  return bkvoice_session_dispatch_frame(session, &frame);
}

int bkvoice_session_ptt_down(struct bkvoice_session_s *session,
                             uint64_t now_ms,
                             struct bkvoice_turn_token_s *token)
{
  int ret;

  if (session == NULL || !session->initialized || !session->ready)
    {
      return -ENOTCONN;
    }

  ret = bkvoice_ptt_down(session->ptt, now_ms, token);
  session->last_error = ret;
  return ret;
}

int bkvoice_session_ptt_down_prefill(
  struct bkvoice_session_s *session, uint64_t now_ms,
  bkvoice_capture_prefill_read_t read_frame, void *prefill_context,
  size_t prefill_frames, bkvoice_capture_frame_filter_t frame_filter,
  void *live_context, struct bkvoice_turn_token_s *token)
{
  int ret;

  if (session == NULL || !session->initialized || !session->ready)
    {
      return -ENOTCONN;
    }

  ret = bkvoice_ptt_down_stream(
    session->ptt, now_ms, read_frame, prefill_context, prefill_frames,
    frame_filter, live_context, token);
  session->last_error = ret;
  return ret;
}

int bkvoice_session_ptt_up(struct bkvoice_session_s *session,
                           uint64_t now_ms)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  int ret;

  if (session == NULL || !session->initialized || !session->ready)
    {
      return -ENOTCONN;
    }

  ret = bkvoice_ptt_up(session->ptt, now_ms);

  /* A remote CANCEL may finish and acknowledge the active turn while the
   * physical button is still held.  Its later release is the same event's
   * tail, not a new protocol error.  Keep the live session usable while
   * preserving -EPERM for every non-idle or partially-owned state.
   */

  if (ret == -EPERM)
    {
      bkvoice_ptt_snapshot(session->ptt, &snapshot);
      if (snapshot.turn.state == BKVOICE_TURN_IDLE &&
          !snapshot.worker_joinable && snapshot.capture_ready)
        {
          ret = 0;
        }
    }

  session->last_error = ret;
  return ret;
}

int bkvoice_session_cancel(struct bkvoice_session_s *session, int reason)
{
  int ret;

  if (session == NULL || !session->initialized || !session->ready)
    {
      return -ENOTCONN;
    }

  ret = bkvoice_ptt_cancel(session->ptt, reason);
  session->last_error = ret;
  return ret;
}

int bkvoice_session_timeout(struct bkvoice_session_s *session,
                            uint64_t now_ms)
{
  int ret;

  if (session == NULL || !session->initialized || !session->ready)
    {
      return -ENOTCONN;
    }

  ret = bkvoice_ptt_timeout(session->ptt, now_ms);
  /* The owner polls every tick, including idle and unexpired turns.  These
   * are normal no-op outcomes, not reasons to tear down the connection.
   */

  if (ret == -EALREADY || ret == -EAGAIN)
    {
      return 0;
    }

  session->last_error = ret;
  return ret;
}

void bkvoice_session_snapshot(struct bkvoice_session_s *session,
                              struct bkvoice_session_snapshot_s *snapshot)
{
  if (session == NULL || snapshot == NULL || !session->initialized)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  bkvoice_gateway_snapshot(&session->gateway, &snapshot->gateway);
  bkvoice_ptt_snapshot(session->ptt, &snapshot->ptt);
  snapshot->last_error = session->last_error;
  snapshot->initialized = session->initialized;
  snapshot->connected = session->connected;
  snapshot->ready = session->ready;
  snapshot->terminal_pending =
    __atomic_load_n(&session->terminal_state, __ATOMIC_ACQUIRE) !=
    BKVOICE_TERMINAL_EMPTY;
}
