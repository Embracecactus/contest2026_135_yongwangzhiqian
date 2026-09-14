/****************************************************************************
 * app/bk7258/bk7258_voice_capture.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BKVoice capture/uplink frame pump.  This module owns no task, socket,
 * certificate, device path, GPIO or board resource.
 ****************************************************************************/

#include "bk7258_voice_capture.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

static bool bkvoice_capture_token_valid(
  const struct bkvoice_turn_token_s *token)
{
  return token != NULL && token->boot_generation != 0 &&
         token->session_id != 0 && token->turn_id != 0 &&
         token->sequence != 0;
}

static bool bkvoice_capture_ops_valid(
  const struct bkvoice_capture_source_ops_s *source_ops,
  const struct bkvoice_capture_sink_ops_s *sink_ops)
{
  return source_ops != NULL && source_ops->attach != NULL &&
         source_ops->read != NULL && source_ops->interrupt != NULL &&
         source_ops->detach != NULL && sink_ops != NULL &&
         sink_ops->start != NULL && sink_ops->audio != NULL &&
         sink_ops->end != NULL && sink_ops->cancel != NULL;
}

static void bkvoice_capture_clear_active(struct bkvoice_capture_s *capture)
{
  memset(&capture->token, 0, sizeof(capture->token));
  memset(capture->frame, 0, sizeof(capture->frame));
  capture->frame_filter = NULL;
  capture->frame_filter_context = NULL;
  capture->frame_fill = 0;
  __atomic_store_n(&capture->source_attached, false, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->sink_started, false, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->stop_requested, false, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->run_started, false, __ATOMIC_RELEASE);
}

static void bkvoice_capture_first_error(int *first, int ret)
{
  if (*first >= 0 && ret < 0)
    {
      *first = ret;
    }
}

int bkvoice_capture_initialize(
  struct bkvoice_capture_s *capture,
  const struct bkvoice_capture_source_ops_s *source_ops,
  void *source_context,
  const struct bkvoice_capture_sink_ops_s *sink_ops,
  void *sink_context)
{
  if (capture == NULL || source_context == NULL || sink_context == NULL ||
      !bkvoice_capture_ops_valid(source_ops, sink_ops))
    {
      return -EINVAL;
    }

  memset(capture, 0, sizeof(*capture));
  memcpy(&capture->source_ops, source_ops, sizeof(*source_ops));
  memcpy(&capture->sink_ops, sink_ops, sizeof(*sink_ops));
  capture->source_context = source_context;
  capture->sink_context = sink_context;
  capture->state = BKVOICE_CAPTURE_IDLE;
  return 0;
}

int bkvoice_capture_start(
  struct bkvoice_capture_s *capture,
  const struct bkvoice_turn_token_s *token)
{
  int cancel_ret;
  int detach_ret;
  int first;
  int ret;

  if (capture == NULL || !bkvoice_capture_token_valid(token))
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&capture->state, __ATOMIC_ACQUIRE) !=
      BKVOICE_CAPTURE_IDLE ||
      __atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&capture->sink_started, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  __atomic_store_n(&capture->frames_sent, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->prefill_frames_sent, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->bytes_sent, 0, __ATOMIC_RELEASE);
  capture->frame_fill = 0;
  __atomic_store_n(&capture->partial_bytes_discarded, 0,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&capture->last_error, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->stop_requested, false, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->run_started, false, __ATOMIC_RELEASE);
  capture->frame_filter = NULL;
  capture->frame_filter_context = NULL;
  memcpy(&capture->token, token, sizeof(*token));

  ret = capture->source_ops.attach(capture->source_context);
  if (ret < 0)
    {
      capture->last_error = ret;
      memset(&capture->token, 0, sizeof(capture->token));
      return ret;
    }

  __atomic_store_n(&capture->source_attached, true, __ATOMIC_RELEASE);
  ret = capture->sink_ops.start(capture->sink_context, &capture->token);
  if (ret < 0)
    {
      /* A failed transport callback may still have emitted TURN_START.
       * Always issue the matching cancellation before releasing the source;
       * the sink contract makes cancel idempotent for a no-op start failure.
       */

      first = ret;
      cancel_ret = capture->sink_ops.cancel(capture->sink_context,
                                            &capture->token, ret);
      bkvoice_capture_first_error(&first, cancel_ret);
      __atomic_store_n(&capture->sink_started, cancel_ret < 0,
                       __ATOMIC_RELEASE);
      detach_ret = capture->source_ops.detach(capture->source_context);
      bkvoice_capture_first_error(&first, detach_ret);
      if (detach_ret >= 0)
        {
          __atomic_store_n(&capture->source_attached, false,
                           __ATOMIC_RELEASE);
        }

      if (cancel_ret >= 0 && detach_ret >= 0)
        {
          memset(&capture->token, 0, sizeof(capture->token));
          capture->state = BKVOICE_CAPTURE_IDLE;
        }
      else
        {
          capture->state = BKVOICE_CAPTURE_FAULTED;
        }

      capture->last_error = first;
      return first;
    }

  __atomic_store_n(&capture->sink_started, true, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->state, BKVOICE_CAPTURE_RUNNING,
                   __ATOMIC_RELEASE);
  return 0;
}

static int bkvoice_capture_prefill_abort(
  struct bkvoice_capture_s *capture, int reason)
{
  int first = reason;
  int ret;

  ret = capture->source_ops.interrupt(capture->source_context);
  bkvoice_capture_first_error(&first, ret);

  ret = capture->source_ops.detach(capture->source_context);
  if (ret >= 0)
    {
      __atomic_store_n(&capture->source_attached, false,
                       __ATOMIC_RELEASE);
    }

  bkvoice_capture_first_error(&first, ret);
  ret = capture->sink_ops.cancel(capture->sink_context, &capture->token,
                                 reason);
  if (ret >= 0)
    {
      __atomic_store_n(&capture->sink_started, false, __ATOMIC_RELEASE);
    }

  bkvoice_capture_first_error(&first, ret);
  if (!__atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE) &&
      !__atomic_load_n(&capture->sink_started, __ATOMIC_ACQUIRE))
    {
      bkvoice_capture_clear_active(capture);
      __atomic_store_n(&capture->state, BKVOICE_CAPTURE_IDLE,
                       __ATOMIC_RELEASE);
    }
  else
    {
      __atomic_store_n(&capture->state, BKVOICE_CAPTURE_FAULTED,
                       __ATOMIC_RELEASE);
    }

  __atomic_store_n(&capture->last_error, first, __ATOMIC_RELEASE);
  return first;
}

int bkvoice_capture_prefill(
  struct bkvoice_capture_s *capture,
  bkvoice_capture_prefill_read_t read_frame, void *context,
  size_t frames)
{
  size_t index;
  int ret;

  if (capture == NULL || read_frame == NULL || frames == 0 ||
      frames > BKVOICE_CAPTURE_MAX_PREFILL_FRAMES)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&capture->state, __ATOMIC_ACQUIRE) !=
      BKVOICE_CAPTURE_RUNNING ||
      !__atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE) ||
      !__atomic_load_n(&capture->sink_started, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&capture->stop_requested, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&capture->run_started, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&capture->frames_sent, __ATOMIC_ACQUIRE) != 0 ||
      capture->frame_fill != 0)
    {
      return -EBUSY;
    }

  for (index = 0; index < frames; index++)
    {
      ret = read_frame(context, index, capture->frame);
      if (ret != 0)
        {
          ret = ret < 0 ? ret : -EPROTO;
          goto failed;
        }

      ret = capture->sink_ops.audio(
        capture->sink_context, &capture->token, capture->frame,
        sizeof(capture->frame));
      if (ret != 0)
        {
          if (ret > 0) ret = -ENOSPC;
          goto failed;
        }

      (void)__atomic_add_fetch(&capture->frames_sent, 1,
                               __ATOMIC_RELEASE);
      (void)__atomic_add_fetch(&capture->prefill_frames_sent, 1,
                               __ATOMIC_RELEASE);
      (void)__atomic_add_fetch(&capture->bytes_sent,
                               BKVOICE_CAPTURE_FRAME_BYTES,
                               __ATOMIC_RELEASE);
    }

  memset(capture->frame, 0, sizeof(capture->frame));
  return 0;

failed:
  memset(capture->frame, 0, sizeof(capture->frame));
  return bkvoice_capture_prefill_abort(capture, ret);
}

int bkvoice_capture_set_frame_filter(
  struct bkvoice_capture_s *capture,
  bkvoice_capture_frame_filter_t observer, void *context)
{
  if (capture == NULL || observer == NULL)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&capture->state, __ATOMIC_ACQUIRE) !=
      BKVOICE_CAPTURE_RUNNING ||
      !__atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE) ||
      !__atomic_load_n(&capture->sink_started, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&capture->stop_requested, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&capture->run_started, __ATOMIC_ACQUIRE) ||
      capture->frame_fill != 0)
    {
      return -EBUSY;
    }

  if (capture->frame_filter != NULL)
    {
      return -EALREADY;
    }

  capture->frame_filter = observer;
  capture->frame_filter_context = context;
  return 0;
}

int bkvoice_capture_request_stop(struct bkvoice_capture_s *capture)
{
  enum bkvoice_capture_state_e state;
  int ret;

  if (capture == NULL)
    {
      return -EINVAL;
    }

  state = __atomic_load_n(&capture->state, __ATOMIC_ACQUIRE);
  if (state == BKVOICE_CAPTURE_STOPPING)
    {
      /* A bounded owner may have timed out because the first lower-half stop
       * did not wake its reader.  Reissue the idempotent interrupt on retry;
       * a successful retry clears the earlier interrupt error before the
       * worker accounts its final result.
       */

      ret = capture->source_ops.interrupt(capture->source_context);
      __atomic_store_n(&capture->last_error, ret, __ATOMIC_RELEASE);
      return ret;
    }

  if (state != BKVOICE_CAPTURE_RUNNING ||
      !__atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE))
    {
      return -EPERM;
    }

  __atomic_store_n(&capture->stop_requested, true, __ATOMIC_RELEASE);
  __atomic_store_n(&capture->state, BKVOICE_CAPTURE_STOPPING,
                   __ATOMIC_RELEASE);
  ret = capture->source_ops.interrupt(capture->source_context);
  if (ret < 0)
    {
      __atomic_store_n(&capture->last_error, ret, __ATOMIC_RELEASE);
    }

  return ret;
}

int bkvoice_capture_run(struct bkvoice_capture_s *capture)
{
  enum bkvoice_capture_state_e state;
  bool expected = false;
  ssize_t nread;
  size_t remaining;
  int detach_ret;
  int first = 0;
  int ret;

  if (capture == NULL)
    {
      return -EINVAL;
    }

  state = __atomic_load_n(&capture->state, __ATOMIC_ACQUIRE);
  if ((state != BKVOICE_CAPTURE_RUNNING &&
       state != BKVOICE_CAPTURE_STOPPING) ||
      !__atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE) ||
      !__atomic_load_n(&capture->sink_started, __ATOMIC_ACQUIRE))
    {
      return -EPERM;
    }

  if (!__atomic_compare_exchange_n(&capture->run_started, &expected, true,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  while (!__atomic_load_n(&capture->stop_requested, __ATOMIC_ACQUIRE))
    {
      remaining = BKVOICE_CAPTURE_FRAME_BYTES - capture->frame_fill;
      nread = capture->source_ops.read(
        capture->source_context, capture->frame + capture->frame_fill,
        remaining);
      if (nread < 0)
        {
          if (__atomic_load_n(&capture->stop_requested,
                              __ATOMIC_ACQUIRE) &&
              (nread == -EPIPE || nread == -ECANCELED ||
               nread == -EINTR))
            {
              break;
            }

          first = (int)nread;
          break;
        }

      if (nread == 0)
        {
          if (!__atomic_load_n(&capture->stop_requested,
                               __ATOMIC_ACQUIRE))
            {
              first = -EPIPE;
            }

          break;
        }

      if ((size_t)nread > remaining)
        {
          first = -EOVERFLOW;
          break;
        }

      capture->frame_fill += (size_t)nread;
      if (capture->frame_fill == BKVOICE_CAPTURE_FRAME_BYTES)
        {
          if (capture->frame_filter != NULL)
            {
              ret = capture->frame_filter(capture->frame_filter_context,
                                           &capture->token, capture->frame);
              if (ret < 0)
                {
                  first = ret;
                  break;
                }
              if (ret == 0)
                {
                  capture->frame_fill = 0;
                  continue;
                }
            }

          if (__atomic_load_n(&capture->frames_sent, __ATOMIC_ACQUIRE) ==
              UINT32_MAX ||
              __atomic_load_n(&capture->bytes_sent, __ATOMIC_ACQUIRE) >
              UINT32_MAX - BKVOICE_CAPTURE_FRAME_BYTES)
            {
              first = -EOVERFLOW;
              break;
            }

          ret = capture->sink_ops.audio(
            capture->sink_context, &capture->token, capture->frame,
            sizeof(capture->frame));
          if (ret < 0)
            {
              first = ret;
              break;
            }

          (void)__atomic_add_fetch(&capture->frames_sent, 1,
                                   __ATOMIC_RELEASE);
          (void)__atomic_add_fetch(&capture->bytes_sent,
                                   BKVOICE_CAPTURE_FRAME_BYTES,
                                   __ATOMIC_RELEASE);
          capture->frame_fill = 0;
          if (ret > 0)
            {
              break;
            }
        }
    }

  if (capture->frame_fill != 0)
    {
      (void)__atomic_add_fetch(&capture->partial_bytes_discarded,
                               capture->frame_fill, __ATOMIC_RELEASE);
      capture->frame_fill = 0;
    }

  detach_ret = capture->source_ops.detach(capture->source_context);
  if (detach_ret >= 0)
    {
      __atomic_store_n(&capture->source_attached, false,
                       __ATOMIC_RELEASE);
    }

  bkvoice_capture_first_error(&first, detach_ret);
  ret = __atomic_load_n(&capture->last_error, __ATOMIC_ACQUIRE);
  bkvoice_capture_first_error(&first, ret);
  __atomic_store_n(&capture->last_error, first, __ATOMIC_RELEASE);

  if (first < 0)
    {
      __atomic_store_n(&capture->state, BKVOICE_CAPTURE_FAULTED,
                       __ATOMIC_RELEASE);
      return first;
    }

  __atomic_store_n(&capture->state, BKVOICE_CAPTURE_STOPPED,
                   __ATOMIC_RELEASE);
  return 0;
}

int bkvoice_capture_complete(struct bkvoice_capture_s *capture)
{
  int ret;

  if (capture == NULL)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&capture->state, __ATOMIC_ACQUIRE) !=
      BKVOICE_CAPTURE_STOPPED ||
      __atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE) ||
      !__atomic_load_n(&capture->sink_started, __ATOMIC_ACQUIRE))
    {
      return -EPERM;
    }

  ret = capture->sink_ops.end(capture->sink_context, &capture->token);
  if (ret < 0)
    {
      __atomic_store_n(&capture->last_error, ret, __ATOMIC_RELEASE);
      __atomic_store_n(&capture->state, BKVOICE_CAPTURE_FAULTED,
                       __ATOMIC_RELEASE);
      return ret;
    }

  capture->last_error = 0;
  bkvoice_capture_clear_active(capture);
  __atomic_store_n(&capture->state, BKVOICE_CAPTURE_IDLE,
                   __ATOMIC_RELEASE);
  return 0;
}

int bkvoice_capture_cancel(struct bkvoice_capture_s *capture, int reason)
{
  enum bkvoice_capture_state_e state;
  int ret;
  int first = 0;

  if (capture == NULL)
    {
      return -EINVAL;
    }

  state = __atomic_load_n(&capture->state, __ATOMIC_ACQUIRE);
  if (state == BKVOICE_CAPTURE_RUNNING ||
      state == BKVOICE_CAPTURE_STOPPING)
    {
      return -EBUSY;
    }

  if (state == BKVOICE_CAPTURE_IDLE &&
      !__atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE) &&
      !__atomic_load_n(&capture->sink_started, __ATOMIC_ACQUIRE))
    {
      return -EALREADY;
    }

  if (reason >= 0)
    {
      reason = -ECANCELED;
    }

  if (__atomic_load_n(&capture->source_attached, __ATOMIC_ACQUIRE))
    {
      ret = capture->source_ops.detach(capture->source_context);
      if (ret >= 0)
        {
          __atomic_store_n(&capture->source_attached, false,
                           __ATOMIC_RELEASE);
        }

      bkvoice_capture_first_error(&first, ret);
    }

  if (__atomic_load_n(&capture->sink_started, __ATOMIC_ACQUIRE))
    {
      ret = capture->sink_ops.cancel(capture->sink_context,
                                     &capture->token, reason);
      if (ret >= 0)
        {
          __atomic_store_n(&capture->sink_started, false,
                           __ATOMIC_RELEASE);
        }

      bkvoice_capture_first_error(&first, ret);
    }

  __atomic_store_n(&capture->last_error, first, __ATOMIC_RELEASE);
  if (first < 0)
    {
      __atomic_store_n(&capture->state, BKVOICE_CAPTURE_FAULTED,
                       __ATOMIC_RELEASE);
      return first;
    }

  bkvoice_capture_clear_active(capture);
  __atomic_store_n(&capture->state, BKVOICE_CAPTURE_IDLE,
                   __ATOMIC_RELEASE);
  return 0;
}

void bkvoice_capture_snapshot(
  const struct bkvoice_capture_s *capture,
  struct bkvoice_capture_snapshot_s *snapshot)
{
  if (capture == NULL || snapshot == NULL)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  memcpy(&snapshot->token, &capture->token, sizeof(snapshot->token));
  snapshot->state = __atomic_load_n(&capture->state, __ATOMIC_ACQUIRE);
  snapshot->frames_sent = __atomic_load_n(&capture->frames_sent,
                                           __ATOMIC_ACQUIRE);
  snapshot->prefill_frames_sent = __atomic_load_n(
    &capture->prefill_frames_sent, __ATOMIC_ACQUIRE);
  snapshot->bytes_sent = __atomic_load_n(&capture->bytes_sent,
                                          __ATOMIC_ACQUIRE);
  snapshot->partial_bytes_discarded = __atomic_load_n(
    &capture->partial_bytes_discarded, __ATOMIC_ACQUIRE);
  snapshot->last_error = __atomic_load_n(&capture->last_error,
                                         __ATOMIC_ACQUIRE);
  snapshot->source_attached = __atomic_load_n(&capture->source_attached,
                                               __ATOMIC_ACQUIRE);
  snapshot->sink_started = __atomic_load_n(&capture->sink_started,
                                            __ATOMIC_ACQUIRE);
  snapshot->stop_requested = __atomic_load_n(&capture->stop_requested,
                                              __ATOMIC_ACQUIRE);
  snapshot->run_started = __atomic_load_n(&capture->run_started,
                                           __ATOMIC_ACQUIRE);
}

const char *bkvoice_capture_state_name(enum bkvoice_capture_state_e state)
{
  switch (state)
    {
      case BKVOICE_CAPTURE_IDLE:
        return "idle";
      case BKVOICE_CAPTURE_RUNNING:
        return "running";
      case BKVOICE_CAPTURE_STOPPING:
        return "stopping";
      case BKVOICE_CAPTURE_STOPPED:
        return "stopped";
      case BKVOICE_CAPTURE_FAULTED:
        return "faulted";
      default:
        return "invalid";
    }
}
