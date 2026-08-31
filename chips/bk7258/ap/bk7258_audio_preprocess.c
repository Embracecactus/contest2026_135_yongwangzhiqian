/****************************************************************************
 * chips/bk7258/ap/bk7258_audio_preprocess.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-side adapter for the pinned v3.1.1.9 AEC/NS, AGC and VAD libraries.
 * The SDK closure sorts libaec.a before libaec_v3.a; this adapter therefore
 * deliberately targets the stable AEC v1 ABI instead of allowing duplicate
 * public symbols to select a backend implicitly.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_AUDIO_PREPROCESS) && \
    defined(CONFIG_BK7258_AP_CORE)

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_audio_preprocess.h>

#define BK7258_AEC_CTRL_SET_MIC_DELAY 2u
#define BK7258_AGC_MIN_LEVEL          0
#define BK7258_AGC_MAX_LEVEL          255
#define BK7258_AGC_TARGET_DBFS        3
#define BK7258_AGC_COMPRESSION_DB     9
#define BK7258_AGC_LIMITER_ENABLE     1u
#define BK7258_AUDIO_WORK_BUFFERS     4u

struct bk7258_agc_config_sdk_s
{
  int16_t target_level_dbfs;
  int16_t compression_gain_db;
  uint8_t limiter_enable;
};

struct bk7258_audio_preprocess_s
{
  mutex_t lock;
  FAR void *aec;
  FAR void *agc;
  FAR int16_t *work;
  FAR int16_t *near;
  FAR int16_t *reference;
  FAR int16_t *aec_output;
  FAR int16_t *agc_output;
  bool ns_ready;
  bool vad_ready;
  bool ready;
  struct bk7258_audio_preprocess_diag_s diag;
};

extern uint32_t aec_size(uint32_t delay);
extern void aec_init(FAR void *aec, int16_t sample_rate);
extern void aec_ctrl(FAR void *aec, uint32_t command, uint32_t argument);
extern void aec_proc(FAR void *aec, FAR int16_t *reference,
                     FAR int16_t *near, FAR int16_t *output);
extern uint32_t aec_ver(void);

extern int bk_aud_agc_create(FAR void **instance);
extern int bk_aud_agc_free(FAR void *instance);
extern int bk_aud_agc_init(FAR void *instance, int32_t min_level,
                           int32_t max_level, uint32_t sample_rate);
extern int bk_aud_agc_set_config(
  FAR void *instance, struct bk7258_agc_config_sdk_s config);
extern int bk_aud_agc_process(FAR void *instance, FAR const int16_t *input,
                              int16_t samples, FAR int16_t *output);
extern int bk_aud_ns_init(int frame_size_20ms, int sample_rate);
extern int bk_aud_ns_deinit(void);
extern int bk_aud_ns_process(FAR int16_t *input);
extern int bk_aud_vad_init(int frame_size_20ms, int sample_rate);
extern int bk_aud_vad_deinit(void);
extern int bk_aud_vad_process(FAR int16_t *input);

_Static_assert(sizeof(struct bk7258_agc_config_sdk_s) == 6,
               "v3.1.1.9 AGC config ABI changed");

static struct bk7258_audio_preprocess_s g_bk7258_audio_preprocess =
{
  .lock = NXMUTEX_INITIALIZER,
};

static int bk7258_audio_preprocess_sdk_result(int result)
{
  return result == 0 ? OK : (result < 0 ? result : -EIO);
}

static void bk7258_audio_preprocess_reset_diag(
  FAR struct bk7258_audio_preprocess_s *priv)
{
  memset(&priv->diag, 0, sizeof(priv->diag));
  priv->diag.magic = BK7258_AUDIO_PREPROCESS_DIAG_MAGIC;
  priv->diag.version = BK7258_AUDIO_PREPROCESS_DIAG_VERSION;
  priv->diag.size = sizeof(priv->diag);
}

static void bk7258_audio_preprocess_release(
  FAR struct bk7258_audio_preprocess_s *priv)
{
  if (priv->agc != NULL)
    {
      (void)bk_aud_agc_free(priv->agc);
      priv->agc = NULL;
    }

  if (priv->vad_ready)
    {
      (void)bk_aud_vad_deinit();
      priv->vad_ready = false;
    }

  if (priv->ns_ready)
    {
      (void)bk_aud_ns_deinit();
      priv->ns_ready = false;
    }

  if (priv->aec != NULL)
    {
      explicit_bzero(priv->aec, priv->diag.aec_context_bytes);
      kmm_free(priv->aec);
      priv->aec = NULL;
    }

  if (priv->work != NULL)
    {
      explicit_bzero(priv->work,
                     BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES *
                     sizeof(int16_t) * BK7258_AUDIO_WORK_BUFFERS);
      kmm_free(priv->work);
      priv->work = NULL;
    }

  priv->near = NULL;
  priv->reference = NULL;
  priv->aec_output = NULL;
  priv->agc_output = NULL;
  priv->ready = false;
  priv->diag.ready = 0;
}

int bk7258_audio_preprocess_initialize(
  FAR const struct bk7258_audio_preprocess_config_s *config)
{
  FAR struct bk7258_audio_preprocess_s *priv =
    &g_bk7258_audio_preprocess;
  struct bk7258_agc_config_sdk_s agc_config;
  size_t work_bytes;
  uint32_t aec_bytes;
  int ret;

  if (config == NULL ||
      config->sample_rate != BK7258_AUDIO_PREPROCESS_RATE ||
      config->frame_samples != BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES ||
      config->aec_delay_samples > 1000u)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->ready)
    {
      nxmutex_unlock(&priv->lock);
      return -EALREADY;
    }

  bk7258_audio_preprocess_reset_diag(priv);
  aec_bytes = aec_size(config->aec_delay_samples);
  if (aec_bytes < 4096u || aec_bytes > 131072u)
    {
      ret = -EPROTO;
      goto errout;
    }

  priv->diag.aec_context_bytes = aec_bytes;
  priv->aec = kmm_zalloc(aec_bytes);
  work_bytes = BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES *
               sizeof(int16_t) * BK7258_AUDIO_WORK_BUFFERS;
  priv->work = kmm_zalloc(work_bytes);
  if (priv->aec == NULL || priv->work == NULL)
    {
      ret = -ENOMEM;
      goto errout;
    }

  priv->near = priv->work;
  priv->reference = priv->near + BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES;
  priv->aec_output = priv->reference +
                     BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES;
  priv->agc_output = priv->aec_output +
                     BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES;

  aec_init(priv->aec, BK7258_AUDIO_PREPROCESS_RATE);
  aec_ctrl(priv->aec, BK7258_AEC_CTRL_SET_MIC_DELAY,
           config->aec_delay_samples);
  priv->diag.backend_version = aec_ver();

  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_ns_init(BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES,
                   BK7258_AUDIO_PREPROCESS_RATE));
  if (ret < 0)
    {
      goto errout;
    }

  priv->ns_ready = true;

  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_agc_create(&priv->agc));
  if (ret < 0 || priv->agc == NULL)
    {
      ret = ret < 0 ? ret : -ENOMEM;
      goto errout;
    }

  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_agc_init(priv->agc, BK7258_AGC_MIN_LEVEL,
                    BK7258_AGC_MAX_LEVEL,
                    BK7258_AUDIO_PREPROCESS_RATE));
  if (ret < 0)
    {
      goto errout;
    }

  memset(&agc_config, 0, sizeof(agc_config));
  agc_config.target_level_dbfs = BK7258_AGC_TARGET_DBFS;
  agc_config.compression_gain_db = BK7258_AGC_COMPRESSION_DB;
  agc_config.limiter_enable = BK7258_AGC_LIMITER_ENABLE;
  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_agc_set_config(priv->agc, agc_config));
  if (ret < 0)
    {
      goto errout;
    }

  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_vad_init(BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES,
                    BK7258_AUDIO_PREPROCESS_RATE));
  if (ret < 0)
    {
      goto errout;
    }

  priv->vad_ready = true;
  priv->ready = true;
  priv->diag.ready = 1;
  nxmutex_unlock(&priv->lock);
  return OK;

errout:
  priv->diag.last_error = ret;
  bk7258_audio_preprocess_release(priv);
  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_audio_preprocess_process(FAR const int16_t *interleaved,
                                    uint32_t frames,
                                    FAR int16_t *output)
{
  FAR struct bk7258_audio_preprocess_s *priv =
    &g_bk7258_audio_preprocess;
  uint32_t index;
  int vad;
  int ret;

  if (interleaved == NULL || output == NULL ||
      frames != BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->ready)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  for (index = 0; index < frames; index++)
    {
      priv->near[index] = interleaved[index * 2u];
      priv->reference[index] = interleaved[index * 2u + 1u];
    }

  aec_proc(priv->aec, priv->reference, priv->near, priv->aec_output);
  ret = bk_aud_ns_process(priv->aec_output);
  if (ret < 0)
    {
      memcpy(output, priv->near, frames * sizeof(*output));
      priv->diag.last_error = ret;
      priv->diag.process_failures++;
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_agc_process(priv->agc, priv->aec_output, (int16_t)frames,
                       priv->agc_output));
  if (ret < 0)
    {
      memcpy(output, priv->near, frames * sizeof(*output));
      priv->diag.last_error = ret;
      priv->diag.process_failures++;
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  vad = bk_aud_vad_process(priv->agc_output);
  if (vad < 0)
    {
      memcpy(output, priv->near, frames * sizeof(*output));
      priv->diag.last_error = vad;
      priv->diag.process_failures++;
      nxmutex_unlock(&priv->lock);
      return vad;
    }

  memcpy(output, priv->agc_output, frames * sizeof(*output));
  priv->diag.frames++;
  priv->diag.last_vad = vad > 0 ? 1u : 0u;
  if (vad > 0)
    {
      priv->diag.speech_frames++;
    }
  else
    {
      priv->diag.silence_frames++;
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

void bk7258_audio_preprocess_deinitialize(void)
{
  FAR struct bk7258_audio_preprocess_s *priv =
    &g_bk7258_audio_preprocess;

  if (nxmutex_lock(&priv->lock) >= 0)
    {
      bk7258_audio_preprocess_release(priv);
      nxmutex_unlock(&priv->lock);
    }
}

int bk7258_audio_preprocess_get_diag(
  FAR struct bk7258_audio_preprocess_diag_s *diag)
{
  FAR struct bk7258_audio_preprocess_s *priv =
    &g_bk7258_audio_preprocess;
  int ret;

  if (diag == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  memcpy(diag, &priv->diag, sizeof(*diag));
  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif
