/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_voice_wake_owner.h"

#include <errno.h>
#include <string.h>
#include <syslog.h>
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
#include "bk7258_haptic_service.h"
#endif

static void publish(struct bkvoice_wake_owner_s *owner,
                    unsigned int event, int error)
{
  __atomic_store_n(&owner->observer_error, error, __ATOMIC_RELEASE);
  __atomic_store_n(&owner->event, event, __ATOMIC_RELEASE);
  if (owner->wake != NULL)
    {
      (void)sem_post(owner->wake);
    }
}

int bkvoice_wake_owner_frame_filter(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES])
{
  struct bkvoice_wake_owner_s *owner = context;
  enum bkvoice_wake_window_event_e event = BKVOICE_WAKE_EVENT_NONE;
  int16_t aligned[BKVOICE_WAKE_FRAME_SAMPLES];
  uint64_t end_ms;
  bool waiting_quiet;
  int ret;

  (void)token;
  if (owner == NULL || pcm == NULL)
    {
      return -EINVAL;
    }
  if (__atomic_load_n(&owner->terminal_latched, __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  /* The capture worker is the only live writer after the listener joined.
   * Discard wake audio before the cloud sink receives any PCM. */
  waiting_quiet = owner->window->state == BKVOICE_WAKE_WINDOW_WAITING_QUIET;
  owner->live_next_ms += 20u;
  end_ms = owner->live_next_ms;
  memcpy(aligned, pcm, sizeof(aligned));
  ret = bkvoice_wake_window_feed(owner->window, aligned,
                                 BKVOICE_WAKE_FRAME_SAMPLES, end_ms, &event);
  if (ret < 0)
    {
      __atomic_store_n(&owner->terminal_latched, true, __ATOMIC_RELEASE);
      publish(owner, BKVOICE_WAKE_OWNER_EVENT_FAULT, ret);
      return ret;
    }
  else if (event == BKVOICE_WAKE_EVENT_END_OF_SPEECH ||
           event == BKVOICE_WAKE_EVENT_NO_SPEECH ||
           event == BKVOICE_WAKE_EVENT_MAX_DURATION)
    {
      __atomic_store_n(&owner->terminal_latched, true, __ATOMIC_RELEASE);
      publish(owner, event, 0);
    }
  if (waiting_quiet &&
      owner->window->state == BKVOICE_WAKE_WINDOW_WAITING_SPEECH)
    syslog(LOG_INFO, "BKVOICE wake capture waiting-user\n");
  if (event == BKVOICE_WAKE_EVENT_SPEECH_STARTED)
    syslog(LOG_INFO, "BKVOICE wake capture user-speech\n");
  return !waiting_quiet && event != BKVOICE_WAKE_EVENT_NO_SPEECH ? 1 : 0;
}

int bkvoice_wake_owner_initialize(struct bkvoice_wake_owner_s *owner,
                                  struct bkvoice_wake_listener_s *listener,
                                  struct bkvoice_wake_window_s *window,
                                  struct bkcloud_runtime_s *cloud,
                                  sem_t *wake)
{
  if (owner == NULL || listener == NULL || window == NULL || cloud == NULL ||
      wake == NULL)
    {
      return -EINVAL;
    }

  memset(owner, 0, sizeof(*owner));
  owner->listener = listener;
  owner->window = window;
  owner->cloud = cloud;
  owner->wake = wake;
  owner->state = BKVOICE_WAKE_OWNER_IDLE;
  owner->initialized = true;
  return 0;
}

static int stop_listener(struct bkvoice_wake_owner_s *owner)
{
  int ret;

  if (!owner->listener_started)
    {
      return 0;
    }

  ret = bkvoice_wake_listener_stop(owner->listener);
  if (ret < 0)
    {
      owner->last_error = ret;
      return ret;
    }

  owner->listener_started = false;
  /* listener_stop() deliberately preserves TRIGGERED for diagnostics.  Do
   * not turn that stale status into a second cloud request after cleanup. */
  owner->trigger_consumed = true;
  if (owner->state == BKVOICE_WAKE_OWNER_LISTENING)
    {
      owner->state = BKVOICE_WAKE_OWNER_IDLE;
    }
  return 0;
}

static int cancel_and_drain(struct bkvoice_wake_owner_s *owner)
{
  int ret;

  ret = stop_listener(owner);
  if (ret < 0)
    {
      return ret;
    }

  if (owner->automatic_owned)
    {
      ret = bkcloud_runtime_cancel_drain(owner->cloud);
      if (ret < 0)
        {
          owner->last_error = ret;
          return ret;
        }
      owner->state = BKVOICE_WAKE_OWNER_DRAINING;
    }

  owner->automatic_started = false;
  owner->automatic_owned = false;
  owner->cancel_requested = false;
  __atomic_store_n(&owner->event, BKVOICE_WAKE_EVENT_NONE,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&owner->observer_error, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&owner->terminal_latched, false, __ATOMIC_RELEASE);
  owner->state = BKVOICE_WAKE_OWNER_IDLE;
  return 0;
}

int bkvoice_wake_owner_cancel(struct bkvoice_wake_owner_s *owner)
{
  if (owner == NULL || !owner->initialized)
    {
      return -EINVAL;
    }
  owner->cancel_requested = true;
  return cancel_and_drain(owner);
}

int bkvoice_wake_owner_suspend(struct bkvoice_wake_owner_s *owner)
{
  if (owner == NULL || !owner->initialized)
    {
      return -EINVAL;
    }

  return stop_listener(owner);
}

int bkvoice_wake_owner_stop(struct bkvoice_wake_owner_s *owner)
{
  return bkvoice_wake_owner_cancel(owner);
}

int bkvoice_wake_owner_close(struct bkvoice_wake_owner_s *owner)
{
  int ret = bkvoice_wake_owner_cancel(owner);
  if (ret < 0)
    {
      return ret;
    }

  /* Contexts remain intact for a caller that retries a failed close; this
   * owner never uninitializes borrowed listener/window/cloud storage. */
  owner->initialized = false;
  owner->state = BKVOICE_WAKE_OWNER_CLOSED;
  return 0;
}

static int begin_capture(struct bkvoice_wake_owner_s *owner)
{
  struct bkvoice_wake_window_snapshot_s snapshot;
  int ret;

  ret = stop_listener(owner);
  if (ret < 0)
    {
      return ret;
    }

  bkvoice_wake_window_snapshot(owner->window, &snapshot);
  if (snapshot.last_frame_ms == 0)
    {
      owner->last_error = -EINVAL;
      owner->cancel_requested = true;
      return -EINVAL;
    }

  __atomic_store_n(&owner->event, BKVOICE_WAKE_EVENT_NONE, __ATOMIC_RELEASE);
  __atomic_store_n(&owner->observer_error, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&owner->terminal_latched, false, __ATOMIC_RELEASE);
  /* After auto_begin() succeeds, only its capture worker advances this
   * timestamp.  It is initialized before that worker is allowed to run. */
  owner->live_next_ms = snapshot.last_frame_ms;
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  /* The board inhibits its motor while MIC is live. Complete this short
   * acknowledgement after listener stop and before acquiring capture. */
  ret = bkhaptic_service_pulse_wait(35u);
  if (ret < 0)
    syslog(LOG_WARNING, "BKVOICE wake haptic=%d\n", ret);
#endif
  /* Claim ownership before auto_begin(): a failure can still leave borrowed
   * capture state which only the normal cloud cancel/drain path may release. */
  owner->automatic_owned = true;
  ret = bkcloud_runtime_auto_begin(owner->cloud,
                                   bkvoice_wake_owner_frame_filter,
                                   owner);
  if (ret < 0)
    {
      owner->last_error = ret;
      /* auto_begin may have left a borrowed capture worker; never restart
       * the listener until the normal cloud cancellation path has drained. */
      owner->cancel_requested = true;
      owner->automatic_started = true;
      return ret;
    }

  owner->automatic_started = true;
  owner->state = BKVOICE_WAKE_OWNER_CAPTURE;
  return 0;
}

int bkvoice_wake_owner_step(struct bkvoice_wake_owner_s *owner, bool allowed,
                            uint64_t now_ms)
{
  struct bkvoice_wake_listener_status_s status;
  struct bkcloud_runtime_status_s cloud_status = {0};
  unsigned int event;
  int observer_error;
  int ret;

  if (owner == NULL || !owner->initialized || now_ms == 0)
    {
      return -EINVAL;
    }

  if (!allowed || owner->cancel_requested)
    {
      return cancel_and_drain(owner);
    }

  /* A terminal event is a capture-worker latch.  Do not exchange it while
   * auto_end() is retried: that would let a later frame replace END with a
   * spurious EALREADY fault. */
  if (owner->automatic_started)
    {
      /* Read worker completion before its observer latch. A completed frame
       * can publish NO_SPEECH as well as fill the PCM buffer: cancellation
       * must win before this same owner decides whether to start ASR.
       */
      bkcloud_runtime_status(owner->cloud, &cloud_status);
    }
  event = __atomic_load_n(&owner->event, __ATOMIC_ACQUIRE);
  observer_error = event == BKVOICE_WAKE_EVENT_NONE ? 0 :
    __atomic_load_n(&owner->observer_error, __ATOMIC_ACQUIRE);
  if (event == BKVOICE_WAKE_EVENT_NO_SPEECH ||
      event == BKVOICE_WAKE_OWNER_EVENT_FAULT || observer_error < 0)
    {
      owner->last_error = observer_error < 0 ? observer_error : -ENODATA;
      owner->cancel_requested = true;
      return cancel_and_drain(owner);
    }
  if (event == BKVOICE_WAKE_EVENT_END_OF_SPEECH ||
      event == BKVOICE_WAKE_EVENT_MAX_DURATION ||
      cloud_status.capture_finished)
    {
      ret = bkcloud_runtime_auto_end(owner->cloud);
      if (ret == -EAGAIN)
        {
          __atomic_store_n(&owner->event, event, __ATOMIC_RELEASE);
          return ret;
        }
      owner->automatic_started = false;
      __atomic_store_n(&owner->event, BKVOICE_WAKE_EVENT_NONE,
                       __ATOMIC_RELEASE);
      __atomic_store_n(&owner->observer_error, 0, __ATOMIC_RELEASE);
      if (ret < 0)
        {
          owner->last_error = ret;
          owner->cancel_requested = true;
          return cancel_and_drain(owner);
        }
      owner->state = BKVOICE_WAKE_OWNER_DRAINING;
      return 0;
    }

  /* No concurrent MIC use: an in-flight cloud/capture turn always keeps the
   * continuous listener stopped.  Terminal capture events above are handled
   * first, because automatic capture itself is necessarily reported busy. */
  if (bkcloud_runtime_busy(owner->cloud))
    {
      ret = stop_listener(owner);
      if (ret < 0)
        {
          return ret;
        }
      if (!owner->automatic_started)
        {
          owner->state = BKVOICE_WAKE_OWNER_DRAINING;
        }
      return 0;
    }

  if (owner->automatic_owned)
    {
      /* cloud reports idle only after it has released borrowed capture state. */
      owner->automatic_started = false;
      owner->automatic_owned = false;
      owner->state = BKVOICE_WAKE_OWNER_IDLE;
    }

  if (owner->listener_started && owner->last_error < 0)
    {
      /* Cover a partial start whose listener status was not published before
       * release failed.  stop() owns interrupt/join/release retries. */
      ret = stop_listener(owner);
      if (ret < 0)
        {
          return ret;
        }
      owner->fault_consumed = true;
      if (owner->retry_after_ms == 0)
        {
          owner->retry_after_ms = now_ms + 1000u;
        }
      return owner->last_error;
    }

  bkvoice_wake_listener_status(owner->listener, &status);
  if (status.state == BKVOICE_WAKE_LISTENER_TRIGGERED &&
      !owner->trigger_consumed)
    {
      return begin_capture(owner);
    }
  if (status.state == BKVOICE_WAKE_LISTENER_FAULTED)
    {
      if (!owner->fault_consumed)
        {
          ret = stop_listener(owner);
          if (ret < 0)
            {
              return ret;
            }
          owner->fault_consumed = true;
          owner->retry_after_ms = now_ms + 1000u;
          owner->last_error = status.result < 0 ? status.result : -EIO;
          return owner->last_error;
        }
    }

  if (!owner->listener_started)
    {
      if (owner->retry_after_ms != 0 && now_ms < owner->retry_after_ms)
        {
          return -EAGAIN;
        }
      owner->trigger_consumed = false;
      owner->fault_consumed = false;
      ret = bkvoice_wake_listener_start(owner->listener, now_ms);
      if (ret < 0)
        {
          /* start() may have acquired part of the listener MIC path before
           * release_audio() itself failed.  Always route the next owner step
           * through stop(), even if the start call reported an error. */
          owner->listener_started = true;
          owner->retry_after_ms = now_ms + 1000u;
          owner->last_error = ret;
          return ret;
        }
      owner->listener_started = true;
      owner->retry_after_ms = 0;
      owner->last_error = 0;
      owner->state = BKVOICE_WAKE_OWNER_LISTENING;
    }
  return 0;
}
