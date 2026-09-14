/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_voice_trigger_model.h"
#include "bk7258_voice_kws.h"
#include "bk7258_voice_kws_model.h"

#include <nuttx/config.h>
#include <media_trigger_model.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define BKVOICE_TRIGGER_RATE 16000u
#define BKVOICE_TRIGGER_SAMPLES 320u
#define BKVOICE_TRIGGER_BYTES (BKVOICE_TRIGGER_SAMPLES * sizeof(int16_t))

struct bkvoice_trigger_model_s
{
  unsigned char *model_bytes;
  void *arena_allocation;
  void *arena;
  struct bkvoice_kws_model_s *model;
  struct bkvoice_kws_s kws;
  struct bkvoice_wake_window_s *window;
  hotword_detection_callback_t callback;
  void *callback_priv;
  uint8_t frame[BKVOICE_TRIGGER_BYTES];
  size_t frame_used;
  size_t model_size;
  uint64_t next_frame_ms;
  uint32_t input_frames;
  uint32_t input_mean_min;
  uint32_t input_mean_max;
  float score;
  bool model_open;
  bool kws_initialized;
  bool bound;
  bool triggered;
  bool error_reported;
};

static pthread_mutex_t g_trigger_lock = PTHREAD_MUTEX_INITIALIZER;
static struct bkvoice_trigger_model_s *g_trigger;

static void trigger_error(struct bkvoice_trigger_model_s *context, int error)
{
  hotword_detection_callback_t callback = NULL;
  void *priv = NULL;

  pthread_mutex_lock(&g_trigger_lock);
  if (g_trigger == context && !context->error_reported)
    {
      context->error_reported = true;
      callback = context->callback;
      priv = context->callback_priv;
    }
  pthread_mutex_unlock(&g_trigger_lock);

  if (callback != NULL)
    {
      callback(priv, 1, error < 0 ? error : -EIO, NULL);
    }
}

void media_trigger_model_get_properties(void *properties, size_t *size)
{
  static const char value[] = "bk7258-kws";
  size_t bytes = sizeof(value);

  if (size == NULL)
    {
      return;
    }
  if (properties != NULL && *size != 0)
    {
      size_t copy = bytes < *size ? bytes : *size;
      memcpy(properties, value, copy);
      if (copy == *size)
        {
          ((char *)properties)[copy - 1] = '\0';
        }
    }
  *size = bytes;
}

void *media_trigger_model_load(const void *data, size_t size,
                               hotword_detection_callback_t callback,
                               void *priv)
{
  struct bkvoice_trigger_model_s *context;
  struct bkvoice_kws_model_spec_s spec;
  struct bkvoice_kws_policy_s policy;
  uintptr_t aligned;
  int ret;

  if (data == NULL || size == 0 || size > BKVOICE_KWS_MODEL_MAX_BYTES)
    {
      return NULL;
    }

  pthread_mutex_lock(&g_trigger_lock);
  if (g_trigger != NULL)
    {
      pthread_mutex_unlock(&g_trigger_lock);
      return NULL;
    }
  context = calloc(1, sizeof(*context));
  if (context != NULL)
    {
      g_trigger = context;
    }
  pthread_mutex_unlock(&g_trigger_lock);
  if (context == NULL)
    {
      return NULL;
    }

  context->model_bytes = malloc(size);
  context->arena_allocation = malloc(CONFIG_BK7258_VOICE_KWS_ARENA_BYTES + 15u);
  if (context->model_bytes == NULL || context->arena_allocation == NULL)
    {
      ret = -ENOMEM;
      goto fail;
    }
  memcpy(context->model_bytes, data, size);
  context->model_size = size;
  aligned = ((uintptr_t)context->arena_allocation + 15u) & ~(uintptr_t)15u;
  context->arena = (void *)aligned;

  memset(&spec, 0, sizeof(spec));
  spec.data = context->model_bytes;
  spec.bytes = context->model_size;
  spec.frontend = BKVOICE_KWS_FRONTEND_ID;
  spec.labels[0] = "silence";
  spec.labels[1] = "unknown";
  spec.labels[2] = BKVOICE_KWS_LABEL;
  ret = bkvoice_kws_model_open(&spec, context->arena,
                               CONFIG_BK7258_VOICE_KWS_ARENA_BYTES,
                               &context->model);
  if (ret < 0)
    {
      goto fail;
    }
  context->model_open = true;
  bkvoice_kws_default_policy(&policy);
  ret = bkvoice_kws_initialize(&context->kws, &policy,
                               bkvoice_kws_model_infer, context->model);
  if (ret < 0)
    {
      goto fail;
    }
  context->kws_initialized = true;
  context->callback = callback;
  context->callback_priv = priv;
  syslog(LOG_INFO, "BKVOICE trigger model bytes=%zu arena_used=%zu\n",
         size, bkvoice_kws_model_arena_used(context->model));
  return context;

fail:
  syslog(LOG_ERR, "BKVOICE trigger model load ret=%d\n", ret);
  media_trigger_model_unload(context);
  return NULL;
}

void media_trigger_model_get_options(void *context, char *options, size_t size)
{
  if (context == NULL || options == NULL || size == 0)
    {
      return;
    }
  (void)snprintf(options, size,
                 "format=s16le:sample_rate=16000:ch_layout=mono");
}

void media_trigger_model_get_buffer_size(void *context, size_t *size)
{
  if (size != NULL)
    {
      *size = context != NULL ? BKVOICE_TRIGGER_BYTES : 0;
    }
}

bool media_trigger_model_detect_hotword(void *opaque, const char *buffer,
                                        size_t size)
{
  struct bkvoice_trigger_model_s *context = opaque;
  bool detected = false;
  int error = 0;

  if (context == NULL || buffer == NULL)
    {
      return false;
    }
  pthread_mutex_lock(&g_trigger_lock);
  if (g_trigger != context || !context->bound || context->triggered)
    {
      pthread_mutex_unlock(&g_trigger_lock);
      return false;
    }
  while (size > 0)
    {
      uint32_t mean;
      size_t copy = BKVOICE_TRIGGER_BYTES - context->frame_used;
      if (copy > size) copy = size;
      memcpy(context->frame + context->frame_used, buffer, copy);
      context->frame_used += copy;
      buffer += copy;
      size -= copy;
      if (context->frame_used != BKVOICE_TRIGGER_BYTES) continue;
      context->frame_used = 0;
      context->next_frame_ms += 20u;
      error = bkvoice_wake_window_observe(context->window,
        (const int16_t *)context->frame, BKVOICE_TRIGGER_SAMPLES,
        context->next_frame_ms);
      if (error < 0) break;
      /* Reuse the endpoint owner's existing PCM magnitude calculation. This
       * bounded health record identifies an empty or low-level capture route
       * without retaining audio or interpreting energy as a wake event.
       */
      mean = context->window->last_mean_abs;
      if (context->input_frames++ == 0)
        {
          context->input_mean_min = mean;
          context->input_mean_max = mean;
        }
      if (mean < context->input_mean_min) context->input_mean_min = mean;
      if (mean > context->input_mean_max) context->input_mean_max = mean;
      if (context->input_frames == 500u)
        {
          syslog(LOG_INFO, "BKVOICE trigger input frames=%lu mean_abs=%lu/%lu "
                 "noise=%lu\n", (unsigned long)context->input_frames,
                 (unsigned long)context->input_mean_min,
                 (unsigned long)context->input_mean_max,
                 (unsigned long)context->window->noise_mean_abs);
          context->input_frames = 0;
        }
      error = bkvoice_kws_feed(&context->kws,
        (const int16_t *)context->frame, BKVOICE_TRIGGER_SAMPLES,
        context->next_frame_ms, &context->score);
      if (error < 0) break;
      if (error == 1)
        {
          error = bkvoice_wake_window_trigger(context->window,
                                               context->next_frame_ms);
          if (error == 0)
            {
              context->triggered = true;
              detected = true;
            }
          break;
        }
    }
  pthread_mutex_unlock(&g_trigger_lock);
  if (error < 0)
    {
      trigger_error(context, error);
    }
  return detected;
}

void media_trigger_model_unload(void *opaque)
{
  struct bkvoice_trigger_model_s *context = opaque;

  if (context == NULL) return;
  pthread_mutex_lock(&g_trigger_lock);
  if (g_trigger == context) g_trigger = NULL;
  pthread_mutex_unlock(&g_trigger_lock);
  if (context->kws_initialized)
    {
      bkvoice_kws_uninitialize(&context->kws);
    }
  if (context->model_open)
    {
      bkvoice_kws_model_close(context->model);
    }
  if (context->arena_allocation != NULL)
    {
      memset(context->arena_allocation, 0,
             CONFIG_BK7258_VOICE_KWS_ARENA_BYTES + 15u);
      free(context->arena_allocation);
    }
  if (context->model_bytes != NULL)
    {
      memset(context->model_bytes, 0, context->model_size);
      free(context->model_bytes);
    }
  memset(context, 0, sizeof(*context));
  free(context);
}

int media_trigger_model_get_poll_fd(void *context)
{
  (void)context;
  return -1;
}

int media_trigger_model_poll_available(void *context)
{
  return context == NULL ? -EINVAL : -ENOSYS;
}

int bkvoice_trigger_model_bind(struct bkvoice_wake_window_s *window,
                               uint64_t start_ms)
{
  int ret = 0;

  if (window == NULL || start_ms == 0) return -EINVAL;
  pthread_mutex_lock(&g_trigger_lock);
  if (g_trigger == NULL || g_trigger->bound) ret = -EBUSY;
  else
    {
      bkvoice_kws_pause(&g_trigger->kws);
      bkvoice_wake_window_reset(window);
      g_trigger->window = window;
      g_trigger->next_frame_ms = start_ms;
      g_trigger->frame_used = 0;
      g_trigger->input_frames = 0;
      g_trigger->score = 0.0f;
      g_trigger->triggered = false;
      g_trigger->error_reported = false;
      g_trigger->bound = true;
    }
  pthread_mutex_unlock(&g_trigger_lock);
  return ret;
}

int bkvoice_trigger_model_unbind(void)
{
  pthread_mutex_lock(&g_trigger_lock);
  if (g_trigger == NULL || !g_trigger->bound)
    {
      pthread_mutex_unlock(&g_trigger_lock);
      return -EINVAL;
    }
  g_trigger->bound = false;
  g_trigger->window = NULL;
  g_trigger->frame_used = 0;
  pthread_mutex_unlock(&g_trigger_lock);
  return 0;
}

float bkvoice_trigger_model_score(void)
{
  float score = 0.0f;
  pthread_mutex_lock(&g_trigger_lock);
  if (g_trigger != NULL) score = g_trigger->score;
  pthread_mutex_unlock(&g_trigger_lock);
  return score;
}
