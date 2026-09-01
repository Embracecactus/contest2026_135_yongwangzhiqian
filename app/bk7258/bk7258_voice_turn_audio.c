/****************************************************************************
 * app/bk7258/bk7258_voice_turn_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BKVoice MIC/DAC lifecycle adapter.  Device paths, channels, PA polarity and
 * pin ownership stay behind the public recorder/player ABI and its lower
 * halves; this App layer owns only the product's fixed PCM tuple.
 ****************************************************************************/

#include "bk7258_voice_turn_audio.h"

#include <errno.h>
#include <media_player.h>
#include <media_recorder.h>
#include <stdint.h>
#include <string.h>

#define BKVOICE_TURN_AUDIO_OPTIONS \
  "format=s16le:sample_rate=16000:ch_layout=mono"

static int bkvoice_turn_audio_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

bool bkvoice_turn_audio_released(const struct bkvoice_turn_audio_s *audio)
{
  return audio != NULL && audio->mic_handle == NULL &&
         audio->dac_handle == NULL && !audio->mic_prepared &&
         !audio->mic_started &&
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
      return 0;
    }

  return ret;
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

  ret = media_recorder_start(audio->mic_handle);
  if (ret >= 0)
    {
      audio->mic_started = true;
      return 0;
    }

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

  return media_recorder_read_data(audio->mic_handle, pcm, bytes);
}

static int bkvoice_turn_audio_mic_stop(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->mic_handle == NULL)
    {
      return -EINVAL;
    }

  if (!audio->mic_started)
    {
      return 0;
    }

  ret = media_recorder_stop(audio->mic_handle);
  if (ret >= 0)
    {
      audio->mic_started = false;
      return 0;
    }

  return ret;
}

static int bkvoice_turn_audio_mic_drain(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL || audio->mic_handle == NULL ||
      !audio->mic_prepared)
    {
      return -EINVAL;
    }

  /* media_recorder_stop() synchronously stops the lower half and wakes its
   * blocking mqueue reader.  The future capture owner must join that reader
   * before invoking this serialized drain/release sequence.
   */

  return audio->mic_started ||
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

  /* The BK7258 recorder bridge guarantees that a negative close result
   * leaves the handle alive for a later fail-closed recovery attempt.
   */

  return ret;
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

static int bkvoice_turn_audio_dac_prepare(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->dac_handle == NULL)
    {
      return -EINVAL;
    }

  if (audio->dac_prepared)
    {
      return -EALREADY;
    }

  ret = media_player_prepare(audio->dac_handle, NULL,
                             BKVOICE_TURN_AUDIO_OPTIONS);
  if (ret >= 0)
    {
      audio->dac_prepared = true;
      return 0;
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

  return media_player_write_data(audio->dac_handle, pcm, bytes);
}

static int bkvoice_turn_audio_dac_drain(void *context)
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

  /* The standalone BK7258 media_player_stop() drains pending PCM, stops the
   * lower half and releases its reservation while retaining the player
   * handle.  Model that folded public-backend operation here; dac_stop()
   * below is therefore idempotent after a successful drain.
   */

  ret = media_player_stop(audio->dac_handle);
  if (ret >= 0)
    {
      audio->dac_prepared = false;
      audio->dac_started = false;
      return 0;
    }

  return ret;
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

  /* Retry the folded cleanup only when dac_drain() failed. */

  ret = media_player_stop(audio->dac_handle);
  if (ret >= 0)
    {
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

  ret = media_player_close(audio->dac_handle, 1);
  if (ret >= 0)
    {
      audio->dac_handle = NULL;
      audio->dac_prepared = false;
      audio->dac_started = false;
      return 0;
    }

  /* The BK7258 player bridge guarantees that a negative close result leaves
   * the handle alive for a later fail-closed recovery attempt.
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
  .dac_stop = bkvoice_turn_audio_dac_stop,
  .dac_release = bkvoice_turn_audio_dac_release,
};

const struct bkvoice_turn_audio_ops_s *bkvoice_turn_audio_ops(void)
{
  return &g_bkvoice_turn_audio_ops;
}
