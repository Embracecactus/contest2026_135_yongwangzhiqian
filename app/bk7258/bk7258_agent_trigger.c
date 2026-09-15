/* SPDX-License-Identifier: Apache-2.0 */
/* TFLM model backend for the official Media Trigger contract.
 * Reuses this repository's validated tensor/frontend adapter and frozen
 * score policy. It has no recorder, VAD, conversation or product owner.
 * Media serializes load/detect/unload on its trigger worker. */
#include "bk7258_voice_kws.h"
#include "bk7258_voice_kws_model.h"

#include <nuttx/config.h>
#include <media_trigger.h>
#include <media_trigger_model.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <mbedtls/sha256.h>
#include "voice/voice_channel.h"
#include "agent_compat.h"

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
  bool error_reported;
};

static struct bkvoice_trigger_model_s *g_trigger;

static void trigger_error(struct bkvoice_trigger_model_s *context, int error)
{
  hotword_detection_callback_t callback = NULL;
  void *priv = NULL;

  if (g_trigger == context && !context->error_reported)
    {
      context->error_reported = true;
      callback = context->callback;
      priv = context->callback_priv;
    }

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

  if (g_trigger != NULL)
    {
          return NULL;
    }
  context = calloc(1, sizeof(*context));
  if (context != NULL)
    {
      g_trigger = context;
    }
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
  context->next_frame_ms = 0;
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
  if (g_trigger != context || context->error_reported)
    {
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
      /* Only model input diagnostics live here. Media owns the recorder,
       * and the Agent channel owns speech endpointing and rearming. */
      mean = 0;
      for (unsigned int i = 0; i < BKVOICE_TRIGGER_SAMPLES; i++)
        {
          int16_t sample;
          memcpy(&sample, context->frame + i * sizeof(sample), sizeof(sample));
          mean += sample < 0 ? -(int32_t)sample : sample;
        }
      mean /= BKVOICE_TRIGGER_SAMPLES;
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
                 "\n", (unsigned long)context->input_frames,
                 (unsigned long)context->input_mean_min,
                 (unsigned long)context->input_mean_max);
          context->input_frames = 0;
        }
      error = bkvoice_kws_feed(&context->kws,
        (const int16_t *)context->frame, BKVOICE_TRIGGER_SAMPLES,
        context->next_frame_ms, &context->score);
      if (error < 0) break;
      if (error == 1)
        {
          detected = true;
          break;
        }
    }
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
  if (g_trigger == context) g_trigger = NULL;
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

struct bk7258_agent_trigger_s
{
  void *handle;
  unsigned char *model;
  size_t model_size;
  atomic_bool turn_pending;
};

static struct bk7258_agent_trigger_s g_agent_trigger;

static int trigger_load_model(void)
{
  struct stat st;
  uint8_t hash[32];
  char hex[65];
  mbedtls_sha256_context sha;
  size_t used = 0;
  int fd = open(CONFIG_BK7258_VOICE_KWS_MODEL_PATH, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return -errno;
  if (fstat(fd, &st) < 0 || st.st_size <= 0 ||
      st.st_size > BKVOICE_KWS_MODEL_MAX_BYTES)
    { close(fd); return -EBADMSG; }
  g_agent_trigger.model = malloc((size_t)st.st_size);
  if (!g_agent_trigger.model) { close(fd); return -ENOMEM; }
  while (used < (size_t)st.st_size)
    {
      ssize_t n = read(fd, g_agent_trigger.model + used,
                       (size_t)st.st_size - used);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { close(fd); return n < 0 ? -errno : -EIO; }
      used += (size_t)n;
    }
  close(fd);
  g_agent_trigger.model_size = used;
  mbedtls_sha256_init(&sha);
  if (mbedtls_sha256_starts(&sha, 0) ||
      mbedtls_sha256_update(&sha, g_agent_trigger.model, used) ||
      mbedtls_sha256_finish(&sha, hash))
    { mbedtls_sha256_free(&sha); return -EIO; }
  mbedtls_sha256_free(&sha);
  for (unsigned int i = 0; i < sizeof(hash); i++)
    snprintf(hex + i * 2, 3, "%02x", hash[i]);
  hex[64] = '\0';
  if (strcmp(hex, CONFIG_BK7258_VOICE_KWS_MODEL_SHA256) != 0)
    return -EKEYREJECTED;
  return 0;
}

static void trigger_voice_event(int event, int result)
{
  if (!g_agent_trigger.handle) return;
  if (event == VOICE_CHANNEL_EVENT_CAPTURE_COMPLETE && result < 0)
    {
      int ret = media_trigger_start_recognition(g_agent_trigger.handle);
      syslog(LOG_WARNING,
             "BKVOICE official Trigger rearm after capture failure=%d ret=%d\n",
             result, ret);
      return;
    }
  if (event != VOICE_CHANNEL_EVENT_TTS_COMPLETE) return;
  int ret = media_trigger_start_recognition(g_agent_trigger.handle);
  syslog(result == 0 ? LOG_INFO : LOG_WARNING,
         "BKVOICE official Trigger rearm after Media TTS result=%d ret=%d\n",
         result, ret);
}

static int trigger_turn_task(int argc, FAR char *argv[])
{
  int ret;
  (void)argc;
  (void)argv;
  ret = media_trigger_stop_recognition(g_agent_trigger.handle);
  if (ret == 0) ret = voice_channel_start_auto();
  if (ret < 0)
    {
      syslog(LOG_WARNING, "BKVOICE official Trigger turn start failed: %d\n", ret);
      (void)media_trigger_start_recognition(g_agent_trigger.handle);
    }
  atomic_store(&g_agent_trigger.turn_pending, false);
  return ret;
}

static void *trigger_turn_worker(void *arg)
{
  (void)arg;
  (void)trigger_turn_task(0, NULL);
  return NULL;
}

static void trigger_event(void *cookie, int event, int result, const char *extra)
{
  (void)cookie;
  (void)extra;
  if (event == 0 && result == 0 &&
      !atomic_exchange(&g_agent_trigger.turn_pending, true))
    {
      int ret = agent_task_create(trigger_turn_worker, "voice_trigger_turn",
                                  8192, NULL, 90);
      if (ret != OK)
        {
          atomic_store(&g_agent_trigger.turn_pending, false);
          syslog(LOG_ERR, "BKVOICE official Trigger turn task failed=%d\n", ret);
        }
    }
}

int bk7258_agent_trigger_start(void)
{
  int ret;
  if (g_agent_trigger.handle) return 0;
  ret = voice_channel_set_event_callback(trigger_voice_event);
  if (ret < 0) return ret;
  ret = trigger_load_model();
  if (ret < 0) goto fail;
  g_agent_trigger.handle = media_trigger_open("default");
  if (!g_agent_trigger.handle) { ret = -ENODEV; goto fail; }
  ret = media_trigger_set_event_callback(g_agent_trigger.handle,
                                         &g_agent_trigger, trigger_event);
  if (ret < 0) goto fail;
  ret = media_trigger_load_sound_model(g_agent_trigger.handle,
                                       g_agent_trigger.model,
                                       g_agent_trigger.model_size);
  if (ret < 0) goto fail;
  ret = media_trigger_start_recognition(g_agent_trigger.handle);
  if (ret < 0) goto fail;
  syslog(LOG_INFO, "BKVOICE official Trigger active model=%s bytes=%zu\n",
         CONFIG_BK7258_VOICE_KWS_MODEL_SHA256, g_agent_trigger.model_size);
  return 0;
fail:
  if (g_agent_trigger.handle)
    {
      (void)media_trigger_close(g_agent_trigger.handle);
      g_agent_trigger.handle = NULL;
    }
  free(g_agent_trigger.model);
  g_agent_trigger.model = NULL;
  g_agent_trigger.model_size = 0;
  return ret;
}
