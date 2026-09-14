/****************************************************************************
 * app/bk7258/bk7258_voice_ptt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BKVoice PTT capture-worker lifecycle owner.  This App module owns no GPIO,
 * device path, socket, certificate or board resource.
 ****************************************************************************/

#include "bk7258_voice_ptt.h"

#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <string.h>
#include <time.h>

static bool bkvoice_ptt_source_ops_valid(
  const struct bkvoice_capture_source_ops_s *ops)
{
  return ops != NULL && ops->attach != NULL && ops->read != NULL &&
         ops->interrupt != NULL && ops->detach != NULL;
}

static void bkvoice_ptt_first_error(int *first, int ret)
{
  if (*first >= 0 && ret < 0)
    {
      *first = ret;
    }
}

static int bkvoice_ptt_next_control_token(
  const struct bkvoice_ptt_s *ptt,
  struct bkvoice_turn_token_s *token)
{
  if (ptt->turn.active.boot_generation == 0 ||
      ptt->turn.active.session_id == 0 || ptt->turn.active.turn_id == 0)
    {
      return -ESTALE;
    }

  if (ptt->turn.last_control_sequence >= UINT32_MAX - 1u)
    {
      return -EOVERFLOW;
    }

  memcpy(token, &ptt->turn.active, sizeof(*token));
  token->sequence = ptt->turn.last_control_sequence + 1u;
  return 0;
}

static void *bkvoice_ptt_worker(void *arg)
{
  struct bkvoice_ptt_s *ptt = arg;
  int ret = bkvoice_capture_run(&ptt->capture);

  __atomic_store_n(&ptt->worker_result, ret, __ATOMIC_RELEASE);
  (void)sem_post(&ptt->worker_done);
  return NULL;
}

static int bkvoice_ptt_worker_start(struct bkvoice_ptt_s *ptt)
{
  struct sched_param param;
  pthread_attr_t attr;
  int ret;

  while (sem_trywait(&ptt->worker_done) == 0)
    {
    }

  if (errno != EAGAIN)
    {
      return errno > 0 ? -errno : -EIO;
    }

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      return -ret;
    }

  if (ptt->worker_config.stack_size != 0)
    {
      ret = pthread_attr_setstacksize(&attr,
                                      ptt->worker_config.stack_size);
    }

  if (ret == 0 && ptt->worker_config.priority > 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = ptt->worker_config.priority;
      ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
      if (ret == 0)
        {
          ret = pthread_attr_setschedparam(&attr, &param);
        }

      if (ret == 0)
        {
          ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        }
    }

  if (ret == 0)
    {
      __atomic_store_n(&ptt->worker_result, -EINPROGRESS,
                       __ATOMIC_RELEASE);
      ret = pthread_create(&ptt->worker, &attr, bkvoice_ptt_worker, ptt);
    }

  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      return -ret;
    }

  ptt->worker_joinable = true;
  return 0;
}

static int bkvoice_ptt_deadline(uint32_t timeout_ms,
                                struct timespec *deadline)
{
  uint64_t nanoseconds;

  if (clock_gettime(CLOCK_REALTIME, deadline) < 0)
    {
      return errno > 0 ? -errno : -EIO;
    }

  nanoseconds = (uint64_t)deadline->tv_nsec +
                (uint64_t)(timeout_ms % 1000u) * 1000000u;
  deadline->tv_sec += (time_t)(timeout_ms / 1000u) +
                      (time_t)(nanoseconds / 1000000000u);
  deadline->tv_nsec = (long)(nanoseconds % 1000000000u);
  return 0;
}

static int bkvoice_ptt_worker_join(struct bkvoice_ptt_s *ptt)
{
  struct timespec deadline;
  int ret;

  if (!ptt->worker_joinable)
    {
      return -EALREADY;
    }

  ret = bkvoice_ptt_deadline(ptt->worker_config.join_timeout_ms,
                             &deadline);
  if (ret < 0)
    {
      return ret;
    }

  do
    {
      ret = sem_timedwait(&ptt->worker_done, &deadline);
    }
  while (ret < 0 && errno == EINTR);

  if (ret < 0)
    {
      return errno > 0 ? -errno : -EIO;
    }

  ret = pthread_join(ptt->worker, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  ptt->worker_joinable = false;
  return 0;
}

static enum bkvoice_capture_state_e
bkvoice_ptt_capture_state(const struct bkvoice_ptt_s *ptt)
{
  return __atomic_load_n(&ptt->capture.state, __ATOMIC_ACQUIRE);
}

static void bkvoice_ptt_clear_session(struct bkvoice_ptt_s *ptt)
{
  memset(&ptt->capture, 0, sizeof(ptt->capture));
  memset(&ptt->token, 0, sizeof(ptt->token));
  ptt->capture_ready = false;
}

/* Stop and join the recorder before any owner releases the MIC.  A join
 * timeout deliberately leaves worker_joinable set so the caller can retry
 * without destroying state still referenced by the recorder.
 */

static int bkvoice_ptt_stop_worker(struct bkvoice_ptt_s *ptt)
{
  int first = 0;
  int ret;

  if (!ptt->worker_joinable)
    {
      return 0;
    }

  ret = bkvoice_capture_request_stop(&ptt->capture);
  if (ret != -EPERM)
    {
      bkvoice_ptt_first_error(&first, ret);
    }

  ret = bkvoice_ptt_worker_join(ptt);
  if (ret < 0)
    {
      return ret;
    }

  ret = __atomic_load_n(&ptt->worker_result, __ATOMIC_ACQUIRE);
  bkvoice_ptt_first_error(&first, ret);
  return first;
}

/* Cancel an active turn after its capture worker is no longer joinable.
 * Control-sequence exhaustion is terminal for the session, so fall back to
 * the token-free session close path instead of trying to mint another token.
 */

static int bkvoice_ptt_cancel_stopped(struct bkvoice_ptt_s *ptt,
                                      int reason)
{
  struct bkvoice_turn_token_s event;
  int terminal_reason;
  int ret;

  if (reason >= 0)
    {
      reason = -ECANCELED;
    }

  if (ptt->capture_ready &&
      bkvoice_ptt_capture_state(ptt) != BKVOICE_CAPTURE_IDLE)
    {
      ret = bkvoice_capture_cancel(&ptt->capture, reason);
      if (ret < 0)
        {
          ptt->last_error = ret;
          return ret;
        }
    }

  if (ptt->turn.state != BKVOICE_TURN_IDLE)
    {
      ret = bkvoice_ptt_next_control_token(ptt, &event);
      if (ret < 0)
        {
          terminal_reason = ret;
          if (ptt->turn.session_id != 0)
            {
              ret = bkvoice_turn_session_close(&ptt->turn,
                                               terminal_reason);
              if (ret < 0)
                {
                  if (ptt->turn.session_id == 0 &&
                      ptt->turn.state == BKVOICE_TURN_IDLE)
                    {
                      bkvoice_ptt_clear_session(ptt);
                    }

                  ptt->last_error = ret;
                  return ret;
                }
            }

          if (ptt->turn.session_id == 0 &&
              ptt->turn.state == BKVOICE_TURN_IDLE)
            {
              bkvoice_ptt_clear_session(ptt);
            }

          ptt->last_error = terminal_reason;
          return terminal_reason;
        }

      ret = bkvoice_turn_cancel(&ptt->turn, &event, reason);
      if (ret < 0)
        {
          if (ptt->turn.session_id == 0 &&
              ptt->turn.state == BKVOICE_TURN_IDLE)
            {
              bkvoice_ptt_clear_session(ptt);
            }

          ptt->last_error = ret;
          return ret;
        }

      memcpy(&ptt->token, &event, sizeof(ptt->token));
    }

  ptt->last_error = reason;
  return 0;
}

static int bkvoice_ptt_abort(struct bkvoice_ptt_s *ptt, int reason)
{
  int ret = bkvoice_ptt_cancel_stopped(ptt, reason);

  return ret < 0 ? ret : (reason < 0 ? reason : -ECANCELED);
}

static void bkvoice_ptt_stop_without_worker(struct bkvoice_ptt_s *ptt)
{
  if (bkvoice_capture_request_stop(&ptt->capture) >= 0)
    {
      (void)bkvoice_capture_run(&ptt->capture);
    }
}

int bkvoice_ptt_initialize(
  struct bkvoice_ptt_s *ptt,
  const struct bkvoice_turn_audio_ops_s *audio_ops,
  void *audio_context,
  const struct bkvoice_capture_source_ops_s *source_ops,
  void *source_context,
  const struct bkvoice_turn_limits_s *turn_limits,
  const struct bkvoice_ptt_worker_config_s *worker_config,
  uint32_t boot_generation)
{
  int ret;

  if (ptt == NULL || source_context == NULL ||
      !bkvoice_ptt_source_ops_valid(source_ops) || worker_config == NULL ||
      worker_config->priority < 0 || worker_config->join_timeout_ms == 0)
    {
      return -EINVAL;
    }

  memset(ptt, 0, sizeof(*ptt));
  ret = bkvoice_turn_initialize(&ptt->turn, audio_ops, audio_context,
                                turn_limits, boot_generation);
  if (ret < 0)
    {
      return ret;
    }

  if (sem_init(&ptt->worker_done, 0, 0) < 0)
    {
      return errno > 0 ? -errno : -EIO;
    }

  memcpy(&ptt->source_ops, source_ops, sizeof(*source_ops));
  memcpy(&ptt->worker_config, worker_config, sizeof(*worker_config));
  ptt->source_context = source_context;
  ptt->initialized = true;
  return 0;
}

int bkvoice_ptt_uninitialize(struct bkvoice_ptt_s *ptt)
{
  if (ptt == NULL)
    {
      return -EINVAL;
    }

  if (!ptt->initialized)
    {
      return -EALREADY;
    }

  if (ptt->worker_joinable || ptt->capture_ready ||
      ptt->turn.session_id != 0 || ptt->turn.state != BKVOICE_TURN_IDLE)
    {
      return -EBUSY;
    }

  if (sem_destroy(&ptt->worker_done) < 0)
    {
      return errno > 0 ? -errno : -EIO;
    }

  memset(ptt, 0, sizeof(*ptt));
  return 0;
}

int bkvoice_ptt_session_open(
  struct bkvoice_ptt_s *ptt, uint32_t session_id,
  const struct bkvoice_capture_sink_ops_s *sink_ops,
  void *sink_context)
{
  int ret;

  if (ptt == NULL || !ptt->initialized)
    {
      return -EINVAL;
    }

  if (ptt->capture_ready || ptt->worker_joinable)
    {
      return -EBUSY;
    }

  ret = bkvoice_capture_initialize(&ptt->capture, &ptt->source_ops,
                                   ptt->source_context, sink_ops,
                                   sink_context);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_turn_session_open(&ptt->turn, session_id);
  if (ret < 0)
    {
      memset(&ptt->capture, 0, sizeof(ptt->capture));
      return ret;
    }

  ptt->capture_ready = true;
  ptt->last_error = 0;
  return 0;
}

int bkvoice_ptt_session_close(struct bkvoice_ptt_s *ptt, int reason)
{
  int first = 0;
  int ret;

  if (ptt == NULL || !ptt->initialized)
    {
      return -EINVAL;
    }

  if (reason >= 0)
    {
      reason = -ENOTCONN;
    }

  if (!ptt->capture_ready)
    {
      return -EALREADY;
    }

  ret = bkvoice_ptt_stop_worker(ptt);
  if (ptt->worker_joinable)
    {
      ptt->last_error = ret;
      return ret;
    }

  bkvoice_ptt_first_error(&first, ret);
  if (bkvoice_ptt_capture_state(ptt) != BKVOICE_CAPTURE_IDLE)
    {
      ret = bkvoice_capture_cancel(&ptt->capture, reason);
      if (ret < 0)
        {
          ptt->last_error = ret;
          return ret;
        }
    }

  if (ptt->turn.session_id != 0)
    {
      ret = bkvoice_turn_session_close(&ptt->turn, reason);
    }
  else if (ptt->turn.state == BKVOICE_TURN_FAULTED)
    {
      ret = bkvoice_turn_recover(&ptt->turn);
    }
  else
    {
      ret = ptt->turn.state == BKVOICE_TURN_IDLE ? 0 : -EIO;
    }

  if (ret < 0)
    {
      if (ptt->turn.session_id == 0 &&
          ptt->turn.state == BKVOICE_TURN_IDLE)
        {
          bkvoice_ptt_clear_session(ptt);
        }

      ptt->last_error = ret;
      return ret;
    }

  bkvoice_ptt_clear_session(ptt);
  ptt->last_error = first < 0 ? first : reason;
  return first;
}

int bkvoice_ptt_cancel(struct bkvoice_ptt_s *ptt, int reason)
{
  int first = 0;
  int ret;

  if (ptt == NULL || !ptt->initialized || !ptt->capture_ready)
    {
      return -EINVAL;
    }

  if (!ptt->worker_joinable &&
      bkvoice_ptt_capture_state(ptt) == BKVOICE_CAPTURE_IDLE &&
      ptt->turn.state == BKVOICE_TURN_IDLE)
    {
      return -EALREADY;
    }

  if (reason >= 0)
    {
      reason = -ECANCELED;
    }

  ret = bkvoice_ptt_stop_worker(ptt);
  if (ptt->worker_joinable)
    {
      ptt->last_error = ret;
      return ret;
    }

  bkvoice_ptt_first_error(&first, ret);
  ret = bkvoice_ptt_cancel_stopped(ptt, reason);
  bkvoice_ptt_first_error(&first, ret);
  ptt->last_error = first < 0 ? first : reason;
  return first;
}

int bkvoice_ptt_timeout(struct bkvoice_ptt_s *ptt, uint64_t now_ms)
{
  int first = 0;
  int ret;

  if (ptt == NULL || !ptt->initialized || !ptt->capture_ready)
    {
      return -EINVAL;
    }

  if (ptt->turn.state == BKVOICE_TURN_IDLE)
    {
      return -EALREADY;
    }

  if (ptt->turn.state == BKVOICE_TURN_FAULTED)
    {
      return -EIO;
    }

  if (now_ms < ptt->turn.deadline_ms)
    {
      return -EAGAIN;
    }

  ret = bkvoice_ptt_stop_worker(ptt);
  if (ptt->worker_joinable)
    {
      ptt->last_error = ret;
      return ret;
    }

  bkvoice_ptt_first_error(&first, ret);
  if (bkvoice_ptt_capture_state(ptt) != BKVOICE_CAPTURE_IDLE)
    {
      ret = bkvoice_capture_cancel(&ptt->capture, -ETIMEDOUT);
      if (ret < 0)
        {
          ptt->last_error = ret;
          return ret;
        }
    }

  ret = bkvoice_turn_timeout(&ptt->turn, now_ms);
  bkvoice_ptt_first_error(&first, ret);
  ptt->last_error = first < 0 ? first : -ETIMEDOUT;
  return first;
}

static int bkvoice_ptt_down_start(
  struct bkvoice_ptt_s *ptt, uint64_t now_ms,
  bkvoice_capture_prefill_read_t read_frame, void *prefill_context,
  size_t prefill_frames, bkvoice_capture_frame_filter_t frame_filter,
  void *live_context, struct bkvoice_turn_token_s *token)
{
  uint32_t sequence;
  int ret;

  if (ptt == NULL || token == NULL || !ptt->initialized ||
      !ptt->capture_ready ||
      ((read_frame == NULL) != (prefill_frames == 0)) ||
      (frame_filter == NULL && live_context != NULL) ||
      prefill_frames > BKVOICE_CAPTURE_MAX_PREFILL_FRAMES)
    {
      return -EINVAL;
    }

  if (ptt->worker_joinable)
    {
      return -EBUSY;
    }

  if (ptt->turn.last_control_sequence >= UINT32_MAX - 1u)
    {
      ret = bkvoice_ptt_session_close(ptt, -EOVERFLOW);
      ptt->last_error = ret < 0 ? ret : -EOVERFLOW;
      if (ret < 0)
        {
          return ret;
        }

      return -EOVERFLOW;
    }

  sequence = ptt->turn.last_control_sequence + 1u;
  ret = bkvoice_turn_ptt_down(&ptt->turn, ptt->turn.session_id,
                              sequence, now_ms, &ptt->token);
  if (ret < 0)
    {
      ptt->last_error = ret;
      return ret;
    }

  ret = bkvoice_capture_start(&ptt->capture, &ptt->token);
  if (ret < 0)
    {
      (void)bkvoice_ptt_abort(ptt, ret);
      return ret;
    }

  if (prefill_frames != 0)
    {
      ret = bkvoice_capture_prefill(&ptt->capture, read_frame,
                                    prefill_context, prefill_frames);
      if (ret < 0)
        {
          return bkvoice_ptt_abort(ptt, ret);
        }
    }

  if (frame_filter != NULL)
    {
      ret = bkvoice_capture_set_frame_filter(
        &ptt->capture, frame_filter, live_context);
      if (ret < 0)
        {
          return bkvoice_ptt_abort(ptt, ret);
        }
    }

  ret = bkvoice_ptt_worker_start(ptt);
  if (ret < 0)
    {
      bkvoice_ptt_stop_without_worker(ptt);
      (void)bkvoice_ptt_abort(ptt, ret);
      return ret;
    }

  memcpy(token, &ptt->token, sizeof(*token));
  ptt->last_error = 0;
  return 0;
}

int bkvoice_ptt_down(struct bkvoice_ptt_s *ptt, uint64_t now_ms,
                     struct bkvoice_turn_token_s *token)
{
  return bkvoice_ptt_down_start(ptt, now_ms, NULL, NULL, 0, NULL, NULL,
                                token);
}

int bkvoice_ptt_down_stream(
  struct bkvoice_ptt_s *ptt, uint64_t now_ms,
  bkvoice_capture_prefill_read_t read_frame, void *prefill_context,
  size_t prefill_frames, bkvoice_capture_frame_filter_t frame_filter,
  void *live_context, struct bkvoice_turn_token_s *token)
{
  if (read_frame == NULL && frame_filter == NULL)
    {
      return -EINVAL;
    }

  return bkvoice_ptt_down_start(
    ptt, now_ms, read_frame, prefill_context, prefill_frames,
    frame_filter, live_context, token);
}

int bkvoice_ptt_up(struct bkvoice_ptt_s *ptt, uint64_t now_ms)
{
  struct bkvoice_turn_token_s event;
  int worker_result;
  int ret;

  if (ptt == NULL || !ptt->initialized || !ptt->capture_ready)
    {
      return -EINVAL;
    }

  if (!ptt->worker_joinable)
    {
      return -EPERM;
    }

  ret = bkvoice_capture_request_stop(&ptt->capture);
  if (ret < 0 && ret != -EPERM)
    {
      ptt->last_error = ret;
    }

  ret = bkvoice_ptt_worker_join(ptt);
  if (ret < 0)
    {
      ptt->last_error = ret;
      return ret;
    }

  worker_result = __atomic_load_n(&ptt->worker_result, __ATOMIC_ACQUIRE);
  if (worker_result < 0)
    {
      return bkvoice_ptt_abort(ptt, worker_result);
    }

  ret = bkvoice_ptt_next_control_token(ptt, &event);
  if (ret < 0)
    {
      return bkvoice_ptt_abort(ptt, ret);
    }

  ret = bkvoice_turn_ptt_up(&ptt->turn, &event, now_ms);
  if (ret < 0)
    {
      (void)bkvoice_ptt_abort(ptt, ret);
      return ret;
    }

  memcpy(&ptt->token, &event, sizeof(ptt->token));
  ret = bkvoice_capture_complete(&ptt->capture);
  if (ret < 0)
    {
      (void)bkvoice_ptt_abort(ptt, ret);
      return ret;
    }

  ptt->last_error = 0;
  return 0;
}

bool bkvoice_ptt_quiescent(const struct bkvoice_ptt_s *ptt)
{
  if (ptt == NULL || !ptt->initialized || ptt->worker_joinable ||
      ptt->turn.state != BKVOICE_TURN_IDLE ||
      ptt->turn.mic_acquired || ptt->turn.mic_prepared ||
      ptt->turn.mic_started || ptt->turn.dac_acquired ||
      ptt->turn.dac_prepared || ptt->turn.dac_started)
    {
      return false;
    }

  if (!ptt->capture_ready)
    {
      return true;
    }

  return bkvoice_ptt_capture_state(ptt) == BKVOICE_CAPTURE_IDLE &&
         !ptt->capture.source_attached && !ptt->capture.sink_started;
}

void bkvoice_ptt_snapshot(const struct bkvoice_ptt_s *ptt,
                          struct bkvoice_ptt_snapshot_s *snapshot)
{
  if (ptt == NULL || snapshot == NULL)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  bkvoice_turn_snapshot(&ptt->turn, &snapshot->turn);
  if (ptt->capture_ready)
    {
      bkvoice_capture_snapshot(&ptt->capture, &snapshot->capture);
    }

  memcpy(&snapshot->token, &ptt->token, sizeof(snapshot->token));
  snapshot->worker_result = __atomic_load_n(&ptt->worker_result,
                                             __ATOMIC_ACQUIRE);
  snapshot->last_error = ptt->last_error;
  snapshot->session_open = ptt->turn.session_id != 0;
  snapshot->capture_ready = ptt->capture_ready;
  snapshot->worker_joinable = ptt->worker_joinable;
}
