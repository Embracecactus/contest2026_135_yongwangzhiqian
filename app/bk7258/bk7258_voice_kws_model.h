/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __APP_BK7258_BK7258_VOICE_KWS_MODEL_H
#define __APP_BK7258_BK7258_VOICE_KWS_MODEL_H

#include "bk7258_voice_kws.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define BKVOICE_KWS_LABEL "nihao_openvela"
#define BKVOICE_KWS_MODEL_MAX_BYTES (1024 * 1024)

struct bkvoice_kws_model_s;

struct bkvoice_kws_model_spec_s
{
  const unsigned char *data;
  size_t bytes;
  const char *frontend;
  const char *labels[BKVOICE_KWS_CLASSES];
};

/* The caller owns immutable, already authenticated model bytes and a 16-byte
 * aligned arena until close. This runner validates the model/tensor contract;
 * it does not authenticate downloaded assets or grant MIC access. All calls
 * are serialized by one owner. No network, file or peripheral API is used.
 */

int bkvoice_kws_model_open(const struct bkvoice_kws_model_spec_s *spec,
                           void *arena, size_t arena_bytes,
                           struct bkvoice_kws_model_s **model);
int bkvoice_kws_model_infer(void *context, const float *features,
                           float scores[BKVOICE_KWS_CLASSES]);
size_t bkvoice_kws_model_arena_used(const struct bkvoice_kws_model_s *model);
void bkvoice_kws_model_close(struct bkvoice_kws_model_s *model);

#ifdef __cplusplus
}
#endif
#endif
