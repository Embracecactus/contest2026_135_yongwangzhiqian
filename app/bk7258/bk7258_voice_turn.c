/****************************************************************************
 * app/bk7258/bk7258_voice_turn.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure BKVoice half-duplex turn and audio-resource policy.
 ****************************************************************************/

#include "bk7258_voice_turn.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

static void bkvoice_turn_first_error(int *first, int ret)
{
  if (*first == 0 && ret < 0)
    {
      *first = ret;
    }
}

static uint64_t bkvoice_turn_deadline(uint64_t now_ms, uint64_t timeout_ms)
{
  return UINT64_MAX - now_ms < timeout_ms ? UINT64_MAX :
         now_ms + timeout_ms;
}

static bool bkvoice_turn_resources_free(const struct bkvoice_turn_s *turn)
{
  return !turn->mic_acquired && !turn->mic_prepared &&
         !turn->mic_started && !turn->dac_acquired &&
         !turn->dac_prepared && !turn->dac_started;
}

static void bkvoice_turn_clear_active(struct bkvoice_turn_s *turn)
{
  memset(&turn->active, 0, sizeof(turn->active));
  turn->deadline_ms = 0;
}

static void bkvoice_turn_clear_session(struct bkvoice_turn_s *turn)
{
  turn->session_id = 0;
  turn->last_control_sequence = 0;
  turn->last_downlink_sequence = 0;
}

static void bkvoice_turn_finish_cleanup(struct bkvoice_turn_s *turn,
                                        int error)
{
  turn->last_error = error;
  if (bkvoice_turn_resources_free(turn))
    {
      bkvoice_turn_clear_active(turn);
      turn->state = BKVOICE_TURN_IDLE;
    }
  else
    {
      turn->state = BKVOICE_TURN_FAULTED;
      turn->deadline_ms = 0;
    }
}

static int bkvoice_turn_cleanup_mic(struct bkvoice_turn_s *turn)
{
  int first = 0;
  int ret;

  if (turn->mic_started)
    {
      ret = turn->ops.mic_stop(turn->audio_context);
      bkvoice_turn_first_error(&first, ret);
      if (ret >= 0)
        {
          turn->mic_started = false;
        }
    }

  if (turn->mic_prepared)
    {
      ret = turn->ops.mic_drain(turn->audio_context);
      bkvoice_turn_first_error(&first, ret);
    }

  if (turn->mic_acquired)
    {
      ret = turn->ops.mic_release(turn->audio_context);
      bkvoice_turn_first_error(&first, ret);
      if (ret >= 0)
        {
          turn->mic_acquired = false;
          turn->mic_prepared = false;
          turn->mic_started = false;
        }
    }

  return first;
}

static int bkvoice_turn_cleanup_dac(struct bkvoice_turn_s *turn)
{
  int first = 0;
  int ret;

  if (turn->dac_prepared)
    {
      ret = turn->ops.dac_drain(turn->audio_context);
      bkvoice_turn_first_error(&first, ret);
    }

  if (turn->dac_started)
    {
      ret = turn->ops.dac_stop(turn->audio_context);
      bkvoice_turn_first_error(&first, ret);
      if (ret >= 0)
        {
          turn->dac_started = false;
        }
    }

  if (turn->dac_acquired)
    {
      ret = turn->ops.dac_release(turn->audio_context);
      bkvoice_turn_first_error(&first, ret);
      if (ret >= 0)
        {
          turn->dac_acquired = false;
          turn->dac_prepared = false;
          turn->dac_started = false;
        }
    }

  return first;
}

static int bkvoice_turn_close_session(struct bkvoice_turn_s *turn,
                                      int reason)
{
  int first = 0;

  if (turn->mic_acquired || turn->mic_prepared || turn->mic_started)
    {
      bkvoice_turn_first_error(&first, bkvoice_turn_cleanup_mic(turn));
    }

  if (turn->dac_acquired || turn->dac_prepared || turn->dac_started)
    {
      bkvoice_turn_first_error(&first, bkvoice_turn_cleanup_dac(turn));
    }

  bkvoice_turn_finish_cleanup(turn, first < 0 ? first : reason);
  bkvoice_turn_clear_session(turn);
  return first;
}

static int bkvoice_turn_token_match(
  const struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token)
{
  if (token == NULL || token->boot_generation == 0 ||
      token->session_id == 0 || token->turn_id == 0 ||
      token->sequence == 0)
    {
      return -EINVAL;
    }

  if (turn->active.turn_id == 0 ||
      turn->session_id == 0 ||
      token->boot_generation != turn->active.boot_generation ||
      token->session_id != turn->session_id ||
      token->session_id != turn->active.session_id ||
      token->turn_id != turn->active.turn_id)
    {
      return -ESTALE;
    }

  return 0;
}

static int bkvoice_turn_sequence(uint32_t last, uint32_t next)
{
  uint32_t expected;

  if (next == 0)
    {
      return -EINVAL;
    }

  if (last >= UINT32_MAX - 1u || next == UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  expected = last + 1u;
  if (next == last)
    {
      return -EALREADY;
    }

  if (next < expected)
    {
      return -ESTALE;
    }

  return next == expected ? 0 : -EPROTO;
}

static int bkvoice_turn_control_event(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token)
{
  int ret = bkvoice_turn_token_match(turn, token);

  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_turn_sequence(turn->last_control_sequence,
                              token->sequence);
  if (ret == -EOVERFLOW)
    {
      (void)bkvoice_turn_close_session(turn, ret);
    }

  return ret;
}

static int bkvoice_turn_downlink_event(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token)
{
  int ret = bkvoice_turn_token_match(turn, token);

  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_turn_sequence(turn->last_downlink_sequence,
                              token->sequence);
  if (ret == -EOVERFLOW)
    {
      (void)bkvoice_turn_close_session(turn, ret);
    }

  return ret;
}

static void bkvoice_turn_accept_control(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token)
{
  turn->last_control_sequence = token->sequence;
  turn->active.sequence = token->sequence;
}

static void bkvoice_turn_accept_downlink(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token)
{
  turn->last_downlink_sequence = token->sequence;
  turn->active.sequence = token->sequence;
}

int bkvoice_turn_initialize(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_audio_ops_s *ops,
  void *audio_context,
  const struct bkvoice_turn_limits_s *limits,
  uint32_t boot_generation)
{
  if (turn == NULL || ops == NULL || limits == NULL ||
      boot_generation == 0 || limits->capture_timeout_ms == 0 ||
      limits->waiting_tts_timeout_ms == 0 ||
      limits->playback_timeout_ms == 0 ||
      limits->audio_frame_bytes == 0 ||
      ops->mic_acquire == NULL || ops->mic_prepare == NULL ||
      ops->mic_start == NULL || ops->mic_stop == NULL ||
      ops->mic_drain == NULL || ops->mic_release == NULL ||
      ops->dac_acquire == NULL || ops->dac_prepare == NULL ||
      ops->dac_start == NULL || ops->dac_write == NULL ||
      ops->dac_drain == NULL || ops->dac_stop == NULL ||
      ops->dac_release == NULL)
    {
      return -EINVAL;
    }

  memset(turn, 0, sizeof(*turn));
  memcpy(&turn->ops, ops, sizeof(*ops));
  memcpy(&turn->limits, limits, sizeof(*limits));
  turn->audio_context = audio_context;
  turn->boot_generation = boot_generation;
  turn->state = BKVOICE_TURN_IDLE;
  return 0;
}

int bkvoice_turn_session_open(struct bkvoice_turn_s *turn,
                              uint32_t session_id)
{
  if (turn == NULL || session_id == 0)
    {
      return -EINVAL;
    }

  if (turn->state != BKVOICE_TURN_IDLE || turn->session_id != 0 ||
      !bkvoice_turn_resources_free(turn))
    {
      return turn->state == BKVOICE_TURN_FAULTED ? -EIO : -EBUSY;
    }

  if (turn->last_session_id == UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  if (session_id <= turn->last_session_id)
    {
      return -ESTALE;
    }

  turn->session_id = session_id;
  turn->last_session_id = session_id;
  turn->last_control_sequence = 0;
  turn->last_downlink_sequence = 0;
  turn->last_error = 0;
  return 0;
}

int bkvoice_turn_session_close(struct bkvoice_turn_s *turn, int reason)
{
  if (turn == NULL)
    {
      return -EINVAL;
    }

  if (turn->session_id == 0)
    {
      return -EALREADY;
    }

  if (reason >= 0)
    {
      reason = -ENOTCONN;
    }

  return bkvoice_turn_close_session(turn, reason);
}

int bkvoice_turn_ptt_down(
  struct bkvoice_turn_s *turn, uint32_t session_id,
  uint32_t control_sequence, uint64_t now_ms,
  struct bkvoice_turn_token_s *token)
{
  int cleanup;
  int ret;

  if (turn == NULL || token == NULL || session_id == 0 ||
      control_sequence == 0)
    {
      return -EINVAL;
    }

  if (turn->session_id == 0)
    {
      return -ENOTCONN;
    }

  if (session_id != turn->session_id)
    {
      return -ESTALE;
    }

  ret = bkvoice_turn_sequence(turn->last_control_sequence,
                              control_sequence);
  if (ret < 0)
    {
      if (ret == -EOVERFLOW)
        {
          (void)bkvoice_turn_close_session(turn, ret);
        }

      return ret;
    }

  turn->last_control_sequence = control_sequence;

  if (turn->state != BKVOICE_TURN_IDLE ||
      !bkvoice_turn_resources_free(turn))
    {
      return turn->state == BKVOICE_TURN_FAULTED ? -EIO : -EBUSY;
    }

  if (turn->last_turn_id == UINT32_MAX)
    {
      (void)bkvoice_turn_close_session(turn, -EOVERFLOW);
      return -EOVERFLOW;
    }

  turn->last_turn_id++;
  turn->active.boot_generation = turn->boot_generation;
  turn->active.session_id = session_id;
  turn->active.turn_id = turn->last_turn_id;
  turn->active.sequence = control_sequence;
  turn->last_error = 0;
  turn->deadline_ms = bkvoice_turn_deadline(
    now_ms, turn->limits.capture_timeout_ms);

  ret = turn->ops.mic_acquire(turn->audio_context);
  if (ret < 0)
    {
      goto fail;
    }

  turn->mic_acquired = true;
  ret = turn->ops.mic_prepare(turn->audio_context);
  if (ret < 0)
    {
      goto fail;
    }

  turn->mic_prepared = true;
  ret = turn->ops.mic_start(turn->audio_context);
  if (ret < 0)
    {
      goto fail;
    }

  turn->mic_started = true;
  turn->state = BKVOICE_TURN_CAPTURING;
  memcpy(token, &turn->active, sizeof(*token));
  return 0;

fail:
  cleanup = bkvoice_turn_cleanup_mic(turn);
  bkvoice_turn_finish_cleanup(turn, cleanup < 0 ? cleanup : ret);
  return ret;
}

int bkvoice_turn_ptt_up(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token,
  uint64_t now_ms)
{
  int ret;

  if (turn == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_turn_control_event(turn, token);
  if (ret < 0)
    {
      return ret;
    }

  bkvoice_turn_accept_control(turn, token);
  if (turn->state != BKVOICE_TURN_CAPTURING)
    {
      return -EPERM;
    }

  ret = bkvoice_turn_cleanup_mic(turn);
  if (ret < 0)
    {
      bkvoice_turn_finish_cleanup(turn, ret);
      return ret;
    }

  turn->state = BKVOICE_TURN_WAITING_TTS;
  turn->deadline_ms = bkvoice_turn_deadline(
    now_ms, turn->limits.waiting_tts_timeout_ms);
  return 0;
}

int bkvoice_turn_tts_start(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token,
  uint64_t now_ms)
{
  int cleanup;
  int ret;

  if (turn == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_turn_downlink_event(turn, token);
  if (ret < 0)
    {
      return ret;
    }

  bkvoice_turn_accept_downlink(turn, token);
  if (turn->state != BKVOICE_TURN_WAITING_TTS ||
      turn->mic_acquired || turn->mic_prepared || turn->mic_started)
    {
      return -EPERM;
    }

  ret = turn->ops.dac_acquire(turn->audio_context);
  if (ret < 0)
    {
      goto fail;
    }

  turn->dac_acquired = true;
  ret = turn->ops.dac_prepare(turn->audio_context);
  if (ret < 0)
    {
      goto fail;
    }

  turn->dac_prepared = true;
  ret = turn->ops.dac_start(turn->audio_context);
  if (ret < 0)
    {
      goto fail;
    }

  turn->dac_started = true;
  turn->state = BKVOICE_TURN_PLAYING;
  turn->deadline_ms = bkvoice_turn_deadline(
    now_ms, turn->limits.playback_timeout_ms);
  return 0;

fail:
  cleanup = bkvoice_turn_cleanup_dac(turn);
  bkvoice_turn_finish_cleanup(turn, cleanup < 0 ? cleanup : ret);
  return ret;
}

int bkvoice_turn_tts_audio(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token,
  const uint8_t *pcm, size_t bytes, uint64_t now_ms)
{
  ssize_t written;
  int cleanup;
  int ret;

  if (turn == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_turn_downlink_event(turn, token);
  if (ret < 0)
    {
      return ret;
    }

  bkvoice_turn_accept_downlink(turn, token);
  if (turn->state != BKVOICE_TURN_PLAYING || !turn->dac_started ||
      turn->mic_acquired || turn->mic_prepared || turn->mic_started)
    {
      return -EPERM;
    }

  if (pcm == NULL || bytes != turn->limits.audio_frame_bytes)
    {
      ret = pcm == NULL ? -EINVAL : -EMSGSIZE;
      cleanup = bkvoice_turn_cleanup_dac(turn);
      bkvoice_turn_finish_cleanup(turn, cleanup < 0 ? cleanup : ret);
      return ret;
    }

  written = turn->ops.dac_write(turn->audio_context, pcm, bytes);
  if (written != (ssize_t)bytes)
    {
      ret = written < 0 ? (int)written : -EIO;
      cleanup = bkvoice_turn_cleanup_dac(turn);
      bkvoice_turn_finish_cleanup(turn, cleanup < 0 ? cleanup : ret);
      return ret;
    }

  turn->deadline_ms = bkvoice_turn_deadline(
    now_ms, turn->limits.playback_timeout_ms);
  return 0;
}

int bkvoice_turn_tts_end(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token)
{
  int ret;

  if (turn == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_turn_downlink_event(turn, token);
  if (ret < 0)
    {
      return ret;
    }

  bkvoice_turn_accept_downlink(turn, token);
  if (turn->state != BKVOICE_TURN_PLAYING)
    {
      return -EPERM;
    }

  ret = bkvoice_turn_cleanup_dac(turn);
  bkvoice_turn_finish_cleanup(turn, ret);
  return ret;
}

int bkvoice_turn_cancel(
  struct bkvoice_turn_s *turn,
  const struct bkvoice_turn_token_s *token, int reason)
{
  int first = 0;
  int ret;

  if (turn == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_turn_control_event(turn, token);
  if (ret < 0)
    {
      return ret;
    }

  bkvoice_turn_accept_control(turn, token);
  if (turn->mic_acquired || turn->mic_prepared || turn->mic_started)
    {
      bkvoice_turn_first_error(&first, bkvoice_turn_cleanup_mic(turn));
    }

  if (turn->dac_acquired || turn->dac_prepared || turn->dac_started)
    {
      bkvoice_turn_first_error(&first, bkvoice_turn_cleanup_dac(turn));
    }

  if (reason >= 0)
    {
      reason = -ECANCELED;
    }

  bkvoice_turn_finish_cleanup(turn, first < 0 ? first : reason);
  return first;
}

int bkvoice_turn_timeout(struct bkvoice_turn_s *turn, uint64_t now_ms)
{
  int first = 0;

  if (turn == NULL)
    {
      return -EINVAL;
    }

  if (turn->state == BKVOICE_TURN_IDLE)
    {
      return -EALREADY;
    }

  if (turn->state == BKVOICE_TURN_FAULTED)
    {
      return -EIO;
    }

  if (now_ms < turn->deadline_ms)
    {
      return -EAGAIN;
    }

  if (turn->mic_acquired || turn->mic_prepared || turn->mic_started)
    {
      bkvoice_turn_first_error(&first, bkvoice_turn_cleanup_mic(turn));
    }

  if (turn->dac_acquired || turn->dac_prepared || turn->dac_started)
    {
      bkvoice_turn_first_error(&first, bkvoice_turn_cleanup_dac(turn));
    }

  bkvoice_turn_finish_cleanup(turn, first < 0 ? first : -ETIMEDOUT);
  return first;
}

int bkvoice_turn_recover(struct bkvoice_turn_s *turn)
{
  int first = 0;

  if (turn == NULL)
    {
      return -EINVAL;
    }

  if (turn->state != BKVOICE_TURN_FAULTED)
    {
      return -EALREADY;
    }

  if (turn->mic_acquired || turn->mic_prepared || turn->mic_started)
    {
      bkvoice_turn_first_error(&first, bkvoice_turn_cleanup_mic(turn));
    }

  if (turn->dac_acquired || turn->dac_prepared || turn->dac_started)
    {
      bkvoice_turn_first_error(&first, bkvoice_turn_cleanup_dac(turn));
    }

  bkvoice_turn_finish_cleanup(turn, first);
  return first;
}

void bkvoice_turn_snapshot(
  const struct bkvoice_turn_s *turn,
  struct bkvoice_turn_snapshot_s *snapshot)
{
  if (turn == NULL || snapshot == NULL)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  memcpy(&snapshot->active, &turn->active, sizeof(snapshot->active));
  snapshot->state = turn->state;
  snapshot->session_id = turn->session_id;
  snapshot->last_session_id = turn->last_session_id;
  snapshot->last_control_sequence = turn->last_control_sequence;
  snapshot->last_downlink_sequence = turn->last_downlink_sequence;
  snapshot->deadline_ms = turn->deadline_ms;
  snapshot->last_error = turn->last_error;
  snapshot->mic_acquired = turn->mic_acquired;
  snapshot->mic_prepared = turn->mic_prepared;
  snapshot->mic_started = turn->mic_started;
  snapshot->dac_acquired = turn->dac_acquired;
  snapshot->dac_prepared = turn->dac_prepared;
  snapshot->dac_started = turn->dac_started;
}

const char *bkvoice_turn_state_name(enum bkvoice_turn_state_e state)
{
  switch (state)
    {
      case BKVOICE_TURN_IDLE:
        return "idle";
      case BKVOICE_TURN_CAPTURING:
        return "capturing";
      case BKVOICE_TURN_WAITING_TTS:
        return "waiting-tts";
      case BKVOICE_TURN_PLAYING:
        return "playing";
      case BKVOICE_TURN_FAULTED:
        return "faulted";
      default:
        return "invalid";
    }
}
