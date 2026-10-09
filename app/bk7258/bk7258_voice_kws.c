/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_kws.h"
#include <errno.h>
#include <math.h>
#include <string.h>
#ifdef __NuttX__
#include <syslog.h>
#endif

void bkvoice_kws_default_policy(struct bkvoice_kws_policy_s *policy)
{
  if (policy != NULL)
    {
      policy->threshold = .60f;
      policy->release_threshold = .20f;
      policy->consecutive = 2;
      policy->cooldown_ms = 1000u;
    }
}

void bkvoice_kws_pause(struct bkvoice_kws_s *kws)
{
  bkvoice_kws_frontend_reset(&kws->frontend);
  memset(kws->features, 0, sizeof(kws->features));
  memset(kws->pcm, 0, sizeof(kws->pcm));
  kws->pending = 0;
  kws->rows = 0;
  kws->hops = 0;
  kws->hits = 0;
  kws->stream_ready = false;
  memset(kws->stream_scores, 0, sizeof(kws->stream_scores));
  if (kws->reset != NULL) kws->reset(kws->context);
  /* Retain both the monotonic timestamp and trigger/release latch across
   * pauses. A new boot/clock epoch requires explicit reinitialization.
   */
}

int bkvoice_kws_set_stream(struct bkvoice_kws_s *kws,
                           bkvoice_kws_infer_t step,
                           bkvoice_kws_reset_t reset)
{
  if (kws == NULL || kws->infer == NULL || step == NULL || reset == NULL)
    {
      return -EINVAL;
    }

  kws->step = step;
  kws->reset = reset;
  bkvoice_kws_pause(kws);
  return 0;
}

void bkvoice_kws_uninitialize(struct bkvoice_kws_s *kws)
{
  if (kws != NULL)
    {
      bkvoice_kws_frontend_uninitialize(&kws->frontend);
      memset(kws, 0, sizeof(*kws));
    }
}

int bkvoice_kws_initialize(struct bkvoice_kws_s *kws,
                           const struct bkvoice_kws_policy_s *policy,
                           bkvoice_kws_infer_t infer, void *context)
{
  return bkvoice_kws_initialize_version(kws, policy, infer, context, 1);
}

int bkvoice_kws_initialize_version(struct bkvoice_kws_s *kws,
                                   const struct bkvoice_kws_policy_s *policy,
                                   bkvoice_kws_infer_t infer, void *context,
                                   int frontend_version)
{
  if (kws == NULL || policy == NULL || infer == NULL ||
      !isfinite(policy->threshold) || !isfinite(policy->release_threshold) ||
      policy->threshold <= 0.0f || policy->threshold > 1.0f ||
      policy->release_threshold < 0.0f ||
      policy->release_threshold >= policy->threshold ||
      policy->consecutive == 0 || policy->consecutive > 100 ||
      policy->cooldown_ms > 60000)
    {
      return -EINVAL;
    }

  memset(kws, 0, sizeof(*kws));
  {
    int ret = bkvoice_kws_frontend_init_version(&kws->frontend,
                                                frontend_version);
    if (ret < 0)
      {
        return ret;
      }
  }
  kws->policy = *policy;
  kws->infer = infer;
  kws->context = context;
  kws->armed = true;
  return 0;
}

int bkvoice_kws_feed(struct bkvoice_kws_s *kws, const int16_t *pcm,
                     size_t samples, uint64_t end_ms, float *wake_score)
{
  float scores[BKVOICE_KWS_CLASSES];
  float total = 0.0f;
  size_t consumed = 0;
  int ret;

  if (wake_score != NULL)
    {
      *wake_score = 0.0f;
    }

  if (kws == NULL || kws->infer == NULL || pcm == NULL ||
      samples != BKVOICE_KWS_HOP || wake_score == NULL)
    {
      return -EINVAL;
    }

  if (kws->timestamp_valid &&
      (end_ms <= kws->last_ms || end_ms - kws->last_ms != 20))
    {
      bool backwards = end_ms <= kws->last_ms;
      bkvoice_kws_pause(kws);
      if (backwards)
        {
          return -ESTALE;
        }
    }

  kws->last_ms = end_ms;
  kws->timestamp_valid = true;
  while (consumed < samples)
    {
      size_t take = BKVOICE_KWS_WINDOW - kws->pending;
      if (take > samples - consumed)
        {
          take = samples - consumed;
        }

      memcpy(kws->pcm + kws->pending, pcm + consumed,
             take * sizeof(*pcm));
      kws->pending += take;
      consumed += take;
      if (kws->pending == BKVOICE_KWS_WINDOW)
        {
          if (kws->step != NULL)
            {
              kws->rows = 1;
            }
          else if (kws->rows == BKVOICE_KWS_ROWS)
            {
              memmove(kws->features, kws->features + BKVOICE_KWS_BINS,
                      sizeof(float) * (BKVOICE_KWS_FEATURES -
                                       BKVOICE_KWS_BINS));
            }
          else
            {
              kws->rows++;
            }

          ret = bkvoice_kws_frontend_frame(&kws->frontend, kws->pcm,
            kws->features + (kws->rows - 1) * BKVOICE_KWS_BINS);
          if (ret < 0)
            {
              bkvoice_kws_pause(kws);
              return ret;
            }
          if (kws->step != NULL)
            {
              ret = kws->step(kws->context, kws->features,
                              kws->stream_scores);
              if (ret < 0)
                {
                  bkvoice_kws_pause(kws);
                  return ret;
                }

              kws->stream_ready = true;
            }
          memmove(kws->pcm, kws->pcm + BKVOICE_KWS_HOP,
                  (BKVOICE_KWS_WINDOW - BKVOICE_KWS_HOP) * sizeof(*pcm));
          kws->pending = BKVOICE_KWS_WINDOW - BKVOICE_KWS_HOP;
        }
    }

  kws->hops++;
  if ((kws->step != NULL ? !kws->stream_ready :
       kws->rows < BKVOICE_KWS_ROWS) ||
      kws->hops % BKVOICE_KWS_INFER_HOPS != 0)
    {
      return 0;
    }

  kws->hops = 0;
  if (kws->step != NULL)
    {
      memcpy(scores, kws->stream_scores, sizeof(scores));
      ret = 0;
    }
  else
    {
      ret = kws->infer(kws->context, kws->features, scores);
    }
  if (ret < 0)
    {
      bkvoice_kws_pause(kws);
      return ret;
    }

  for (unsigned int i = 0; i < BKVOICE_KWS_CLASSES; i++)
    {
      if (!isfinite(scores[i]) || scores[i] < 0.0f || scores[i] > 1.0f)
        {
          bkvoice_kws_pause(kws);
          return -EPROTO;
        }

      total += scores[i];
    }

  if (total < 0.95f || total > 1.05f)
    {
      bkvoice_kws_pause(kws);
      return -EPROTO;
    }

  if (!kws->armed && scores[BKVOICE_KWS_WAKE_CLASS] <=
      kws->policy.release_threshold && end_ms >= kws->triggered_ms &&
      end_ms - kws->triggered_ms >= kws->policy.cooldown_ms)
    {
      kws->armed = true;
    }

  if (!kws->armed || scores[BKVOICE_KWS_WAKE_CLASS] < kws->policy.threshold ||
      scores[BKVOICE_KWS_WAKE_CLASS] <= scores[0] ||
      scores[BKVOICE_KWS_WAKE_CLASS] <= scores[1])
    {
#ifdef __NuttX__
      if (kws->hits != 0)
        {
          syslog(LOG_INFO, "BKVOICE KWS candidate released hits=%u "
                 "scores=%u/%u/%u\n", kws->hits,
                 (unsigned int)(scores[0] * 1000),
                 (unsigned int)(scores[1] * 1000),
                 (unsigned int)(scores[2] * 1000));
        }
#endif
      kws->hits = 0;
      return 0;
    }

  if (++kws->hits < kws->policy.consecutive)
    {
#ifdef __NuttX__
      syslog(LOG_INFO, "BKVOICE KWS candidate hits=%u/%u score=%u\n",
             kws->hits, kws->policy.consecutive,
             (unsigned int)(scores[BKVOICE_KWS_WAKE_CLASS] * 1000));
#endif
      return 0;
    }

  kws->hits = 0;
  kws->armed = false;
  kws->triggered_ms = end_ms;
  *wake_score = scores[BKVOICE_KWS_WAKE_CLASS];
  return 1;
}
