/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_kws_frontend.h"

#include <errno.h>
#include <string.h>

#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

int bkvoice_kws_frontend_init(struct bkvoice_kws_frontend_s *frontend)
{
  return bkvoice_kws_frontend_init_version(frontend, 1);
}

int bkvoice_kws_frontend_init_version(struct bkvoice_kws_frontend_s *frontend,
                                     int version)
{
  struct FrontendConfig config;

  if (frontend == NULL || (version != 1 && version != 2))
    {
      return -EINVAL;
    }

  memset(frontend, 0, sizeof(*frontend));
  FrontendFillConfigWithDefaults(&config);
  config.window.size_ms = 30;
  config.window.step_size_ms = 20;
  config.filterbank.num_channels = BKVOICE_KWS_BINS;
  config.filterbank.lower_band_limit = 125.0f;
  config.filterbank.upper_band_limit = 7500.0f;
  if (version == 1)
    {
      config.noise_reduction.even_smoothing = 0.0f;
      config.noise_reduction.odd_smoothing = 0.0f;
      config.noise_reduction.min_signal_remaining = 1.0f;
      config.pcan_gain_control.enable_pcan = 0;
    }
  else
    {
      /* Keep upstream smoothing and PCAN coefficients. Retain at least
       * 20 percent of each band rather than the default 5 percent floor.
       * This is feature-domain PCAN, not waveform AGC or PCEN.
       */

      config.noise_reduction.min_signal_remaining = 0.20f;
      config.pcan_gain_control.enable_pcan = 1;
    }
  config.log_scale.enable_log = 1;
  config.log_scale.scale_shift = 6;
  if (!FrontendPopulateState(&config, &frontend->state, BKVOICE_KWS_RATE))
    {
      FrontendFreeStateContents(&frontend->state);
      memset(frontend, 0, sizeof(*frontend));
      return -ENOMEM;
    }

  frontend->initialized = 1;
  frontend->version = version;
  return 0;
}

void bkvoice_kws_frontend_reset(struct bkvoice_kws_frontend_s *frontend)
{
  if (frontend != NULL && frontend->initialized)
    {
      FrontendReset(&frontend->state);
      frontend->warmed = 0;
    }
}

void bkvoice_kws_frontend_uninitialize(struct bkvoice_kws_frontend_s *frontend)
{
  if (frontend != NULL && frontend->initialized)
    {
      FrontendFreeStateContents(&frontend->state);
      memset(frontend, 0, sizeof(*frontend));
    }
}

int bkvoice_kws_frontend_frame(struct bkvoice_kws_frontend_s *frontend,
                               const int16_t *pcm, float *features)
{
  struct FrontendOutput output;
  size_t samples_read = 0;
  size_t samples = BKVOICE_KWS_WINDOW;

  if (frontend == NULL || !frontend->initialized || pcm == NULL ||
      features == NULL)
    {
      return -EINVAL;
    }

  /* The caller supplies an overlapping frame. V2's upstream window already
   * retains the overlap, so only append the new hop after the first frame.
   * Never reset the noise estimate between frames in a continuous stream.
   */

  if (frontend->version == 1)
    {
      FrontendReset(&frontend->state);
    }
  else if (frontend->warmed)
    {
      pcm += BKVOICE_KWS_WINDOW - BKVOICE_KWS_HOP;
      samples = BKVOICE_KWS_HOP;
    }

  output = FrontendProcessSamples(&frontend->state, pcm, samples,
                                  &samples_read);
  if (samples_read != samples || output.values == NULL ||
      output.size != BKVOICE_KWS_BINS)
    {
      return -EPROTO;
    }

  frontend->warmed = 1;

  for (size_t i = 0; i < BKVOICE_KWS_BINS; i++)
    {
      features[i] = (float)output.values[i];
    }

  return 0;
}

int bkvoice_kws_features(const int16_t *pcm, size_t samples,
                         float *features, size_t count)
{
  return bkvoice_kws_features_version(pcm, samples, features, count, 1, 0);
}

int bkvoice_kws_features_version(const int16_t *pcm, size_t samples,
                                float *features, size_t count, int version,
                                size_t warmup_hops)
{
  struct bkvoice_kws_frontend_s frontend;
  float discarded[BKVOICE_KWS_BINS];
  int ret;

  if (pcm == NULL || features == NULL ||
      warmup_hops > BKVOICE_KWS_RATE / BKVOICE_KWS_HOP * 10 ||
      samples != BKVOICE_KWS_SAMPLES + warmup_hops * BKVOICE_KWS_HOP ||
      count != BKVOICE_KWS_FEATURES)
    {
      return -EINVAL;
    }

  ret = bkvoice_kws_frontend_init_version(&frontend, version);
  if (ret < 0)
    {
      return ret;
    }

  for (size_t i = 0; i < BKVOICE_KWS_ROWS + warmup_hops; i++)
    {
      ret = bkvoice_kws_frontend_frame(&frontend, pcm + i * BKVOICE_KWS_HOP,
        i < warmup_hops ? discarded :
        features + (i - warmup_hops) * BKVOICE_KWS_BINS);
      if (ret < 0)
        {
          break;
        }
    }

  bkvoice_kws_frontend_uninitialize(&frontend);
  return ret;
}
