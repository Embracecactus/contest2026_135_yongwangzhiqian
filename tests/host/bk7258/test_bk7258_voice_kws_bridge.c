/* SPDX-License-Identifier: Apache-2.0 */
/* Host bridge for voice_kws.py; reuses the existing C frontend and trigger
 * policy directly.
 */

#include "bk7258_voice_kws.h"

#include <stdlib.h>
#include <errno.h>

int bkvoice_kws_host_features_stream(const int16_t *pcm, size_t samples,
                                     float *features, size_t count, int version)
{
  struct bkvoice_kws_frontend_s frontend;
  if (pcm == NULL || features == NULL || samples < BKVOICE_KWS_WINDOW ||
      samples > 8u * BKVOICE_KWS_RATE || samples % BKVOICE_KWS_HOP != 0 ||
      count != (1 + (samples - BKVOICE_KWS_WINDOW) / BKVOICE_KWS_HOP) *
               BKVOICE_KWS_BINS)
    {
      return -EINVAL;
    }

  int ret = bkvoice_kws_frontend_init_version(&frontend, version);
  if (ret < 0) return ret;
  for (size_t i = 0; i < count / BKVOICE_KWS_BINS; i++)
    {
      ret = bkvoice_kws_frontend_frame(&frontend, pcm + i * BKVOICE_KWS_HOP,
                                      features + i * BKVOICE_KWS_BINS);
      if (ret < 0) break;
    }

  bkvoice_kws_frontend_uninitialize(&frontend);
  return ret;
}

void bkvoice_kws_host_default_policy(float values[4])
{
  struct bkvoice_kws_policy_s policy;

  bkvoice_kws_default_policy(&policy);
  values[0] = policy.threshold;
  values[1] = policy.release_threshold;
  values[2] = (float)policy.consecutive;
  values[3] = (float)policy.cooldown_ms;
}

void *bkvoice_kws_host_create_version(bkvoice_kws_infer_t infer, void *context,
                                     int frontend_version)
{
  struct bkvoice_kws_policy_s policy;
  struct bkvoice_kws_s *kws = calloc(1, sizeof(*kws));

  if (kws == NULL)
    {
      return NULL;
    }

  bkvoice_kws_default_policy(&policy);
  if (bkvoice_kws_initialize_version(kws, &policy, infer, context,
                                     frontend_version) < 0)
    {
      free(kws);
      return NULL;
    }

  return kws;
}

void *bkvoice_kws_host_create(bkvoice_kws_infer_t infer, void *context)
{
  return bkvoice_kws_host_create_version(infer, context, 1);
}

void *bkvoice_kws_host_create_stream(bkvoice_kws_infer_t step,
                                    bkvoice_kws_reset_t reset, void *context,
                                    int frontend_version)
{
  struct bkvoice_kws_s *kws =
    bkvoice_kws_host_create_version(step, context, frontend_version);
  if (kws != NULL && bkvoice_kws_set_stream(kws, step, reset) < 0)
    {
      bkvoice_kws_uninitialize(kws);
      free(kws);
      return NULL;
    }

  return kws;
}

int bkvoice_kws_host_feed(void *host, const int16_t *pcm, size_t samples,
                          uint64_t end_ms, float *wake_score)
{
  return bkvoice_kws_feed(host, pcm, samples, end_ms, wake_score);
}

void bkvoice_kws_host_pause(void *host)
{
  if (host != NULL)
    {
      bkvoice_kws_pause(host);
    }
}

void bkvoice_kws_host_destroy(void *host)
{
  if (host != NULL)
    {
      bkvoice_kws_uninitialize(host);
      free(host);
    }
}
