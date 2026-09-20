/****************************************************************************
 * app/bk7258/bk7258_agent_trigger.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * TFLM model backend for the official Media Trigger contract.
 * Reuses this repository's validated tensor/frontend adapter and frozen
 * score policy. It has no recorder, VAD, conversation or product owner.
 * Media serializes load/detect/unload on its trigger worker.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_voice_kws.h"
#include "bk7258_voice_kws_model.h"
#include "bk7258_voice_wake_package.h"
#include "bk7258_voice_media.h"
#include "bk7258_control_session.h"

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
#include "voice/audio_playback.h"
#include "voice/voice_channel.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKVOICE_TRIGGER_RATE 16000u
#define BKVOICE_TRIGGER_SAMPLES 320u
#define BKVOICE_TRIGGER_BYTES (BKVOICE_TRIGGER_SAMPLES * sizeof(int16_t))

/****************************************************************************
 * Private Types
 ****************************************************************************/

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
static atomic_bool g_model_stream_reset_pending;
static atomic_uint g_wake_threshold_percent = 60;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

unsigned int bk7258_agent_trigger_threshold_get(void)
{
  return atomic_load(&g_wake_threshold_percent);
}

int bk7258_agent_trigger_threshold_set(unsigned int percent)
{
  if (percent < 50 || percent > 90) return -ERANGE;
  atomic_store(&g_wake_threshold_percent, percent);
  return 0;
}

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
  struct bkvoice_wake_package_s package;

  /* Media carries the existing asset envelope so the model backend validates
   * the same label and bytes that the product reports to the App.
   */

  if (bkvoice_wake_package_decode(data, size, &package) != 0)
    {
      return NULL;
    }

  uint8_t hash[32];
  if (mbedtls_sha256(package.model, package.model_size, hash, 0) != 0 ||
      memcmp(hash, package.sha256, sizeof(hash))) return NULL;
  data = package.model;
  size = package.model_size;

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
  context->arena_allocation =
    malloc(CONFIG_BK7258_VOICE_KWS_ARENA_BYTES + 15u);
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
  spec.labels[2] = package.label;
  ret = bkvoice_kws_model_open(&spec, context->arena,
                               CONFIG_BK7258_VOICE_KWS_ARENA_BYTES,
                               &context->model);
  if (ret < 0)
    {
      goto fail;
    }

  context->model_open = true;
  bkvoice_kws_default_policy(&policy);
  policy.threshold = bk7258_agent_trigger_threshold_get() / 100.0f;
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

static int trigger_model_reset(void *opaque)
{
  struct bkvoice_trigger_model_s *context = opaque;
  int ret;

  if (context == NULL || g_trigger != context ||
      !context->kws_initialized || !context->model_open)
    {
      return -EINVAL;
    }

  /* Called serially by the Media worker; a new audio stream must not
   * inherit the previous window or trigger latch. The TFLM arena, model
   * and front-end allocations are kept, and the score threshold and
   * consecutive-frame policy are unchanged.
   */

  bkvoice_kws_pause(&context->kws);

  /* A new stream has no audio from the past three seconds, so the same
   * front end is used to generate silent history. If rows=0 waited for
   * the window to fill, a short word spoken right after the announcement
   * would slide out
   * of a valid position before the first inference and could not satisfy the
   * model's original two-consecutive-frame condition. Only the missing
   * history is filled in here; later input, the score threshold and events
   * still come entirely from the normal MIC detection path.
   */

  ret = bkvoice_kws_frontend_frame(&context->kws.frontend,
                                   context->kws.pcm,
                                   context->kws.features);
  if (ret < 0)
    {
      return ret;
    }

  for (unsigned int row = 1; row < BKVOICE_KWS_ROWS; row++)
    {
      memcpy(context->kws.features + row * BKVOICE_KWS_BINS,
             context->kws.features, BKVOICE_KWS_BINS * sizeof(float));
    }

  context->kws.rows = BKVOICE_KWS_ROWS;
  context->kws.pending = BKVOICE_KWS_WINDOW - BKVOICE_KWS_HOP;
  context->kws.last_ms = 0;
  context->kws.triggered_ms = 0;
  context->kws.timestamp_valid = false;
  context->kws.armed = true;
  context->frame_used = 0;
  context->next_frame_ms = 0;
  context->input_frames = 0;
  context->score = 0;
  context->error_reported = false;
  syslog(LOG_INFO, "BKVOICE Trigger model stream reset\n");
  return 0;
}

void media_trigger_model_get_options(void *context, char *options,
                                     size_t size)
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

  if (g_trigger != context)
    {
          return false;
    }

  /* The control thread only marks the new stream; features, latch and
   * framing state stay owned exclusively by the Media worker. The next
   * start is
   * requested only after an existing stop has returned, so a window being
   * inferred is never cleaned up concurrently.
   */

  if (atomic_exchange(&g_model_stream_reset_pending, false))
    {
      error = trigger_model_reset(context);
      if (error < 0)
        {
          trigger_error(context, error);
          return false;
        }
    }

  if (context->error_reported)
    {
      return false;
    }

  /* The control thread only publishes the threshold; the Media worker owns
   * detection-state updates and does not rebuild the capture.
   */

  float threshold = bk7258_agent_trigger_threshold_get() / 100.0f;
  if (context->kws.policy.threshold != threshold)
    {
      context->kws.policy.threshold = threshold;
      context->kws.hits = 0;
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
       * and the Agent channel owns speech endpointing and rearming.
       */

      mean = 0;
      for (unsigned int i = 0; i < BKVOICE_TRIGGER_SAMPLES; i++)
        {
          int16_t sample;
          memcpy(&sample, context->frame + i * sizeof(sample),
                 sizeof(sample));
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
          syslog(LOG_INFO, "BKVOICE trigger input frames=%lu "
                 "mean_abs=%lu/%lu "
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

/* Serialized by the product worker. Callback threads only publish a wake
 * for the currently armed Media handle; no conversational state lives here.
 */

static struct
{
  void *handle;
  bool loaded;
  bool recognizing;
  bool policy_active;
  bool capture_format_known;
  bool selection_known;
  bool uncertain;
  uint64_t revision;
  atomic_uint generation;
  atomic_bool accepting;
  atomic_bool turn_pending;
  atomic_int callback_error;
  struct bkvoice_wake_package_descriptor_s active;
  struct bkvoice_wake_package_descriptor_s previous;
  size_t active_size;
  size_t previous_size;
  uint8_t *pending_record;
  size_t pending_size;
  uint32_t pending_kind;
  int error;
} g_agent_trigger;

extern void bk7258_agent_product_wake(void);

static void wire_u32(uint8_t *p, uint32_t n)
{
  p[0] = n >> 24;
  p[1] = n >> 16;
  p[2] = n >> 8;
  p[3] = n;
}

static void wire_descriptor(uint8_t *p,
  const struct bkvoice_wake_package_descriptor_s *d, size_t size)
{
  memset(p, 0, BKVOICE_WAKE_PACKAGE_HEADER);
  if (!d->model_path[0] || !size) return;
  memcpy(p, "WKM1", 4);
  wire_u32(p + 4, size);
  for (size_t i = 0; i < 32; i++)
    {
      unsigned int value = 0;
      (void)sscanf(d->sha256_hex + 2 * i, "%2x", &value);
      p[8 + i] = value;
    }

  memcpy(p + 40, d->label, sizeof(d->label));
  memcpy(p + 72, d->phrase, sizeof(d->phrase));
}

static size_t model_file_size(
  const struct bkvoice_wake_package_descriptor_s *d)
{
  struct stat st;
  if (!d->model_path[0] || lstat(d->model_path, &st) ||
      !S_ISREG(st.st_mode) || st.st_size <= 0 ||
      st.st_size > BKVOICE_KWS_MODEL_MAX_BYTES) return 0;
  return st.st_size;
}

static void trigger_event(void *cookie, int event, int result,
                          const char *extra)
{
  (void)extra;
  if ((uintptr_t)cookie != atomic_load(&g_agent_trigger.generation) ||
      !atomic_exchange(&g_agent_trigger.accepting, false)) return;
  if (event != 0 || result != 0)
    atomic_store(&g_agent_trigger.callback_error,
                 result < 0 ? result : -EIO);
  atomic_store(&g_agent_trigger.turn_pending, true);
  bk7258_agent_product_wake();
}

static int trigger_pause(void)
{
  atomic_store(&g_agent_trigger.accepting, false);
  /* Let the Trigger finish its recorder stop/close before the Hotword route
   * is removed. If the route were removed first, the hotword callback racing
   * with stop would close a recorder that is still in recv; pcm0c would then
   * only see COMPLETE and enter a repeated open/close loop.
   */

  if (g_agent_trigger.recognizing)
    {
      int ret = media_trigger_stop_recognition(g_agent_trigger.handle);
      if (ret < 0) return ret;
      g_agent_trigger.recognizing = false;
    }

  if (g_agent_trigger.policy_active)
    {
      int ret = bkvoice_media_source_set_active(MEDIA_SOURCE_HOTWORD, false);
      if (ret < 0) return ret;
      g_agent_trigger.policy_active = false;
    }

  return 0;
}

int bk7258_agent_trigger_stop(void)
{
  int ret = trigger_pause();
  if (ret < 0) return ret;
  atomic_store(&g_agent_trigger.turn_pending, false);
  if (g_agent_trigger.loaded)
    {
      ret = media_trigger_unload_sound_model(g_agent_trigger.handle);
      if (ret < 0) return ret;
      g_agent_trigger.loaded = false;
      g_agent_trigger.active_size = 0;
    }

  if (g_agent_trigger.handle)
    {
      ret = media_trigger_close(g_agent_trigger.handle);
      if (ret < 0) return ret;
      g_agent_trigger.handle = NULL;
    }

  return 0;
}

bool bk7258_agent_trigger_armed(void)
{
  return g_agent_trigger.handle && g_agent_trigger.loaded &&
         g_agent_trigger.recognizing && g_agent_trigger.policy_active &&
         !g_agent_trigger.uncertain &&
         atomic_load(&g_agent_trigger.accepting);
}

int bk7258_agent_trigger_rearm(void)
{
  int cleanup;
  int ret;

  if (!g_agent_trigger.handle || !g_agent_trigger.loaded ||
      g_agent_trigger.uncertain)
    {
      return -EBUSY;
    }

  if (g_agent_trigger.recognizing)
    {
      return g_agent_trigger.policy_active &&
             atomic_load(&g_agent_trigger.accepting) ? 0 : -EBUSY;
    }

  cleanup = audio_playback_cleanup(100);
  if (cleanup < 0)
    {
      return cleanup;
    }

  /* On a cold start the Trigger recorder establishes the format first
   * and the route is applied afterwards; once the 16 kHz format exists,
   * the route is restored first to drain the previous stop/complete
   * before a new recorder is started.
   */

  ret = g_agent_trigger.capture_format_known ?
    bkvoice_media_source_set_active(MEDIA_SOURCE_HOTWORD, true) :
    bkvoice_media_source_stage_active(MEDIA_SOURCE_HOTWORD);
  if (!ret)
    {
      g_agent_trigger.policy_active = true;
      ret = media_trigger_start_recognition(g_agent_trigger.handle);
      if (!ret)
        {
          g_agent_trigger.recognizing = true;
          if (!g_agent_trigger.capture_format_known)
            {
              g_agent_trigger.capture_format_known = true;
              ret = bkvoice_media_source_apply_active();
            }
        }

      if (!ret)
        {
          atomic_store(&g_agent_trigger.callback_error, 0);
          atomic_store(&g_agent_trigger.accepting, true);
          atomic_store(&g_model_stream_reset_pending, true);
        }
    }

  if (ret)
    {
      atomic_store(&g_agent_trigger.accepting, false);
      (void)trigger_pause();
    }

  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE official Trigger rearm ret=%d\n", ret);
  return ret;
}

static int trigger_open_model(
  const struct bkvoice_wake_package_descriptor_s *selected, bool arm)
{
  size_t size = model_file_size(selected);
  size_t used = 0;
  uint8_t *record;

  if (!size)
    {
      return -EBADMSG;
    }

  record = malloc(BKVOICE_WAKE_PACKAGE_HEADER + size);
  if (!record)
    {
      return -ENOMEM;
    }

  wire_descriptor(record, selected, size);
  int fd = open(selected->model_path, O_RDONLY | O_NOFOLLOW);
  int ret = fd < 0 ? -errno : 0;
  while (!ret && used < size)
    {
      ssize_t n = read(fd, record + BKVOICE_WAKE_PACKAGE_HEADER + used,
                       size - used);
      if (n < 0 && errno == EINTR)
        {
          continue;
        }

      if (n <= 0)
        {
          ret = n < 0 ? -errno : -EIO;
          break;
        }

      used += n;
    }

  if (fd >= 0 && close(fd) < 0 && !ret) ret = -errno;
  uint8_t hash[32];
  if (!ret && (mbedtls_sha256(record + BKVOICE_WAKE_PACKAGE_HEADER, size,
                              hash, 0) ||
               memcmp(hash, record + 8, 32)))
    {
      ret = -EKEYREJECTED;
    }

  if (!ret)
    {
      g_agent_trigger.handle = media_trigger_open("default");
      if (!g_agent_trigger.handle) ret = -ENODEV;
    }

  if (!ret)
    {
      unsigned int generation =
        atomic_fetch_add(&g_agent_trigger.generation, 1) + 1;
      ret = media_trigger_set_event_callback(
        g_agent_trigger.handle, (void *)(uintptr_t)generation,
        trigger_event);
    }

  if (!ret)
    ret = media_trigger_load_sound_model(g_agent_trigger.handle, record,
                                         BKVOICE_WAKE_PACKAGE_HEADER + size);
  memset(record, 0, BKVOICE_WAKE_PACKAGE_HEADER + size);
  free(record);
  if (!ret)
    {
      g_agent_trigger.loaded = true;
      g_agent_trigger.active = *selected;
      g_agent_trigger.active_size = size;
      if (arm) ret = bk7258_agent_trigger_rearm();
    }

  if (ret)
    {
      (void)bk7258_agent_trigger_stop();
      return ret;
    }

  syslog(LOG_INFO,
         "BKVOICE official Trigger active label=%s sha256=%s bytes=%zu\n",
         selected->label, selected->sha256_hex, size);
  return 0;
}

static int trigger_load_selected(bool arm)
{
  struct bkvoice_wake_package_descriptor_s selected =
    {
      0
    };

  struct bkvoice_wake_package_descriptor_s previous =
    {
      0
    };

  uint64_t revision = 0;
  int ret = bkvoice_wake_package_load(&selected, &previous, &revision);
  if (ret == -ENOENT)
    {
      snprintf(selected.model_path, sizeof(selected.model_path), "%s",
               CONFIG_BK7258_VOICE_KWS_MODEL_PATH);
      snprintf(selected.sha256_hex, sizeof(selected.sha256_hex), "%s",
               CONFIG_BK7258_VOICE_KWS_MODEL_SHA256);
      snprintf(selected.label, sizeof(selected.label), "%s",
               BKVOICE_KWS_LABEL);
      snprintf(selected.phrase, sizeof(selected.phrase), "%s",
               "你好，openvela");
      syslog(LOG_INFO,
             "BKVOICE model selection=builtin "
             "reason=no-persistent-selection\n");
    }
  else if (ret)
    {
      g_agent_trigger.error = ret;
      return ret;
    }

  g_agent_trigger.revision = revision;
  g_agent_trigger.previous = previous;
  g_agent_trigger.previous_size = model_file_size(&previous);
  g_agent_trigger.selection_known = true;
  ret = trigger_open_model(&selected, arm);
  g_agent_trigger.error = ret;
  return ret;
}

int bk7258_agent_trigger_prepare(void)
{
  if (g_agent_trigger.handle && g_agent_trigger.loaded)
    {
      return 0;
    }

  return trigger_load_selected(false);
}

int bk7258_agent_trigger_start(void)
{
  if (g_agent_trigger.handle && g_agent_trigger.loaded)
    {
      return bk7258_agent_trigger_rearm();
    }

  return trigger_load_selected(true);
}

/* Existing asset transaction only: no audio turn, history or retry
 * scheduler. Old and candidate inference arenas never need to coexist.
 */

bool bk7258_agent_trigger_model_pending(void)
{
  return g_agent_trigger.pending_record != NULL;
}

int bk7258_agent_trigger_model_step(bool arm)
{
  struct bkvoice_wake_package_s spec;
  struct bkvoice_wake_package_descriptor_s old = g_agent_trigger.active;
  struct bkvoice_wake_package_descriptor_s desired =
    g_agent_trigger.previous;
  int ret;

  if (!g_agent_trigger.pending_record)
    {
      return 0;
    }

  if (g_agent_trigger.handle && g_agent_trigger.recognizing &&
      !bk7258_agent_trigger_armed())
    {
      return -EBUSY;
    }

  ret = bk7258_agent_trigger_stop();
  if (!ret && g_agent_trigger.pending_kind == BKCONTROL_CONFIG_WAKE_MODEL)
    {
      ret = bkvoice_wake_package_decode(g_agent_trigger.pending_record,
                                        g_agent_trigger.pending_size, &spec);
      if (!ret) ret = bkvoice_wake_package_validate(&spec);
      if (!ret) ret = bkvoice_wake_package_stage(&spec, &desired);
    }

  if (!ret) ret = trigger_open_model(&desired, arm);

  /* Trial recognition is stopped until the persistent selection is
   * definite.
   */

  if (!ret) ret = trigger_pause();
  if (!ret)
    ret = bkvoice_wake_package_commit(&desired, &old,
                                      g_agent_trigger.revision);
  if (!ret)
    {
      g_agent_trigger.revision++;
      g_agent_trigger.previous = old;
      g_agent_trigger.previous_size = model_file_size(&old);
      if (arm) ret = bk7258_agent_trigger_rearm();
    }
  else
    {
      int stopped = bk7258_agent_trigger_stop();
      if (ret == -EINPROGRESS)
        {
          /* Do not overwrite an uncertain CAS publication or advertise
           * either
           * model as usable. Reboot reloads the existing journal safely.
           */

          g_agent_trigger.uncertain = true;
        }
      else if (!stopped)
        {
          int restored = trigger_open_model(&old, arm);
          if (restored) ret = restored;
        }
      else
        {
          ret = stopped;
        }
    }

  memset(g_agent_trigger.pending_record, 0, g_agent_trigger.pending_size);
  free(g_agent_trigger.pending_record);
  g_agent_trigger.pending_record = NULL;
  g_agent_trigger.pending_size = 0;
  g_agent_trigger.error = ret;
  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE model apply result=%d loaded=%d\n",
         ret, g_agent_trigger.loaded);
  return ret;
}

int bk7258_agent_trigger_control(void *context,
  enum bkcontrol_command_e command, uint32_t kind, uint32_t offset,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  (void)context;
  if (kind != BKCONTROL_CONFIG_WAKE_MODEL &&
      kind != BKCONTROL_CONFIG_WAKE_RESTORE)
    {
      return -ENOTSUP;
    }

  if (command == BKCONTROL_CONFIG_READ)
    {
      uint8_t wire[12 + 2 * BKVOICE_WAKE_PACKAGE_HEADER] =
        {
          'W', 'K', 'S', '1'
        };

      size_t count;

      if (!g_agent_trigger.selection_known || !g_agent_trigger.active_size)
        {
          return g_agent_trigger.error ? g_agent_trigger.error : -EAGAIN;
        }

      if (kind != BKCONTROL_CONFIG_WAKE_MODEL || offset >= sizeof(wire) ||
          (offset & 15u))
        {
          return -ERANGE;
        }

      wire_u32(wire + 4, g_agent_trigger.pending_record != NULL);
      wire_u32(wire + 8, (uint32_t)g_agent_trigger.error);
      wire_descriptor(wire + 12, &g_agent_trigger.active,
                      g_agent_trigger.active_size);
      wire_descriptor(wire + 12 + BKVOICE_WAKE_PACKAGE_HEADER,
                      &g_agent_trigger.previous,
                      g_agent_trigger.previous_size);
      status->config_total = sizeof(wire);
      memset(status->config_chunk, 0, sizeof(status->config_chunk));
      count = sizeof(wire) - offset;
      if (count > sizeof(status->config_chunk))
        {
          count = sizeof(status->config_chunk);
        }

      memcpy(status->config_chunk, wire + offset, count);
      return 0;
    }

  if (command != BKCONTROL_CONFIG_BEGIN && command != BKCONTROL_CONFIG_APPLY)
    {
      return -EINVAL;
    }

  if (!g_agent_trigger.selection_known)
    {
      return -EAGAIN;
    }

  if (g_agent_trigger.uncertain)
    {
      return -EINPROGRESS;
    }

  if ((g_agent_trigger.recognizing && !bk7258_agent_trigger_armed()) ||
      g_agent_trigger.pending_record)
    {
      return -EBUSY;
    }

  if ((kind == BKCONTROL_CONFIG_WAKE_MODEL &&
      (size <= BKVOICE_WAKE_PACKAGE_HEADER ||
       size > BKCONTROL_CONFIG_RECORD_MAX)) ||
      (kind == BKCONTROL_CONFIG_WAKE_RESTORE && size != 4))
    {
      return -EMSGSIZE;
    }

  if (kind == BKCONTROL_CONFIG_WAKE_RESTORE &&
      !g_agent_trigger.previous_size)
    {
      return -ENOENT;
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return 0;
    }

  if (!record)
    {
      return -EINVAL;
    }

  if (kind == BKCONTROL_CONFIG_WAKE_RESTORE && memcmp(record, "WKR1", 4))
    {
      return -EBADMSG;
    }

  if (kind == BKCONTROL_CONFIG_WAKE_MODEL)
    {
      struct bkvoice_wake_package_s spec;
      int ret = bkvoice_wake_package_decode(record, size, &spec);
      if (ret)
        {
          return ret;
        }
    }

  g_agent_trigger.pending_record = malloc(size);
  if (!g_agent_trigger.pending_record) return -ENOMEM;
  memcpy(g_agent_trigger.pending_record, record, size);
  g_agent_trigger.pending_size = size;
  g_agent_trigger.pending_kind = kind;
  g_agent_trigger.error = 0;
  return 0;
}

/* Plays the local acknowledgement PCM on the first wake only; it follows the
 * Agent's drain and release contract and starts no ASR turn.
 */

static int trigger_reply(void)
{
  audio_playback_t *player;
  uint8_t pcm[640];
  ssize_t size;
  int closed;
  int ret;
  int fd = open(CONFIG_MEDIA_SERVER_CONFIG_PATH "/wake_reply.pcm", O_RDONLY);

  if (fd < 0)
    {
      return -errno;
    }

  player = audio_playback_open(NULL, 16000, 1, 16);
  ret = player ? 0 : -(errno ? errno : EIO);
  while (player && ret >= 0)
    {
      size = read(fd, pcm, sizeof(pcm));
      if (size < 0 && errno == EINTR)
        {
          continue;
        }

      if (size < 0)
        {
          ret = -errno;
          break;
        }

      if (size == 0)
        {
          ret = audio_playback_drain(player, 2000);
          break;
        }

      if (size % 2)
        {
          ret = -EINVAL;
          break;
        }

      ret = audio_playback_write(player, pcm, size);
      if (ret >= 0 && ret != size) ret = -EIO;
    }

  close(fd);
  closed = audio_playback_close(player);
  return closed < 0 ? closed : ret;
}

int bk7258_agent_trigger_process(void)
{
  int callback_error;
  int ret;

  if (!atomic_exchange(&g_agent_trigger.turn_pending, false))
    {
      return 0;
    }

  ret = trigger_pause();
  callback_error = atomic_exchange(&g_agent_trigger.callback_error, 0);
  if (!ret && callback_error) ret = callback_error;
  if (!ret)
    {
      int reply = trigger_reply();
      syslog(reply ? LOG_WARNING : LOG_INFO,
             "BKVOICE wake reply result=%d\n", reply);
      /* A missing prompt or a failed playback does not cancel the
       * interaction, but the player must first be confirmed released.
       */

      ret = audio_playback_cleanup(100);
    }

  if (!ret) ret = voice_channel_start_auto();
  if (ret < 0)
    {
      syslog(LOG_WARNING, "BKVOICE official Trigger turn start failed=%d\n",
             ret);
      if (!callback_error)
        {
          (void)bk7258_agent_trigger_rearm();
        }
    }

  return ret;
}
