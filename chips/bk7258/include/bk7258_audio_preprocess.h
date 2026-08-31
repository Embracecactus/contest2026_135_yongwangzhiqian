/****************************************************************************
 * chips/bk7258/include/bk7258_audio_preprocess.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUDIO_PREPROCESS_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUDIO_PREPROCESS_H

#include <stdint.h>

#include <nuttx/compiler.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_AUDIO_PREPROCESS_RATE          16000u
#define BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES 320u
#define BK7258_AUDIO_PREPROCESS_DIAG_MAGIC    0x50504142u /* "BAPP" */
#define BK7258_AUDIO_PREPROCESS_DIAG_VERSION  1u

struct bk7258_audio_preprocess_config_s
{
  uint32_t sample_rate;
  uint32_t frame_samples;
  uint32_t aec_delay_samples;
};

struct bk7258_audio_preprocess_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t ready;
  int32_t last_error;
  uint32_t backend_version;
  uint32_t aec_context_bytes;
  uint32_t frames;
  uint32_t speech_frames;
  uint32_t silence_frames;
  uint32_t process_failures;
  uint32_t last_vad;
};

#if defined(CONFIG_BK7258_AUDIO_PREPROCESS) && \
    defined(CONFIG_BK7258_AP_CORE)
int bk7258_audio_preprocess_initialize(
  FAR const struct bk7258_audio_preprocess_config_s *config);
int bk7258_audio_preprocess_process(FAR const int16_t *interleaved,
                                    uint32_t frames,
                                    FAR int16_t *output);
void bk7258_audio_preprocess_deinitialize(void);
int bk7258_audio_preprocess_get_diag(
  FAR struct bk7258_audio_preprocess_diag_s *diag);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AUDIO_PREPROCESS_H */
