/****************************************************************************
 * app/bk7258/bk7258_voice_turn_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BKVoice MIC/DAC lifecycle adapter.  Device paths, channels, PA polarity and
 * pin ownership stay behind the public recorder/player ABI and its lower
 * halves; this App layer declares the product's source PCM format.
 ****************************************************************************/

#ifdef __NuttX__
#include <nuttx/config.h>
#endif

#include "bk7258_voice_turn_audio.h"

#include <errno.h>
#include <media_player.h>
#include <media_policy.h>
#include <media_recorder.h>
#ifdef CONFIG_BK7258_PREFERENCES
#include "bk7258_preferences.h"
#endif
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
#include "bk7258_voice_volume_store.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef CONFIG_MEDIA
#include "bk7258_voice_media.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <syslog.h>
#include <time.h>
#endif

#define BKVOICE_TURN_AUDIO_OPTIONS \
  "format=s16le:sample_rate=16000:ch_layout=mono"

static int bkvoice_turn_audio_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

#ifdef CONFIG_MEDIA
static int bkvoice_turn_audio_nonblocking(int fd)
{
  int flags;
  if (fd < 0) return fd;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return bkvoice_turn_audio_errno();
  return 0;
}

/* The pinned NuttX local socket uses FIFOs and does not consume SO_*TIMEO.
 * Poll a nonblocking public Media socket with one deadline for the transfer.
 * The product still calls the official read/write API and owns no new worker.
 */
static ssize_t bkvoice_turn_audio_transfer(void *handle, bool player,
                                           void *data, size_t bytes,
                                           unsigned int timeout_ms)
{
  struct timespec now;
  struct pollfd pfd = {
    .fd = player ? media_player_get_socket(handle) :
                   media_recorder_get_socket(handle),
    .events = player ? POLLOUT : POLLIN
  };
  uint64_t started;
  uint64_t deadline;
  uint64_t current;
  size_t done = 0;
  ssize_t result;
  const char *stage = "deadline";
  int ret;

  if (pfd.fd < 0) return -EPIPE;
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    return bkvoice_turn_audio_errno();
  started = (uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u;
  deadline = started + timeout_ms;
  while (done < bytes)
    {
      if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return bkvoice_turn_audio_errno();
      current = (uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u;
      if (current >= deadline) { ret = -ETIMEDOUT; goto failed; }
      /* A nonblocking transfer is the authoritative progress check. Avoid
       * setting up a poll on every small PCM frame when the stream already
       * has capacity; wait only after the public API reports backpressure.
       */
      result = player ? media_player_write_data(handle, (uint8_t *)data + done,
                                                 bytes - done) :
                        media_recorder_read_data(handle, data, bytes);
      if (result == -EINTR) continue;
      if (result == -EAGAIN || result == -EWOULDBLOCK)
        {
          if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
            return bkvoice_turn_audio_errno();
          current = (uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u;
          if (current >= deadline) { ret = -ETIMEDOUT; goto failed; }
          pfd.revents = 0;
          ret = poll(&pfd, 1, (int)(deadline - current));
          if (ret < 0 && errno == EINTR) continue;
          if (ret < 0)
            { ret = bkvoice_turn_audio_errno(); stage = "poll"; goto failed; }
          /* Keep the original monotonic deadline authoritative, including
           * when a platform poll timeout returns at a tick boundary. */
          if (ret == 0) continue;
          if (!(pfd.revents & pfd.events))
            { ret = -EPIPE; stage = "poll-event"; goto failed; }
          continue;
        }
      if (result <= 0)
        {
          ret = result == 0 || result == -ECONNRESET ? -EPIPE : result;
          stage = "io";
          goto failed;
        }
      if (!player) return result;
      done += result;
    }
  return (ssize_t)done;

failed:
  if (player)
    {
      if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        current = (uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u;
      syslog(LOG_WARNING, "BKVOICE player write stage=%s ret=%d "
             "bytes=%lu/%lu elapsed_ms=%llu timeout_ms=%u events=%x\n",
             stage, ret, (unsigned long)done, (unsigned long)bytes,
             (unsigned long long)(current - started), timeout_ms,
             (unsigned int)pfd.revents);
    }
  return ret;
}
#endif

bool bkvoice_turn_audio_released(const struct bkvoice_turn_audio_s *audio)
{
  return audio != NULL && audio->mic_handle == NULL &&
         audio->dac_handle == NULL && !audio->mic_prepared &&
         !audio->mic_started && !audio->mic_policy_active &&
         !__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE) &&
         !audio->dac_prepared &&
         !audio->dac_started;
}

int bkvoice_turn_audio_initialize(struct bkvoice_turn_audio_s *audio)
{
  if (audio == NULL)
    {
      return -EINVAL;
    }

  memset(audio, 0, sizeof(*audio));
  return 0;
}

static int bkvoice_turn_audio_mic_acquire(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (audio->mic_handle != NULL || audio->dac_handle != NULL ||
      __atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  errno = 0;
  audio->mic_handle = media_recorder_open(MEDIA_SOURCE_MIC);
  return audio->mic_handle != NULL ? 0 : bkvoice_turn_audio_errno();
}

static int bkvoice_turn_audio_mic_prepare(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->mic_handle == NULL)
    {
      return -EINVAL;
    }

  if (audio->mic_prepared)
    {
      return -EALREADY;
    }

  ret = media_recorder_prepare(audio->mic_handle, NULL,
                               BKVOICE_TURN_AUDIO_OPTIONS);
  if (ret >= 0)
    {
      audio->mic_prepared = true;
#ifdef CONFIG_MEDIA
      ret = bkvoice_turn_audio_nonblocking(
        media_recorder_get_socket(audio->mic_handle));
      if (ret < 0) return ret;
#endif
      return 0;
    }

  return ret;
}

/* The product's MIC permission follows its serialized turn. Resolve the
 * configured stream through public policy; no physical device name belongs
 * in this adapter. The Media policy owns starting/stopping the device route. */
static int bkvoice_turn_audio_mic_policy(struct bkvoice_turn_audio_s *audio,
                                        bool active)
{
#ifdef CONFIG_MEDIA
  int ret;
  if (audio->mic_policy_active == active) return 0;
  ret = bkvoice_media_source_set_active(MEDIA_SOURCE_MIC, active);
  if (ret < 0) return ret;
  audio->mic_policy_active = active;
#else
  (void)audio;
  (void)active;
#endif
  return 0;
}

static int bkvoice_turn_audio_mic_start(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->mic_handle == NULL || !audio->mic_prepared)
    {
      return -EINVAL;
    }

  if (audio->mic_started)
    {
      return -EALREADY;
    }

  ret = bkvoice_turn_audio_mic_policy(audio, true);
  if (ret < 0) return ret;
  ret = media_recorder_start(audio->mic_handle);
  if (ret >= 0)
    {
      audio->mic_started = true;
      return ret;
    }

  int cleanup = bkvoice_turn_audio_mic_policy(audio, false);
  if (cleanup < 0) return cleanup;
  return ret;
}

int bkvoice_turn_audio_reader_attach(struct bkvoice_turn_audio_s *audio)
{
  bool expected = false;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (audio->mic_handle == NULL || !audio->mic_prepared ||
      !audio->mic_started)
    {
      return -EPERM;
    }

  if (!__atomic_compare_exchange_n(&audio->mic_reader_active, &expected,
                                   true, false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return -EALREADY;
    }

  return 0;
}

int bkvoice_turn_audio_reader_detach(struct bkvoice_turn_audio_s *audio)
{
  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (!__atomic_exchange_n(&audio->mic_reader_active, false,
                           __ATOMIC_ACQ_REL))
    {
      return -EALREADY;
    }

  return 0;
}

ssize_t bkvoice_turn_audio_read(struct bkvoice_turn_audio_s *audio,
                                void *pcm, size_t bytes)
{
  if (audio == NULL || pcm == NULL || bytes == 0)
    {
      return -EINVAL;
    }

  /* A registered reader pins the recorder handle until detach.  After stop,
   * the blocked public read returns -EPIPE; release refuses to destroy the
   * handle until the reader has observed that wake and detached.
   */

  if (audio->mic_handle == NULL ||
      !__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EPERM;
    }

#ifdef CONFIG_MEDIA
  return bkvoice_turn_audio_transfer(audio->mic_handle, false, pcm, bytes,
                                      CONFIG_BK7258_MEDIA_RECORDER_NO_FRAME_TIMEOUT_MS);
#else
  return media_recorder_read_data(audio->mic_handle, pcm, bytes);
#endif
}

static int bkvoice_turn_audio_mic_stop(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->mic_handle == NULL)
    {
      return -EINVAL;
    }

  if (!audio->mic_started && !audio->mic_policy_active)
    {
      return 0;
    }

  /* Official Media pins its socket while read_data() is blocked. Shutdown
   * wakes that reader without closing/reusing its fd beneath it. */
#ifdef CONFIG_MEDIA
  if (__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      int fd = media_recorder_get_socket(audio->mic_handle);
      if (fd >= 0) (void)shutdown(fd, SHUT_RD);
    }
#endif
  ret = media_recorder_stop(audio->mic_handle);
  int policy_ret = bkvoice_turn_audio_mic_policy(audio, false);
  if (ret >= 0 && policy_ret < 0) ret = policy_ret;
  if (ret >= 0)
    {
      audio->mic_started = false;
      return 0;
    }

  return ret;
}

int bkvoice_turn_audio_reader_stop(struct bkvoice_turn_audio_s *audio)
{
  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EPERM;
    }

  /* PTT release stops the public recorder so the blocking reader wakes, but
   * deliberately leaves that reader attached.  The owner must join and
   * detach it before the turn arbiter may drain and release the MIC.
   */

  return bkvoice_turn_audio_mic_stop(audio);
}

static int bkvoice_turn_audio_mic_drain(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL || audio->mic_handle == NULL ||
      !audio->mic_prepared)
    {
      return -EINVAL;
    }

  /* Stop interrupts the public reader and disables the capture route. The
   * capture owner joins that reader before this drain/release sequence.
   */

  return audio->mic_started || audio->mic_policy_active ||
         __atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE) ?
         -EBUSY : 0;
}

static int bkvoice_turn_audio_mic_release(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  if (audio->mic_handle == NULL)
    {
      return 0;
    }

  ret = bkvoice_turn_audio_mic_policy(audio, false);
  if (ret < 0) return ret;

  ret = media_recorder_close(audio->mic_handle);
  if (ret >= 0)
    {
      audio->mic_handle = NULL;
      audio->mic_prepared = false;
      audio->mic_started = false;
      __atomic_store_n(&audio->mic_reader_active, false,
                       __ATOMIC_RELEASE);
      return 0;
    }

  /* A negative public close result retains the handle for cleanup retry. */

  return ret;
}

static void bkvoice_turn_audio_event(void *cookie, int event, int result,
                                     const char *extra)
{
  struct bkvoice_turn_audio_s *audio = cookie;
  (void)extra;
  if (event == MEDIA_EVENT_COMPLETED)
    {
      audio->dac_result = result;
      __atomic_store_n(&audio->dac_completed, true, __ATOMIC_RELEASE);
    }
}

static int bkvoice_turn_audio_dac_acquire(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (audio->mic_handle != NULL || audio->dac_handle != NULL ||
      __atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  errno = 0;
  audio->dac_handle = media_player_open(MEDIA_STREAM_MUSIC);
  return audio->dac_handle != NULL ? 0 : bkvoice_turn_audio_errno();
}

static int bkvoice_turn_audio_volume_policy(struct bkvoice_turn_audio_s *audio,
                                            bool apply,
                                            unsigned int requested,
                                            unsigned int *volume)
{
  int minimum;
  int maximum;
  int index;
  int observed;
  int ret;

  if (audio == NULL || volume == NULL || (apply && requested > 100u))
    {
      return -EINVAL;
    }

  ret = media_policy_get_range(MEDIA_STREAM_MUSIC MEDIA_POLICY_VOLUME,
                                &minimum, &maximum);
  if (ret < 0)
    {
      return ret;
    }

  if (minimum < 0 || maximum <= minimum)
    {
      return -ERANGE;
    }

  index = minimum + (int)(((uint64_t)requested *
                           (maximum - minimum) + 50u) / 100u);
  if (apply)
    {
      ret = media_policy_set_stream_volume(MEDIA_STREAM_MUSIC, index);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = media_policy_get_stream_volume(MEDIA_STREAM_MUSIC, &observed);
  if (ret < 0)
    {
      return ret;
    }

  if (observed < minimum || observed > maximum ||
      (apply && observed != index))
    {
      return -EIO;
    }

  *volume = (unsigned int)(((uint64_t)(observed - minimum) * 100u +
                            (maximum - minimum) / 2u) / (maximum - minimum));
  if (apply)
    {
      audio->volume_override = true;
      audio->volume_percent = requested;
    }

  return 0;
}

static int bkvoice_turn_audio_volume(void *context, bool set,
                                     unsigned int requested,
                                     unsigned int *volume)
{
  struct bkvoice_turn_audio_s *audio = context;
  unsigned int persisted;
  int ret;

  if (audio == NULL || volume == NULL || (set && requested > 100u))
    {
      return -EINVAL;
    }

#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  if (set)
    {
      /* A successful report promises reboot persistence. Publish the small
       * LittleFS record before changing the live media policy.
       */
      ret = bkvoice_volume_store_set(requested);
      if (ret < 0)
        {
          return ret;
        }
    }
  else if (!audio->volume_override &&
           bkvoice_volume_store_get(&persisted) == 0)
    {
      /* The first post-boot query also reconciles the media policy, so the
       * App observes the restored value before the next TTS turn.
       */
      return bkvoice_turn_audio_volume_policy(audio, true, persisted, volume);
    }
#else
  (void)persisted;
  (void)ret;
#endif

  return bkvoice_turn_audio_volume_policy(audio, set, requested, volume);
}

static int bkvoice_turn_audio_apply_volume(struct bkvoice_turn_audio_s *audio)
{
  unsigned int desired = audio->volume_percent;
  unsigned int observed;
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  /* Every control path commits to the device volume store.  A previous
   * live override must not hide a later settings/CLI change on next reply.
   */
  bool override = false;
#else
  bool override = audio->volume_override;
#endif
  int ret;

  if (!override)
    {
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
      ret = bkvoice_volume_store_get(&desired);
      if (ret < 0)
        {
          /* Missing, not-yet-mounted or damaged preference data must not
           * take the core voice path offline. A later explicit mutation can
           * still report the precise storage failure to the App.
           */
          return 0;
        }
#elif defined(CONFIG_BK7258_PREFERENCES)
      ret = bk7258_preferences_playback_volume(&desired);
      if (ret < 0)
        {
          return ret;
        }
#else
      return 0;
#endif
    }

  if (desired > 100u)
    {
      return -ERANGE;
    }

  ret = bkvoice_turn_audio_volume_policy(audio, true, desired, &observed);
  audio->volume_override = override;
  return ret;
}

static int bkvoice_turn_audio_dac_prepare(void *context,
                                         unsigned int sample_rate)
{
  struct bkvoice_turn_audio_s *audio = context;
#ifdef CONFIG_MEDIA
  char options[96];
#endif
  int ret;

  if (audio == NULL || audio->dac_handle == NULL)
    {
      return -EINVAL;
    }

  if (audio->dac_prepared)
    {
      return -EALREADY;
    }

#ifdef CONFIG_MEDIA
  if (sample_rate == 0) return -EINVAL;
  ret = snprintf(options, sizeof(options),
                 "format=s16le:sample_rate=%u:ch_layout=mono:datqmax=66",
                 sample_rate);
  if (ret < 0 || ret >= sizeof(options)) return -EMSGSIZE;
#else
  if (sample_rate != 16000) return -ENOTSUP;
#endif

  __atomic_store_n(&audio->dac_completed, false, __ATOMIC_RELEASE);
  ret = media_player_set_event_callback(audio->dac_handle, audio,
                                        bkvoice_turn_audio_event);
  if (ret < 0)
    {
      return ret;
    }

  /* The board graph explicitly converts the declared source rate for its
   * sink. The PCM demuxer emits 2048 samples per frame at 24 kHz. The SSE
   * adapter validates a complete event before publishing its PCM. Its
   * 256-KiB JSON limit permits up to 192 KiB of PCM (48 frames), so buffering
   * only individual TLS read gaps still starves between events. Reserve
   * that event budget plus 18 frames of network margin: 270336 PCM bytes,
   * or 5.632 seconds at 24 kHz. Short replies start at EOF. Queue ownership,
   * bounded socket writes and EOF draining stay in the official player.
   */
  ret = media_player_prepare(audio->dac_handle, NULL,
#ifdef CONFIG_MEDIA
                             options);
#else
                             BKVOICE_TURN_AUDIO_OPTIONS);
#endif
  if (ret >= 0)
    {
      audio->dac_prepared = true;
#ifdef CONFIG_MEDIA
      ret = bkvoice_turn_audio_nonblocking(
        media_player_get_socket(audio->dac_handle));
      if (ret < 0) return ret;
#endif
      /* Mark prepared before applying policy so failures still release the
       * player's reservation and buffers through the normal turn cleanup.
       */

      return bkvoice_turn_audio_apply_volume(audio);
    }

  return ret;
}

static int bkvoice_turn_audio_dac_start(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->dac_handle == NULL || !audio->dac_prepared)
    {
      return -EINVAL;
    }

  if (audio->dac_started)
    {
      return -EALREADY;
    }

  ret = media_player_start(audio->dac_handle);
  if (ret >= 0)
    {
      audio->dac_started = true;
      return 0;
    }

  return ret;
}

static ssize_t bkvoice_turn_audio_dac_write(void *context,
                                            const uint8_t *pcm,
                                            size_t bytes)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL || pcm == NULL || bytes == 0)
    {
      return -EINVAL;
    }

  if (audio->dac_handle == NULL || !audio->dac_prepared ||
      !audio->dac_started)
    {
      return -EPERM;
    }

#ifdef CONFIG_MEDIA
  return bkvoice_turn_audio_transfer(audio->dac_handle, true, (void *)pcm, bytes,
                                      CONFIG_BK7258_MEDIA_PLAYER_WRITE_TIMEOUT_MS);
#else
  return media_player_write_data(audio->dac_handle, pcm, bytes);
#endif
}

static int bkvoice_turn_audio_dac_drain(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL || audio->dac_handle == NULL)
    {
      return -EINVAL;
    }

  if (!audio->dac_prepared && !audio->dac_started)
    {
      return 0;
    }

  media_player_close_socket(audio->dac_handle);
  return -EINPROGRESS;
}

static int bkvoice_turn_audio_dac_result(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  if (!__atomic_exchange_n(&audio->dac_completed, false, __ATOMIC_ACQ_REL))
    {
      return -EAGAIN;
    }

  return audio->dac_result;
}

static int bkvoice_turn_audio_dac_stop(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->dac_handle == NULL)
    {
      return -EINVAL;
    }

  if (!audio->dac_prepared && !audio->dac_started)
    {
      return 0;
    }

  /* Cancellation skips drain; the public close API stops immediately. */

  ret = media_player_close(audio->dac_handle, 0);
  if (ret >= 0)
    {
      audio->dac_handle = NULL;
      audio->dac_prepared = false;
      audio->dac_started = false;
      return 0;
    }

  return ret;
}

static int bkvoice_turn_audio_dac_release(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (audio->dac_handle == NULL)
    {
      return 0;
    }

  ret = media_player_close(audio->dac_handle, 0);
  if (ret >= 0)
    {
      audio->dac_handle = NULL;
      audio->dac_prepared = false;
      audio->dac_started = false;
      return 0;
    }

  /* A negative public close result retains the handle for cleanup retry.
   */

  return ret;
}

static const struct bkvoice_turn_audio_ops_s g_bkvoice_turn_audio_ops =
{
  .mic_acquire = bkvoice_turn_audio_mic_acquire,
  .mic_prepare = bkvoice_turn_audio_mic_prepare,
  .mic_start = bkvoice_turn_audio_mic_start,
  .mic_stop = bkvoice_turn_audio_mic_stop,
  .mic_drain = bkvoice_turn_audio_mic_drain,
  .mic_release = bkvoice_turn_audio_mic_release,
  .dac_acquire = bkvoice_turn_audio_dac_acquire,
  .dac_prepare = bkvoice_turn_audio_dac_prepare,
  .dac_start = bkvoice_turn_audio_dac_start,
  .dac_write = bkvoice_turn_audio_dac_write,
  .dac_drain = bkvoice_turn_audio_dac_drain,
  .dac_result = bkvoice_turn_audio_dac_result,
  .dac_stop = bkvoice_turn_audio_dac_stop,
  .dac_release = bkvoice_turn_audio_dac_release,
  .volume = bkvoice_turn_audio_volume,
};

static int bkvoice_turn_capture_attach(void *context)
{
  return bkvoice_turn_audio_reader_attach(context);
}

static ssize_t bkvoice_turn_capture_read(void *context, void *pcm,
                                         size_t bytes)
{
  return bkvoice_turn_audio_read(context, pcm, bytes);
}

static int bkvoice_turn_capture_interrupt(void *context)
{
  return bkvoice_turn_audio_reader_stop(context);
}

static int bkvoice_turn_capture_detach(void *context)
{
  return bkvoice_turn_audio_reader_detach(context);
}

static const struct bkvoice_capture_source_ops_s
  g_bkvoice_turn_capture_source_ops =
{
  .attach = bkvoice_turn_capture_attach,
  .read = bkvoice_turn_capture_read,
  .interrupt = bkvoice_turn_capture_interrupt,
  .detach = bkvoice_turn_capture_detach,
};

const struct bkvoice_capture_source_ops_s *
bkvoice_turn_audio_capture_source_ops(void)
{
  return &g_bkvoice_turn_capture_source_ops;
}

const struct bkvoice_turn_audio_ops_s *bkvoice_turn_audio_ops(void)
{
  return &g_bkvoice_turn_audio_ops;
}
