/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_turn_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <media_player.h>
#include <media_recorder.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_voice_turn.h"
#include "bk7258_voice_turn_audio.h"

#define TEST_FRAME_BYTES 640u
#define TEST_TRACE_MAX   64u

enum test_call_e
{
  TEST_REC_OPEN = 1,
  TEST_REC_PREPARE,
  TEST_REC_START,
  TEST_REC_READ,
  TEST_REC_STOP,
  TEST_REC_CLOSE,
  TEST_PLAYER_OPEN,
  TEST_PLAYER_PREPARE,
  TEST_PLAYER_START,
  TEST_PLAYER_WRITE,
  TEST_PLAYER_STOP,
  TEST_PLAYER_CLOSE,
};

struct test_media_s
{
  enum test_call_e trace[TEST_TRACE_MAX];
  size_t trace_count;
  enum test_call_e fail_call;
  unsigned int fail_count;
  bool recorder_open;
  bool recorder_prepared;
  bool recorder_started;
  bool player_open;
  bool player_prepared;
  bool player_started;
  bool overlap;
};

static struct test_media_s g_media;
static int g_recorder_handle;
static int g_player_handle;

static int test_call(enum test_call_e call)
{
  assert(g_media.trace_count < TEST_TRACE_MAX);
  g_media.trace[g_media.trace_count++] = call;
  if (g_media.fail_call == call && g_media.fail_count > 0)
    {
      g_media.fail_count--;
      return -EIO;
    }

  return 0;
}

void *media_recorder_open(const char *params)
{
  int ret = test_call(TEST_REC_OPEN);

  assert(params != NULL && strcmp(params, MEDIA_SOURCE_MIC) == 0);
  if (g_media.player_open)
    {
      g_media.overlap = true;
    }

  if (ret < 0)
    {
      errno = EIO;
      return NULL;
    }

  assert(!g_media.recorder_open);
  g_media.recorder_open = true;
  return &g_recorder_handle;
}

int media_recorder_prepare(void *handle, const char *url,
                           const char *options)
{
  int ret = test_call(TEST_REC_PREPARE);

  assert(handle == &g_recorder_handle && url == NULL);
  assert(options != NULL &&
         strcmp(options,
                "format=s16le:sample_rate=16000:ch_layout=mono") == 0);
  if (ret >= 0)
    {
      g_media.recorder_prepared = true;
    }

  return ret;
}

int media_recorder_start(void *handle)
{
  int ret = test_call(TEST_REC_START);

  assert(handle == &g_recorder_handle && g_media.recorder_prepared);
  if (ret >= 0)
    {
      g_media.recorder_started = true;
    }

  return ret;
}

ssize_t media_recorder_read_data(void *handle, void *data, size_t len)
{
  int ret = test_call(TEST_REC_READ);

  assert(handle == &g_recorder_handle && data != NULL && len > 0);
  assert(g_media.recorder_started);
  if (ret < 0)
    {
      return ret;
    }

  memset(data, 0x5a, len);
  return (ssize_t)len;
}

int media_recorder_stop(void *handle)
{
  int ret = test_call(TEST_REC_STOP);

  assert(handle == &g_recorder_handle && g_media.recorder_open);
  if (ret >= 0)
    {
      g_media.recorder_started = false;
    }

  return ret;
}

int media_recorder_close(void *handle)
{
  int ret = test_call(TEST_REC_CLOSE);

  assert(handle == &g_recorder_handle && g_media.recorder_open);
  if (ret >= 0)
    {
      g_media.recorder_open = false;
      g_media.recorder_prepared = false;
      g_media.recorder_started = false;
    }

  return ret;
}

void *media_player_open(const char *stream)
{
  int ret = test_call(TEST_PLAYER_OPEN);

  assert(stream != NULL && strcmp(stream, MEDIA_STREAM_MUSIC) == 0);
  if (g_media.recorder_open)
    {
      g_media.overlap = true;
    }

  if (ret < 0)
    {
      errno = EIO;
      return NULL;
    }

  assert(!g_media.player_open);
  g_media.player_open = true;
  return &g_player_handle;
}

int media_player_prepare(void *handle, const char *url,
                         const char *options)
{
  int ret = test_call(TEST_PLAYER_PREPARE);

  assert(handle == &g_player_handle && url == NULL);
  assert(options != NULL &&
         strcmp(options,
                "format=s16le:sample_rate=16000:ch_layout=mono") == 0);
  if (ret >= 0)
    {
      g_media.player_prepared = true;
    }

  return ret;
}

int media_player_start(void *handle)
{
  int ret = test_call(TEST_PLAYER_START);

  assert(handle == &g_player_handle && g_media.player_prepared);
  if (ret >= 0)
    {
      g_media.player_started = true;
    }

  return ret;
}

ssize_t media_player_write_data(void *handle, const void *data, size_t len)
{
  int ret = test_call(TEST_PLAYER_WRITE);

  assert(handle == &g_player_handle && data != NULL && len > 0);
  assert(g_media.player_started);
  return ret < 0 ? ret : (ssize_t)len;
}

int media_player_stop(void *handle)
{
  int ret = test_call(TEST_PLAYER_STOP);

  assert(handle == &g_player_handle && g_media.player_open);
  if (ret >= 0)
    {
      g_media.player_prepared = false;
      g_media.player_started = false;
    }

  return ret;
}

int media_player_close(void *handle, int pending_stop)
{
  int ret = test_call(TEST_PLAYER_CLOSE);

  assert(handle == &g_player_handle && pending_stop == 1);
  assert(g_media.player_open);
  if (ret >= 0)
    {
      g_media.player_open = false;
      g_media.player_prepared = false;
      g_media.player_started = false;
    }

  return ret;
}

static const struct bkvoice_turn_limits_s g_limits =
{
  .capture_timeout_ms = 1000,
  .waiting_tts_timeout_ms = 2000,
  .playback_timeout_ms = 3000,
  .audio_frame_bytes = TEST_FRAME_BYTES,
};

static struct bkvoice_turn_token_s test_event(
  const struct bkvoice_turn_token_s *token, uint32_t sequence)
{
  struct bkvoice_turn_token_s event = *token;

  event.sequence = sequence;
  return event;
}

static void test_initialize(struct bkvoice_turn_s *turn,
                            struct bkvoice_turn_audio_s *audio)
{
  memset(&g_media, 0, sizeof(g_media));
  assert(bkvoice_turn_audio_initialize(audio) == 0);
  assert(bkvoice_turn_audio_released(audio));
  assert(bkvoice_turn_initialize(turn, bkvoice_turn_audio_ops(), audio,
                                 &g_limits, 11) == 0);
  assert(bkvoice_turn_session_open(turn, 3) == 0);
}

static void test_normal_turn(void)
{
  static const enum test_call_e expected[] =
  {
    TEST_REC_OPEN, TEST_REC_PREPARE, TEST_REC_START, TEST_REC_READ,
    TEST_REC_STOP, TEST_REC_CLOSE,
    TEST_PLAYER_OPEN, TEST_PLAYER_PREPARE, TEST_PLAYER_START,
    TEST_PLAYER_WRITE, TEST_PLAYER_STOP, TEST_PLAYER_CLOSE,
  };
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  uint8_t pcm[TEST_FRAME_BYTES];

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 100, &token) == 0);
  assert(bkvoice_turn_audio_reader_attach(&audio) == 0);
  assert(bkvoice_turn_audio_reader_attach(&audio) == -EALREADY);
  assert(bkvoice_turn_audio_read(&audio, pcm, sizeof(pcm)) ==
         (ssize_t)sizeof(pcm));
  assert(pcm[0] == 0x5a && pcm[sizeof(pcm) - 1] == 0x5a);
  assert(bkvoice_turn_audio_reader_detach(&audio) == 0);

  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 200) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 300) == 0);
  event.sequence = 2;
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm),
                                400) == 0);
  event.sequence = 3;
  assert(bkvoice_turn_tts_end(&turn, &event) == 0);
  assert(bkvoice_turn_audio_released(&audio));
  assert(!g_media.recorder_open && !g_media.player_open &&
         !g_media.overlap);
  assert(g_media.trace_count == sizeof(expected) / sizeof(expected[0]));
  assert(memcmp(g_media.trace, expected, sizeof(expected)) == 0);
}

static void test_recorder_close_recovery(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  g_media.fail_call = TEST_REC_CLOSE;
  g_media.fail_count = 1;
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -EIO);
  assert(turn.state == BKVOICE_TURN_FAULTED);
  assert(audio.mic_handle != NULL && g_media.recorder_open);
  assert(bkvoice_turn_recover(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_player_stop_retry(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  uint8_t pcm[TEST_FRAME_BYTES] = {0};
  size_t before;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 2) == 0);
  event.sequence = 2;
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm), 3) == 0);

  g_media.fail_call = TEST_PLAYER_STOP;
  g_media.fail_count = 1;
  before = g_media.trace_count;
  event.sequence = 3;
  assert(bkvoice_turn_tts_end(&turn, &event) == -EIO);
  assert(g_media.trace_count == before + 3);
  assert(g_media.trace[before] == TEST_PLAYER_STOP);
  assert(g_media.trace[before + 1] == TEST_PLAYER_STOP);
  assert(g_media.trace[before + 2] == TEST_PLAYER_CLOSE);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_player_close_recovery(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 2) == 0);

  g_media.fail_call = TEST_PLAYER_CLOSE;
  g_media.fail_count = 1;
  event.sequence = 2;
  assert(bkvoice_turn_tts_end(&turn, &event) == -EIO);
  assert(turn.state == BKVOICE_TURN_FAULTED);
  assert(audio.dac_handle != NULL && g_media.player_open);
  assert(bkvoice_turn_recover(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_guards_and_open_failure(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_s turn;
  uint8_t pcm[TEST_FRAME_BYTES];

  assert(bkvoice_turn_audio_initialize(NULL) == -EINVAL);
  assert(bkvoice_turn_audio_reader_attach(NULL) == -EINVAL);
  assert(!bkvoice_turn_audio_released(NULL));
  assert(bkvoice_turn_audio_initialize(&audio) == 0);
  assert(bkvoice_turn_audio_reader_attach(&audio) == -EPERM);
  assert(bkvoice_turn_audio_reader_detach(&audio) == -EALREADY);
  assert(bkvoice_turn_audio_read(&audio, pcm, sizeof(pcm)) == -EPERM);

  test_initialize(&turn, &audio);
  g_media.fail_call = TEST_REC_OPEN;
  g_media.fail_count = 1;
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == -EIO);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_reader_pins_recorder(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  assert(bkvoice_turn_audio_reader_attach(&audio) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -EBUSY);
  assert(turn.state == BKVOICE_TURN_FAULTED);
  assert(audio.mic_handle != NULL && g_media.recorder_open);
  assert(bkvoice_turn_recover(&turn) == -EBUSY);
  assert(audio.mic_handle != NULL && g_media.recorder_open);
  assert(bkvoice_turn_audio_reader_detach(&audio) == 0);
  assert(bkvoice_turn_recover(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

int main(void)
{
  test_normal_turn();
  test_recorder_close_recovery();
  test_player_stop_retry();
  test_player_close_recovery();
  test_guards_and_open_failure();
  test_reader_pins_recorder();
  puts("BKVOICE_TURN_AUDIO_HOST_PASS");
  return 0;
}
