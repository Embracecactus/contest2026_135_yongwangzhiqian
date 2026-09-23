/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __APP_BK7258_BK7258_VOICE_KWS_H
#define __APP_BK7258_BK7258_VOICE_KWS_H

#include "bk7258_voice_kws_frontend.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BKVOICE_KWS_CLASSES 3
#define BKVOICE_KWS_WAKE_CLASS 2
#define BKVOICE_KWS_INFER_HOPS 15

typedef int (*bkvoice_kws_infer_t)(void *context, const float *features,
                                  float scores[BKVOICE_KWS_CLASSES]);
typedef void (*bkvoice_kws_reset_t)(void *context);

struct bkvoice_kws_policy_s
{
  float threshold;
  float release_threshold;
  unsigned int consecutive;
  uint32_t cooldown_ms;
};

/* Existing product defaults, not a model quality claim. Host event evaluation
 * and the wake session use this same policy; tune only on validation data.
 */
void bkvoice_kws_default_policy(struct bkvoice_kws_policy_s *policy);

struct bkvoice_kws_s
{
  struct bkvoice_kws_frontend_s frontend;
  struct bkvoice_kws_policy_s policy;
  bkvoice_kws_infer_t infer;
  bkvoice_kws_infer_t step;
  bkvoice_kws_reset_t reset;
  void *context;
  float stream_scores[BKVOICE_KWS_CLASSES];
  bool stream_ready;
  float features[BKVOICE_KWS_FEATURES];
  int16_t pcm[BKVOICE_KWS_WINDOW];
  size_t pending;
  unsigned int rows;
  unsigned int hops;
  unsigned int hits;
  uint64_t last_ms;
  uint64_t triggered_ms;
  bool timestamp_valid;
  bool armed;
};

/* No recorder, task or Gateway is opened here. The AP audio owner supplies
 * ordered 20 ms frames and handles a returned wake event. Pausing flushes all
 * PCM/features; resuming or a timestamp gap requires a fresh three-second
 * window. Events must be rechecked against the owner's current IDLE state
 * and session generation before handing the MIC to the product voice turn.
 */

/* Initialize an unused object. Call bkvoice_kws_uninitialize before reusing
 * one so the microfrontend's initialization-time buffers are released.
 */
int bkvoice_kws_initialize(struct bkvoice_kws_s *kws,
                           const struct bkvoice_kws_policy_s *policy,
                           bkvoice_kws_infer_t infer, void *context);
int bkvoice_kws_initialize_version(struct bkvoice_kws_s *kws,
                                   const struct bkvoice_kws_policy_s *policy,
                                   bkvoice_kws_infer_t infer, void *context,
                                   int frontend_version);
/* Opt-in causal model: step sees exactly one new 40-bin row. Detection still
 * uses the unchanged 15-hop score policy. Pause/gap resets the model state.
 */
int bkvoice_kws_set_stream(struct bkvoice_kws_s *kws,
                           bkvoice_kws_infer_t step,
                           bkvoice_kws_reset_t reset);
void bkvoice_kws_uninitialize(struct bkvoice_kws_s *kws);
void bkvoice_kws_pause(struct bkvoice_kws_s *kws);
int bkvoice_kws_feed(struct bkvoice_kws_s *kws, const int16_t *pcm,
                     size_t samples, uint64_t end_ms, float *wake_score);

#ifdef __cplusplus
}
#endif
#endif
