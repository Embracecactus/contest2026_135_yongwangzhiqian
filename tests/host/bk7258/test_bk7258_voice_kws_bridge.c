/* SPDX-License-Identifier: Apache-2.0 */
/* voice_kws.py 的主机桥接，直接复用现有 C 前端和触发策略。 */

#include "bk7258_voice_kws.h"

#include <stdlib.h>

void bkvoice_kws_host_default_policy(float values[4])
{
  struct bkvoice_kws_policy_s policy;

  bkvoice_kws_default_policy(&policy);
  values[0] = policy.threshold;
  values[1] = policy.release_threshold;
  values[2] = (float)policy.consecutive;
  values[3] = (float)policy.cooldown_ms;
}

void *bkvoice_kws_host_create(bkvoice_kws_infer_t infer, void *context)
{
  struct bkvoice_kws_policy_s policy;
  struct bkvoice_kws_s *kws = calloc(1, sizeof(*kws));

  if (kws == NULL)
    {
      return NULL;
    }

  bkvoice_kws_default_policy(&policy);
  if (bkvoice_kws_initialize(kws, &policy, infer, context) < 0)
    {
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
