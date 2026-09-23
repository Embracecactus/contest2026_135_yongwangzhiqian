/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic state-machine coverage only; this does not assess wake accuracy. */

#include "bk7258_voice_kws.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum score_mode_e
{
  SCORE_HIGH,
  SCORE_LOW,
  SCORE_ERROR,
  SCORE_NAN
};

#define WARMUP_HOPS (BKVOICE_KWS_SAMPLES / BKVOICE_KWS_HOP)
#define FIRST_WAKE (WARMUP_HOPS + 2 * BKVOICE_KWS_INFER_HOPS - 1)
#define RELEASE_END (FIRST_WAKE + 1 + BKVOICE_KWS_INFER_HOPS)
#define SECOND_WAKE (RELEASE_END + 3 * BKVOICE_KWS_INFER_HOPS - 1)

struct infer_context_s
{
  enum score_mode_e mode;
  unsigned int calls;
  unsigned int resets;
};

static void fake_reset(void *context)
{
  struct infer_context_s *state = context;
  state->resets++;
}

static int fake_infer(void *context, const float *features, float scores[3])
{
  struct infer_context_s *state = context;

  assert(features != NULL);
  state->calls++;
  if (state->mode == SCORE_ERROR)
    {
      return -EIO;
    }

  scores[0] = state->mode == SCORE_LOW ? 0.5f : 0.05f;
  scores[1] = state->mode == SCORE_LOW ? 0.4f : 0.05f;
  scores[2] = state->mode == SCORE_LOW ? 0.1f : 0.9f;
  if (state->mode == SCORE_NAN)
    {
      scores[2] = NAN;
    }

  return 0;
}

static int feed_one(struct bkvoice_kws_s *kws, const int16_t *pcm,
                    unsigned int frame, float *score)
{
  return bkvoice_kws_feed(kws, pcm + frame * BKVOICE_KWS_HOP,
                          BKVOICE_KWS_HOP, (uint64_t)(frame + 1) * 20,
                          score);
}

static void initialize(struct bkvoice_kws_s *kws, struct infer_context_s *state)
{
  const struct bkvoice_kws_policy_s policy =
    {
      .threshold = 0.8f,
      .release_threshold = 0.3f,
      .consecutive = 3,
      .cooldown_ms = 100,
    };

  memset(state, 0, sizeof(*state));
  state->mode = SCORE_HIGH;
  assert(bkvoice_kws_initialize(kws, &policy, fake_infer, state) == 0);
}

static void direct_configure(struct FrontendState *state)
{
  struct FrontendConfig config;

  FrontendFillConfigWithDefaults(&config);
  config.window.size_ms = 30;
  config.window.step_size_ms = 20;
  config.filterbank.num_channels = BKVOICE_KWS_BINS;
  config.filterbank.lower_band_limit = 125.0f;
  config.filterbank.upper_band_limit = 7500.0f;
  config.noise_reduction.even_smoothing = 0.0f;
  config.noise_reduction.odd_smoothing = 0.0f;
  config.noise_reduction.min_signal_remaining = 1.0f;
  config.pcan_gain_control.enable_pcan = 0;
  config.log_scale.enable_log = 1;
  config.log_scale.scale_shift = 6;
  assert(FrontendPopulateState(&config, state, BKVOICE_KWS_RATE));
}

int main(void)
{
  struct bkvoice_kws_s kws;
  struct FrontendState direct;
  struct infer_context_s state;
  float offline[BKVOICE_KWS_FEATURES];
  /* The gating/cooldown sequence intentionally continues past warmup. */
  int16_t pcm[(SECOND_WAKE + 2) * BKVOICE_KWS_HOP];
  float score = 0.0f;

  for (unsigned int i = 0; i < sizeof(pcm) / sizeof(pcm[0]); i++)
    {
      pcm[i] = (int16_t)((i * 37u) % 30000u - 15000);
    }

  direct_configure(&direct);
  assert(bkvoice_kws_features(pcm, BKVOICE_KWS_SAMPLES, offline,
                               BKVOICE_KWS_FEATURES) == 0);
  for (unsigned int row = 0; row < BKVOICE_KWS_ROWS; row++)
    {
      struct FrontendOutput output;
      size_t read = 0;

      FrontendReset(&direct);
      output = FrontendProcessSamples(&direct, pcm + row * BKVOICE_KWS_HOP,
                                      BKVOICE_KWS_WINDOW, &read);
      assert(read == BKVOICE_KWS_WINDOW && output.size == BKVOICE_KWS_BINS);
      for (unsigned int bin = 0; bin < BKVOICE_KWS_BINS; bin++)
        {
          assert((uint16_t)offline[row * BKVOICE_KWS_BINS + bin] ==
                 output.values[bin]);
        }
    }
  FrontendFreeStateContents(&direct);
  initialize(&kws, &state);

  /* Use the production context and cadence, not the retired 2 s policy. */
  assert(WARMUP_HOPS % BKVOICE_KWS_INFER_HOPS == 0);
  for (unsigned int frame = 0; frame < WARMUP_HOPS - 1; frame++)
    {
      assert(feed_one(&kws, pcm, frame, &score) == 0);
    }

  assert(state.calls == 0);
  assert(feed_one(&kws, pcm, WARMUP_HOPS - 1, &score) == 0);
  assert(state.calls == 1);
  for (unsigned int i = 0; i < BKVOICE_KWS_FEATURES; i++)
    {
      assert(fabsf(kws.features[i] - offline[i]) < 0.00005f);
    }

  /* Three scheduled high scores gate the wake. */
  for (unsigned int frame = WARMUP_HOPS; frame < FIRST_WAKE; frame++)
    {
      assert(feed_one(&kws, pcm, frame, &score) == 0);
    }

  assert(feed_one(&kws, pcm, FIRST_WAKE, &score) == 1);
  assert(fabsf(score - 0.9f) < 0.00001f);
  assert(!kws.armed);
  state.mode = SCORE_LOW;
  for (unsigned int frame = FIRST_WAKE + 1; frame < RELEASE_END; frame++)
    {
      assert(feed_one(&kws, pcm, frame, &score) == 0);
    }

  assert(kws.armed);
  state.mode = SCORE_HIGH;
  for (unsigned int frame = RELEASE_END; frame < SECOND_WAKE; frame++)
    {
      assert(feed_one(&kws, pcm, frame, &score) == 0);
    }

  assert(feed_one(&kws, pcm, SECOND_WAKE, &score) == 1);

  bkvoice_kws_pause(&kws);
  assert(kws.pending == 0 && kws.rows == 0 && kws.hops == 0 && kws.hits == 0);
  assert(kws.timestamp_valid);
  assert(feed_one(&kws, pcm, 0, &score) == -ESTALE);
  assert(feed_one(&kws, pcm, SECOND_WAKE + 1, &score) == 0);
  assert(kws.pending == BKVOICE_KWS_HOP && kws.rows == 0);

  bkvoice_kws_uninitialize(&kws);
  initialize(&kws, &state);
  assert(feed_one(&kws, pcm, 0, &score) == 0);
  assert(bkvoice_kws_feed(&kws, pcm + BKVOICE_KWS_HOP, BKVOICE_KWS_HOP,
                          60, &score) == 0);
  assert(kws.pending == BKVOICE_KWS_HOP && kws.rows == 0);
  assert(kws.timestamp_valid && kws.last_ms == 60);
  assert(bkvoice_kws_feed(&kws, pcm, BKVOICE_KWS_HOP, 60, &score) == -ESTALE);
  assert(kws.timestamp_valid && kws.pending == 0);
  assert(bkvoice_kws_feed(&kws, pcm, BKVOICE_KWS_HOP, 40, &score) == -ESTALE);

  bkvoice_kws_uninitialize(&kws);
  initialize(&kws, &state);
  state.mode = SCORE_ERROR;
  for (unsigned int frame = 0; frame < WARMUP_HOPS - 1; frame++)
    {
      assert(feed_one(&kws, pcm, frame, &score) == 0);
    }

  assert(feed_one(&kws, pcm, WARMUP_HOPS - 1, &score) == -EIO);
  assert(kws.pending == 0 && kws.rows == 0 && kws.timestamp_valid);

  bkvoice_kws_uninitialize(&kws);
  initialize(&kws, &state);
  state.mode = SCORE_NAN;
  for (unsigned int frame = 0; frame < WARMUP_HOPS - 1; frame++)
    {
      assert(feed_one(&kws, pcm, frame, &score) == 0);
    }

  assert(feed_one(&kws, pcm, WARMUP_HOPS - 1, &score) == -EPROTO);
  assert(kws.pending == 0 && kws.rows == 0 && kws.timestamp_valid);
  bkvoice_kws_uninitialize(&kws);

  /* V2 batch training and hop-by-hop runtime must produce identical rows.
   * Pausing must reset adaptive state, not just the feature window.
   */

  {
    struct bkvoice_kws_policy_s policy;
    bkvoice_kws_default_policy(&policy);
    memset(&state, 0, sizeof(state));
    state.mode = SCORE_LOW;
    assert(bkvoice_kws_features_version(pcm, BKVOICE_KWS_SAMPLES, offline,
             BKVOICE_KWS_FEATURES, 2, 0) == 0);
    assert(bkvoice_kws_initialize_version(&kws, &policy, fake_infer,
             &state, 2) == 0);
    for (unsigned int frame = 0; frame < WARMUP_HOPS; frame++)
      {
        assert(feed_one(&kws, pcm, frame, &score) == 0);
      }

    assert(state.calls == 1);
    assert(memcmp(kws.features, offline, sizeof(offline)) == 0);
    bkvoice_kws_pause(&kws);
    assert(kws.frontend.warmed == 0);
    for (unsigned int frame = 0; frame < WARMUP_HOPS; frame++)
      {
        assert(bkvoice_kws_feed(&kws, pcm + frame * BKVOICE_KWS_HOP,
                 BKVOICE_KWS_HOP, (WARMUP_HOPS + frame + 1) * 20,
                 &score) == 0);
      }

    assert(state.calls == 2);
    assert(memcmp(kws.features, offline, sizeof(offline)) == 0);
    bkvoice_kws_uninitialize(&kws);
  }

  /* Incremental mode consumes each new frontend row exactly once, without
   * waiting for a 3-second window; reset and genuine gaps clear model state.
   */
  initialize(&kws, &state);
  state.mode = SCORE_LOW;
  assert(bkvoice_kws_features(pcm, BKVOICE_KWS_SAMPLES, offline,
                            BKVOICE_KWS_FEATURES) == 0);
  assert(bkvoice_kws_set_stream(&kws, fake_infer, fake_reset) == 0);
  assert(state.resets == 1);
  for (unsigned int frame = 0; frame < WARMUP_HOPS; frame++)
    {
      assert(feed_one(&kws, pcm, frame, &score) == 0);
      assert(state.calls == frame);
      assert(kws.rows <= 1);
      if (frame > 0)
        assert(memcmp(kws.features, offline + (frame - 1) * BKVOICE_KWS_BINS,
                      BKVOICE_KWS_BINS * sizeof(float)) == 0);
    }
  bkvoice_kws_pause(&kws);
  assert(state.resets == 2 && !kws.stream_ready);
  assert(bkvoice_kws_feed(&kws, pcm, BKVOICE_KWS_HOP, 3020, &score) == 0);
  assert(state.calls == WARMUP_HOPS - 1);
  assert(bkvoice_kws_feed(&kws, pcm + BKVOICE_KWS_HOP,
                         BKVOICE_KWS_HOP, 3040, &score) == 0);
  assert(state.calls == WARMUP_HOPS);
  assert(bkvoice_kws_feed(&kws, pcm, BKVOICE_KWS_HOP, 5000, &score) == 0);
  assert(state.resets == 3 && !kws.stream_ready);
  bkvoice_kws_uninitialize(&kws);
  return 0;
}
