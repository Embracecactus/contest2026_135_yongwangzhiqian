/****************************************************************************
 * app/bk7258/bk7258_agent_trigger.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Product wake policy over the Agent's single Media capture owner.
 * The local reader runs detection; product lifecycle joins it before model
 * replacement. A wake hands the same PCM stream to the Agent conversation.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_voice_kws.h"
#include "bk7258_voice_kws_model.h"
#include "bk7258_voice_wake_package.h"
#include "bk7258_voice_media.h"
#include "bk7258_agent_trigger.h"
#include "bk7258_control_session.h"

#include <nuttx/config.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
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
#include "voice/audio_capture.h"
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
  void (*callback)(void *priv, int event, int result, const char *extra);
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
static void trigger_model_unload(void *opaque);

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
  void (*callback)(void *priv, int event, int result,
                   const char *extra) = NULL;
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

static void *trigger_model_load(const void *data, size_t size,
  void (*callback)(void *, int, int, const char *), void *priv)
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
  spec.frontend = bkvoice_wake_package_frontend_id(
    package.frontend_version);
  if (!spec.frontend)
    {
      ret = -EBADMSG;
      goto fail;
    }
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
  ret = bkvoice_kws_initialize_version(&context->kws, &policy,
    bkvoice_kws_model_infer, context->model, package.frontend_version);
  if (ret < 0)
    {
      goto fail;
    }

  context->kws_initialized = true;
  if (bkvoice_kws_model_is_streaming(context->model))
    {
      ret = bkvoice_kws_set_stream(&context->kws, bkvoice_kws_model_step,
                                   bkvoice_kws_model_reset);
      if (ret < 0) goto fail;
    }
  context->callback = callback;
  context->next_frame_ms = 0;
  context->callback_priv = priv;
  syslog(LOG_INFO, "BKVOICE trigger model bytes=%zu arena_used=%zu\n",
         size, bkvoice_kws_model_arena_used(context->model));
  return context;

fail:
  syslog(LOG_ERR, "BKVOICE trigger model load ret=%d\n", ret);
  trigger_model_unload(context);
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

  /* Called serially by the detection worker; a new audio stream must not
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

  if (!bkvoice_kws_model_is_streaming(context->model))
    {
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
    }

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

static bool trigger_model_detect(void *opaque, const char *buffer,
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
   * framing state stay owned exclusively by the detection worker. The next
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

  /* The control thread only publishes the threshold; the detection worker
   * owns detection-state updates and does not rebuild the capture.
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

static void trigger_model_unload(void *opaque)
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

/* Serialized by the product worker. Callback threads only publish a wake
 * for the currently armed model; no conversational state lives here.
 */

static struct
{
  void *handle;
  bool loaded;
  bool recognizing;
  pthread_t reader;
  bool reader_valid;
  audio_capture_t *capture;
  atomic_bool reader_stop;
  bool selection_known;
  bool uncertain;
  uint64_t revision;
  atomic_uint generation;
  atomic_bool accepting;
  atomic_bool turn_pending;
  atomic_bool reply_pending;
  atomic_bool reply_abort;
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

/* Product configuration and event workers share this lifecycle boundary.
 * Detection never takes it: stop joins detection before freeing its model.
 */

static pthread_mutex_t g_trigger_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_reply_lock = PTHREAD_MUTEX_INITIALIZER;
static audio_playback_t *g_reply_player;

extern void bk7258_agent_product_wake(void);

static void wire_u32(uint8_t *p, uint32_t n)
{
  p[0] = n >> 24;
  p[1] = n >> 16;
  p[2] = n >> 8;
  p[3] = n;
}

/* WKS2 descriptors are always 140 bytes, even for a WKM1/v1 model.
 * This status envelope is distinct from the installable WKM1/WKM2 asset. */
static int wire_descriptor(uint8_t *p,
  const struct bkvoice_wake_package_descriptor_s *d, size_t size)
{
  memset(p, 0, BKVOICE_WAKE_PACKAGE_HEADER_V2);
  if (!d->model_path[0] || !size)
    return !d->model_path[0] && !size && !d->frontend_version ?
      0 : -EBADMSG;
  if (!bkvoice_wake_package_frontend_id(d->frontend_version))
    return -EBADMSG;
  memcpy(p, "WKM2", 4);
  wire_u32(p + 4, size);
  for (size_t i = 0; i < 32; i++)
    {
      unsigned int value = 0;
      (void)sscanf(d->sha256_hex + 2 * i, "%2x", &value);
      p[8 + i] = value;
    }

  memcpy(p + 40, d->label, sizeof(d->label));
  memcpy(p + 72, d->phrase, sizeof(d->phrase));
  wire_u32(p + BKVOICE_WAKE_PACKAGE_HEADER, d->frontend_version);
  return 0;
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

static void *trigger_read(void *arg)
{
  uint8_t pcm[BKVOICE_TRIGGER_BYTES];
  uint64_t expected = 0;
  void *cookie = arg;

  while (!atomic_load(&g_agent_trigger.reader_stop))
    {
      uint64_t first = 0;
      int ret = audio_capture_read_local(g_agent_trigger.capture, pcm,
                                         sizeof(pcm), &first);
      if (ret == -EAGAIN || ret == -EINTR) continue;
      if (atomic_load(&g_agent_trigger.reader_stop)) break;
      if (ret <= 0 || first != expected)
        {
          trigger_event(cookie, 1, ret < 0 ? ret : -EPIPE, NULL);
          break;
        }

      expected += ret / sizeof(int16_t);
      if (trigger_model_detect(g_agent_trigger.handle,
                                (const char *)pcm, ret))
        {
          ret = audio_capture_handoff_at(g_agent_trigger.capture, expected);
          trigger_event(cookie, ret < 0 ? 1 : 0, ret, NULL);
          break;
        }
    }

  memset(pcm, 0, sizeof(pcm));
  return NULL;
}

static int trigger_join(void)
{
  atomic_store(&g_agent_trigger.reader_stop, true);
  if (g_agent_trigger.reader_valid)
    {
      int ret = pthread_join(g_agent_trigger.reader, NULL);
      if (ret) return -ret;
      g_agent_trigger.reader_valid = false;
    }

  g_agent_trigger.recognizing = false;
  return 0;
}

static int trigger_pause(void)
{
  atomic_store(&g_agent_trigger.accepting, false);
  int ret = trigger_join();
  if (ret < 0) return ret;
  if (g_agent_trigger.capture)
    {
      ret = audio_capture_close(g_agent_trigger.capture);
      if (ret < 0) return ret;
      g_agent_trigger.capture = NULL;
    }

  return 0;
}

static int trigger_stop_locked(void)
{
  atomic_store(&g_agent_trigger.reply_pending, false);
  int ret = trigger_pause();
  if (ret < 0) return ret;
  atomic_store(&g_agent_trigger.turn_pending, false);
  if (g_agent_trigger.loaded)
    {
      trigger_model_unload(g_agent_trigger.handle);
      g_agent_trigger.loaded = false;
      g_agent_trigger.active_size = 0;
      g_agent_trigger.handle = NULL;
    }

  return 0;
}

static bool trigger_armed_locked(void)
{
  return g_agent_trigger.handle && g_agent_trigger.loaded &&
         g_agent_trigger.recognizing && g_agent_trigger.capture &&
         !g_agent_trigger.uncertain &&
         atomic_load(&g_agent_trigger.accepting);
}

static int trigger_rearm_locked(void)
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
      return atomic_load(&g_agent_trigger.accepting) ? 0 : -EBUSY;
    }

  cleanup = audio_playback_cleanup(100);
  if (cleanup < 0)
    {
      return cleanup;
    }

  atomic_store(&g_agent_trigger.reply_pending, false);

  if (g_agent_trigger.capture)
    {
      ret = trigger_pause();
      if (ret) return ret;
    }

  g_agent_trigger.capture = audio_capture_open_local(NULL,
    BKVOICE_TRIGGER_RATE, 1, 16);
  ret = g_agent_trigger.capture ? 0 : -(errno ? errno : EIO);
  if (!ret) ret = audio_capture_start(g_agent_trigger.capture);
  if (!ret)
    {
      atomic_store(&g_agent_trigger.reader_stop, false);
      atomic_store(&g_agent_trigger.callback_error, 0);
      atomic_store(&g_agent_trigger.accepting, true);
      atomic_store(&g_model_stream_reset_pending, true);
      unsigned int generation = atomic_load(&g_agent_trigger.generation);
      pthread_attr_t attr;
      struct sched_param param =
        {
          .sched_priority = 80
        };

      int created = pthread_attr_init(&attr);
      if (!created)
        {
          created = pthread_attr_setstacksize(&attr, 16384);
          if (!created) created = pthread_attr_setschedparam(&attr, &param);
          if (!created)
            created = pthread_create(&g_agent_trigger.reader, &attr,
              trigger_read, (void *)(uintptr_t)generation);
          pthread_attr_destroy(&attr);
        }

      ret = created ? -created : 0;
      if (!ret)
        {
          g_agent_trigger.reader_valid = true;
          g_agent_trigger.recognizing = true;
        }
    }

  if (ret)
    {
      atomic_store(&g_agent_trigger.accepting, false);
      (void)trigger_pause();
    }

  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE single capture rearm ret=%d\n", ret);
  return ret;
}

static int trigger_open_model(
  const struct bkvoice_wake_package_descriptor_s *selected, bool arm)
{
  size_t size = model_file_size(selected);
  size_t header = bkvoice_wake_package_header_size(
    selected->frontend_version);
  size_t used = 0;
  uint8_t *record;

  if (!size || !header)
    {
      return -EBADMSG;
    }

  record = malloc(header + size);
  if (!record)
    {
      return -ENOMEM;
    }

  int encoded = bkvoice_wake_package_encode_header(record, header,
    selected, size);
  if (encoded != (int)header)
    {
      free(record);
      return encoded < 0 ? encoded : -EBADMSG;
    }
  int fd = open(selected->model_path, O_RDONLY | O_NOFOLLOW);
  int ret = fd < 0 ? -errno : 0;
  while (!ret && used < size)
    {
      ssize_t n = read(fd, record + header + used,
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
  if (!ret && (mbedtls_sha256(record + header, size,
                              hash, 0) ||
               memcmp(hash, record + 8, 32)))
    {
      ret = -EKEYREJECTED;
    }

  if (!ret)
    {
      unsigned int generation =
        atomic_fetch_add(&g_agent_trigger.generation, 1) + 1;
      g_agent_trigger.handle = trigger_model_load(record,
        header + size, trigger_event,
        (void *)(uintptr_t)generation);
      if (!g_agent_trigger.handle) ret = -EINVAL;
    }

  memset(record, 0, header + size);
  free(record);
  if (!ret)
    {
      g_agent_trigger.loaded = true;
      g_agent_trigger.active = *selected;
      g_agent_trigger.active_size = size;
      if (arm) ret = trigger_rearm_locked();
    }

  if (ret)
    {
      (void)trigger_stop_locked();
      return ret;
    }

  syslog(LOG_INFO,
         "BKVOICE local wake active label=%s frontend=%lu sha256=%s bytes=%zu\n",
         selected->label, (unsigned long)selected->frontend_version,
         selected->sha256_hex, size);
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
      selected.frontend_version = 1;
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

static int trigger_prepare_locked(void)
{
  if (g_agent_trigger.handle && g_agent_trigger.loaded)
    {
      return 0;
    }

  return trigger_load_selected(false);
}

static int trigger_start_locked(void)
{
  if (g_agent_trigger.handle && g_agent_trigger.loaded)
    {
      return trigger_rearm_locked();
    }

  return trigger_load_selected(true);
}

/* Existing asset transaction only: no audio turn, history or retry
 * scheduler. Old and candidate inference arenas never need to coexist.
 */

bool bk7258_agent_trigger_model_pending(void)
{
  pthread_mutex_lock(&g_trigger_lock);
  bool pending = g_agent_trigger.pending_record != NULL;
  pthread_mutex_unlock(&g_trigger_lock);
  return pending;
}

static int trigger_model_step_locked(bool arm)
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
      !trigger_armed_locked())
    {
      return -EBUSY;
    }

  ret = trigger_stop_locked();
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
      if (arm) ret = trigger_rearm_locked();
    }
  else
    {
      int stopped = trigger_stop_locked();
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

static int trigger_control_locked(void *context,
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
      uint8_t wire[12 + 2 * BKVOICE_WAKE_PACKAGE_HEADER_V2] =
        {
          'W', 'K', 'S', '2'
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
      if (wire_descriptor(wire + 12, &g_agent_trigger.active,
                          g_agent_trigger.active_size) < 0 ||
          wire_descriptor(wire + 12 + BKVOICE_WAKE_PACKAGE_HEADER_V2,
                          &g_agent_trigger.previous,
                          g_agent_trigger.previous_size) < 0)
        {
          return -EBADMSG;
        }
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

  if ((g_agent_trigger.recognizing && !trigger_armed_locked()) ||
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

int bk7258_agent_trigger_reply(void)
{
  audio_playback_t *player;
  uint8_t pcm[640];
  ssize_t size;
  int closed;
  int ret;
  int fd;

  if (!atomic_exchange(&g_agent_trigger.reply_pending, false)) return -ENOENT;
  if (atomic_load(&g_agent_trigger.reply_abort)) return -ECANCELED;
  fd = open(CONFIG_MEDIA_SERVER_CONFIG_PATH "/wake_reply.pcm", O_RDONLY);

  if (fd < 0)
    {
      return -errno;
    }

  player = audio_playback_open(NULL, 16000, 1, 16);
  ret = player ? 0 : -(errno ? errno : EIO);
  pthread_mutex_lock(&g_reply_lock);
  if (atomic_load(&g_agent_trigger.reply_abort)) ret = -ECANCELED;
  if (ret >= 0) g_reply_player = player;
  pthread_mutex_unlock(&g_reply_lock);
  while (player && ret >= 0)
    {
      if (atomic_load(&g_agent_trigger.reply_abort))
        {
          ret = -ECANCELED;
          break;
        }
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
  pthread_mutex_lock(&g_reply_lock);
  g_reply_player = NULL;
  pthread_mutex_unlock(&g_reply_lock);
  closed = player ? audio_playback_close(player) : 0;
  if (atomic_load(&g_agent_trigger.reply_abort)) ret = -ECANCELED;
  return closed < 0 ? closed : ret;
}

void bk7258_agent_trigger_reply_discard(void)
{
  atomic_store(&g_agent_trigger.reply_pending, false);
}

void bk7258_agent_trigger_reply_cancel(void)
{
  atomic_store(&g_agent_trigger.reply_pending, false);
  atomic_store(&g_agent_trigger.reply_abort, true);
  pthread_mutex_lock(&g_reply_lock);
  if (g_reply_player) audio_playback_stop(g_reply_player);
  pthread_mutex_unlock(&g_reply_lock);
}

static int trigger_process_locked(void)
{
  int callback_error;
  int ret;

  if (!atomic_exchange(&g_agent_trigger.turn_pending, false))
    {
      return 0;
    }

  /* Joining detection leaves the recorder and its pending samples alive.
   * The Agent claims this exact capture after its bounded ASR preparation.
   */

  atomic_store(&g_agent_trigger.accepting, false);
  ret = trigger_join();
  callback_error = atomic_exchange(&g_agent_trigger.callback_error, 0);
  if (!ret && callback_error) ret = callback_error;
  if (!ret)
    {
      /* Ownership transfers even if Agent startup fails and releases it.
       * No local pointer may outlive that release. Cleanup also covers a
       * connection failure before the Agent claims the pending recorder.
       * The acknowledgement is deferred until this turn's capture closes,
       * since there is no AEC reference for simultaneous speech and reply.
       */

      g_agent_trigger.capture = NULL;
      atomic_store(&g_agent_trigger.reply_abort, false);
      atomic_store(&g_agent_trigger.reply_pending, true);
      ret = voice_channel_start_auto_wake();
      if (ret < 0)
        {
          atomic_store(&g_agent_trigger.reply_pending, false);
          int cleanup = audio_capture_cleanup(100);
          if (cleanup < 0) ret = cleanup;
        }
    }
  else
    {
      int cleanup = trigger_pause();
      if (cleanup < 0) ret = cleanup;
    }

  if (ret < 0)
    {
      syslog(LOG_WARNING, "BKVOICE local wake turn start failed=%d\n",
             ret);
      if (!callback_error)
        {
          (void)trigger_rearm_locked();
        }
    }

  return ret;
}

int bk7258_agent_trigger_prepare(void)
{
  pthread_mutex_lock(&g_trigger_lock);
  int ret = trigger_prepare_locked();
  pthread_mutex_unlock(&g_trigger_lock);
  return ret;
}

int bk7258_agent_trigger_start(void)
{
  pthread_mutex_lock(&g_trigger_lock);
  int ret = trigger_start_locked();
  pthread_mutex_unlock(&g_trigger_lock);
  return ret;
}

int bk7258_agent_trigger_stop(void)
{
  pthread_mutex_lock(&g_trigger_lock);
  int ret = trigger_stop_locked();
  pthread_mutex_unlock(&g_trigger_lock);
  return ret;
}

int bk7258_agent_trigger_rearm(void)
{
  pthread_mutex_lock(&g_trigger_lock);
  int ret = trigger_rearm_locked();
  pthread_mutex_unlock(&g_trigger_lock);
  return ret;
}

bool bk7258_agent_trigger_armed(void)
{
  pthread_mutex_lock(&g_trigger_lock);
  bool armed = trigger_armed_locked();
  pthread_mutex_unlock(&g_trigger_lock);
  return armed;
}

int bk7258_agent_trigger_process(void)
{
  pthread_mutex_lock(&g_trigger_lock);
  int ret = trigger_process_locked();
  pthread_mutex_unlock(&g_trigger_lock);
  return ret;
}

int bk7258_agent_trigger_model_step(bool arm)
{
  pthread_mutex_lock(&g_trigger_lock);
  int ret = trigger_model_step_locked(arm);
  pthread_mutex_unlock(&g_trigger_lock);
  return ret;
}

int bk7258_agent_trigger_control(void *context,
  enum bkcontrol_command_e command, uint32_t kind, uint32_t offset,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  pthread_mutex_lock(&g_trigger_lock);
  int ret = trigger_control_locked(context, command, kind, offset,
                                    record, size, status);
  pthread_mutex_unlock(&g_trigger_lock);
  return ret;
}
