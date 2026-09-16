/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_kws_frontend.h"

#include <errno.h>
#include <string.h>

#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

int bkvoice_kws_frontend_init(struct bkvoice_kws_frontend_s *frontend)
{
  struct FrontendConfig config;

  if (frontend == NULL)
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
  config.noise_reduction.even_smoothing = 0.0f;
  config.noise_reduction.odd_smoothing = 0.0f;
  config.noise_reduction.min_signal_remaining = 1.0f;
  config.pcan_gain_control.enable_pcan = 0;
  config.log_scale.enable_log = 1;
  config.log_scale.scale_shift = 6;
  if (!FrontendPopulateState(&config, &frontend->state, BKVOICE_KWS_RATE))
    {
      FrontendFreeStateContents(&frontend->state);
      memset(frontend, 0, sizeof(*frontend));
      return -ENOMEM;
    }

  frontend->initialized = 1;
  return 0;
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

  if (frontend == NULL || !frontend->initialized || pcm == NULL ||
      features == NULL)
    {
      return -EINVAL;
    }

  /* The stream layer supplies an already-overlapped 480-sample frame. */
  FrontendReset(&frontend->state);
  output = FrontendProcessSamples(&frontend->state, pcm, BKVOICE_KWS_WINDOW,
                                  &samples_read);
  if (samples_read != BKVOICE_KWS_WINDOW || output.values == NULL ||
      output.size != BKVOICE_KWS_BINS)
    {
      return -EPROTO;
    }

  for (size_t i = 0; i < BKVOICE_KWS_BINS; i++)
    {
      features[i] = (float)output.values[i];
    }

  return 0;
}

int bkvoice_kws_features(const int16_t *pcm, size_t samples,
                         float *features, size_t count)
{
  struct bkvoice_kws_frontend_s frontend;
  int ret;

  if (pcm == NULL || features == NULL || samples != BKVOICE_KWS_SAMPLES ||
      count != BKVOICE_KWS_FEATURES)
    {
      return -EINVAL;
    }

  ret = bkvoice_kws_frontend_init(&frontend);
  if (ret < 0)
    {
      return ret;
    }

  for (size_t i = 0; i < BKVOICE_KWS_ROWS; i++)
    {
      ret = bkvoice_kws_frontend_frame(&frontend, pcm + i * BKVOICE_KWS_HOP,
                                       features + i * BKVOICE_KWS_BINS);
      if (ret < 0)
        {
          break;
        }
    }

  bkvoice_kws_frontend_uninitialize(&frontend);
  return ret;
}
