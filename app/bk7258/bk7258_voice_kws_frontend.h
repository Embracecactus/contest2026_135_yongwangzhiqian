/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __APP_BK7258_BK7258_VOICE_KWS_FRONTEND_H
#define __APP_BK7258_BK7258_VOICE_KWS_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define BKVOICE_KWS_FRONTEND_ID "bkvoice-microfrontend-v1"
#define BKVOICE_KWS_RATE         16000
#define BKVOICE_KWS_SAMPLES      (3 * BKVOICE_KWS_RATE)
#define BKVOICE_KWS_WINDOW       480
#define BKVOICE_KWS_HOP          320
#define BKVOICE_KWS_BINS         40
#define BKVOICE_KWS_ROWS         \
  (1 + (BKVOICE_KWS_SAMPLES - BKVOICE_KWS_WINDOW) / BKVOICE_KWS_HOP)
#define BKVOICE_KWS_FEATURES     (BKVOICE_KWS_ROWS * BKVOICE_KWS_BINS)

/* TFLM microfrontend, configured for a 30 ms / 20 ms 16 kHz window, 40
 * filters (125--7500 Hz), log scale shift 6, PCAN off, and noise reduction
 * bypassed. The upstream state owns initialization-time allocations; callers
 * must uninitialize a successfully initialized frontend exactly once.
 */

struct bkvoice_kws_frontend_s
{
  struct FrontendState state;
  int initialized;
};

int bkvoice_kws_frontend_init(struct bkvoice_kws_frontend_s *frontend);
void bkvoice_kws_frontend_uninitialize(struct bkvoice_kws_frontend_s *frontend);
int bkvoice_kws_frontend_frame(struct bkvoice_kws_frontend_s *frontend,
                               const int16_t *pcm, float *features);

/* Host training boundary. Exactly three seconds in, 149 x 40 row-major out.
 * Firmware retains a persistent frontend state and does no per-frame
 * allocation. Each frame is independently reset before it is processed so
 * this batch path and the existing 20 ms streaming wrapper agree exactly.
 */

int bkvoice_kws_features(const int16_t *pcm, size_t samples,
                        float *features, size_t count);

#ifdef __cplusplus
}
#endif
#endif
