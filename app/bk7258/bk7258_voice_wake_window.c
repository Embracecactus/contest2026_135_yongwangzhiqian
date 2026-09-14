/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_wake_window.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

static bool bkvoice_wake_policy_valid(
  const struct bkvoice_wake_window_policy_s *policy)
{
  return policy != NULL && policy->minimum_speech_mean_abs > 0 &&
         policy->minimum_speech_mean_abs <= 32768u &&
         policy->speech_to_noise_q8 >= 256u &&
         policy->speech_to_noise_q8 <= 4096u &&
         policy->speech_confirm_frames > 0 &&
         policy->wake_quiet_frames > 0 &&
         policy->wake_quiet_frames < policy->no_speech_frames &&
         policy->silence_end_frames > 0 && policy->no_speech_frames > 0 &&
         policy->maximum_turn_frames >= policy->speech_confirm_frames &&
         policy->maximum_turn_frames >= policy->silence_end_frames &&
         policy->maximum_turn_frames >= policy->no_speech_frames;
}

static void bkvoice_wake_clear_history(struct bkvoice_wake_window_s *window)
{
  window->post_trigger_frames = 0;
  window->speech_frames = 0;
  window->silence_frames = 0;
  window->last_mean_abs = 0;
  window->trigger_ms = 0;
  window->noise_mean_abs = window->policy.minimum_speech_mean_abs / 4u;
  if (window->noise_mean_abs == 0)
    {
      window->noise_mean_abs = 1;
    }
}

static uint32_t bkvoice_wake_mean_abs(const int16_t *pcm)
{
  uint32_t total = 0;

  for (size_t i = 0; i < BKVOICE_WAKE_FRAME_SAMPLES; i++)
    {
      int32_t sample = pcm[i];

      total += (uint32_t)(sample < 0 ? -sample : sample);
    }

  return total / BKVOICE_WAKE_FRAME_SAMPLES;
}

static uint32_t bkvoice_wake_threshold(
  const struct bkvoice_wake_window_s *window)
{
  uint64_t threshold =
    ((uint64_t)window->noise_mean_abs * window->policy.speech_to_noise_q8 +
     255u) >> 8;

  if (threshold < window->policy.minimum_speech_mean_abs)
    {
      threshold = window->policy.minimum_speech_mean_abs;
    }

  return threshold > UINT32_MAX ? UINT32_MAX : (uint32_t)threshold;
}

static void bkvoice_wake_update_noise(struct bkvoice_wake_window_s *window,
                                      uint32_t mean_abs)
{
  uint32_t noise = window->noise_mean_abs;

  if (mean_abs < noise)
    {
      window->noise_mean_abs = (noise * 3u + mean_abs + 2u) / 4u;
    }
  else
    {
      uint32_t ceiling = noise > UINT32_MAX / 2u ? UINT32_MAX : noise * 2u;
      uint32_t sample = mean_abs > ceiling ? ceiling : mean_abs;

      window->noise_mean_abs = (noise * 31u + sample + 16u) / 32u;
    }

  if (window->noise_mean_abs == 0)
    {
      window->noise_mean_abs = 1;
    }
}

static int bkvoice_wake_timestamp(struct bkvoice_wake_window_s *window,
                                  uint64_t end_ms, bool listening)
{
  if (end_ms == 0)
    {
      return -EINVAL;
    }

  if (window->timestamp_valid &&
      (end_ms <= window->last_frame_ms ||
       end_ms - window->last_frame_ms != 20u))
    {
      bool backwards = end_ms <= window->last_frame_ms;

      if (!listening || backwards)
        {
          window->state = BKVOICE_WAKE_WINDOW_FAULTED;
          window->last_error = -ESTALE;
          return -ESTALE;
        }

      bkvoice_wake_clear_history(window);
    }

  window->last_frame_ms = end_ms;
  window->timestamp_valid = true;
  return 0;
}

int bkvoice_wake_window_initialize(
  struct bkvoice_wake_window_s *window,
  const struct bkvoice_wake_window_policy_s *policy)
{
  if (window == NULL || !bkvoice_wake_policy_valid(policy))
    {
      return -EINVAL;
    }

  memset(window, 0, sizeof(*window));
  window->policy = *policy;
  window->state = BKVOICE_WAKE_WINDOW_LISTENING;
  bkvoice_wake_clear_history(window);
  return 0;
}

void bkvoice_wake_window_uninitialize(struct bkvoice_wake_window_s *window)
{
  if (window != NULL)
    {
      memset(window, 0, sizeof(*window));
    }
}

void bkvoice_wake_window_reset(struct bkvoice_wake_window_s *window)
{
  if (window == NULL)
    {
      return;
    }

  bkvoice_wake_clear_history(window);
  window->last_frame_ms = 0;
  window->last_error = 0;
  window->timestamp_valid = false;
  window->state = BKVOICE_WAKE_WINDOW_LISTENING;
}

int bkvoice_wake_window_observe(struct bkvoice_wake_window_s *window,
                                const int16_t *pcm, size_t samples,
                                uint64_t end_ms)
{
  int ret;

  if (window == NULL || pcm == NULL || samples != BKVOICE_WAKE_FRAME_SAMPLES)
    {
      return -EINVAL;
    }

  if (window->state != BKVOICE_WAKE_WINDOW_LISTENING)
    {
      return window->state == BKVOICE_WAKE_WINDOW_FAULTED ? -EIO : -EBUSY;
    }

  ret = bkvoice_wake_timestamp(window, end_ms, true);
  if (ret < 0)
    {
      return ret;
    }

  window->last_mean_abs = bkvoice_wake_mean_abs(pcm);
  bkvoice_wake_update_noise(window, window->last_mean_abs);
  return 0;
}

int bkvoice_wake_window_trigger(struct bkvoice_wake_window_s *window,
                                uint64_t end_ms)
{
  if (window == NULL || window->state != BKVOICE_WAKE_WINDOW_LISTENING ||
      !window->timestamp_valid ||
      end_ms != window->last_frame_ms)
    {
      return -EINVAL;
    }

  window->post_trigger_frames = 0;
  window->speech_frames = 0;
  window->silence_frames = 0;
  window->trigger_ms = end_ms;
  window->last_error = 0;
  window->state = BKVOICE_WAKE_WINDOW_WAITING_QUIET;
  return 0;
}

int bkvoice_wake_window_feed(
  struct bkvoice_wake_window_s *window, const int16_t *pcm, size_t samples,
  uint64_t end_ms, enum bkvoice_wake_window_event_e *event)
{
  uint32_t threshold;
  bool speech;
  int ret;

  if (event != NULL)
    {
      *event = BKVOICE_WAKE_EVENT_NONE;
    }

  if (window == NULL || pcm == NULL || event == NULL ||
      samples != BKVOICE_WAKE_FRAME_SAMPLES)
    {
      return -EINVAL;
    }

  if (window->state != BKVOICE_WAKE_WINDOW_WAITING_QUIET &&
      window->state != BKVOICE_WAKE_WINDOW_WAITING_SPEECH &&
      window->state != BKVOICE_WAKE_WINDOW_CAPTURING)
    {
      return window->state == BKVOICE_WAKE_WINDOW_FAULTED ? -EIO : -EALREADY;
    }

  ret = bkvoice_wake_timestamp(window, end_ms, false);
  if (ret < 0)
    {
      return ret;
    }

  window->post_trigger_frames++;
  window->last_mean_abs = bkvoice_wake_mean_abs(pcm);
  threshold = bkvoice_wake_threshold(window);
  speech = window->last_mean_abs >= threshold;

  if (window->state == BKVOICE_WAKE_WINDOW_WAITING_QUIET)
    {
      /* KWS may fire before the last syllable ends. It is not user speech
       * for the new turn. Wait for a short acoustic boundary first. */
      window->silence_frames = speech ? 0 : window->silence_frames + 1u;
      if (window->silence_frames >= window->policy.wake_quiet_frames)
        {
          window->state = BKVOICE_WAKE_WINDOW_WAITING_SPEECH;
          window->silence_frames = 0;
        }
    }
  else if (window->state == BKVOICE_WAKE_WINDOW_WAITING_SPEECH)
    {
      window->speech_frames = speech ? window->speech_frames + 1u : 0;
      if (window->speech_frames >= window->policy.speech_confirm_frames)
        {
          window->state = BKVOICE_WAKE_WINDOW_CAPTURING;
          window->silence_frames = 0;
          *event = BKVOICE_WAKE_EVENT_SPEECH_STARTED;
        }
    }
  else
    {
      window->silence_frames = speech ? 0 : window->silence_frames + 1u;
      if (window->silence_frames >= window->policy.silence_end_frames)
        {
          window->state = BKVOICE_WAKE_WINDOW_COMPLETE;
          *event = BKVOICE_WAKE_EVENT_END_OF_SPEECH;
        }
    }

  if ((window->state == BKVOICE_WAKE_WINDOW_WAITING_QUIET ||
       window->state == BKVOICE_WAKE_WINDOW_WAITING_SPEECH) &&
      window->post_trigger_frames >= window->policy.no_speech_frames)
    {
      window->state = BKVOICE_WAKE_WINDOW_COMPLETE;
      *event = BKVOICE_WAKE_EVENT_NO_SPEECH;
    }
  if (window->state != BKVOICE_WAKE_WINDOW_COMPLETE &&
      window->post_trigger_frames >= window->policy.maximum_turn_frames)
    {
      window->state = BKVOICE_WAKE_WINDOW_COMPLETE;
      *event = BKVOICE_WAKE_EVENT_MAX_DURATION;
    }

  return 0;
}

void bkvoice_wake_window_snapshot(
  const struct bkvoice_wake_window_s *window,
  struct bkvoice_wake_window_snapshot_s *snapshot)
{
  if (window == NULL || snapshot == NULL)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->state = window->state;
  snapshot->noise_mean_abs = window->noise_mean_abs;
  snapshot->speech_threshold = bkvoice_wake_threshold(window);
  snapshot->last_mean_abs = window->last_mean_abs;
  snapshot->post_trigger_frames = window->post_trigger_frames;
  snapshot->speech_frames = window->speech_frames;
  snapshot->silence_frames = window->silence_frames;
  snapshot->last_frame_ms = window->last_frame_ms;
  snapshot->trigger_ms = window->trigger_ms;
}
