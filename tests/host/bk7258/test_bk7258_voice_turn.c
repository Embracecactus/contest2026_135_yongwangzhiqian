/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_turn.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_voice_turn.h"

#define TEST_FRAME_BYTES 640u
#define TEST_TRACE_MAX   128u

enum test_stage_e
{
  TEST_MIC_ACQUIRE = 1,
  TEST_MIC_PREPARE,
  TEST_MIC_START,
  TEST_MIC_STOP,
  TEST_MIC_DRAIN,
  TEST_MIC_RELEASE,
  TEST_DAC_ACQUIRE,
  TEST_DAC_PREPARE,
  TEST_DAC_START,
  TEST_DAC_WRITE,
  TEST_DAC_DRAIN,
  TEST_DAC_STOP,
  TEST_DAC_RELEASE,
};

struct test_audio_s
{
  enum test_stage_e trace[TEST_TRACE_MAX];
  size_t trace_count;
  enum test_stage_e fail_stage;
  unsigned int fail_count;
  bool partial_write;
  bool strict_release;
  bool mic_owned;
  bool mic_prepared;
  bool mic_started;
  bool dac_owned;
  bool dac_prepared;
  bool dac_started;
  bool overlap;
};

static int test_call(struct test_audio_s *audio, enum test_stage_e stage)
{
  assert(audio->trace_count < TEST_TRACE_MAX);
  audio->trace[audio->trace_count++] = stage;
  if (audio->fail_stage == stage && audio->fail_count != 0)
    {
      audio->fail_count--;
      return -EIO;
    }

  return 0;
}

static int test_mic_acquire(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_MIC_ACQUIRE);

  if (audio->dac_owned)
    {
      audio->overlap = true;
    }

  if (ret >= 0)
    {
      assert(!audio->mic_owned);
      audio->mic_owned = true;
    }

  return ret;
}

static int test_mic_prepare(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_MIC_PREPARE);

  assert(audio->mic_owned);
  if (ret >= 0)
    {
      audio->mic_prepared = true;
    }

  return ret;
}

static int test_mic_start(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_MIC_START);

  assert(audio->mic_prepared);
  if (ret >= 0)
    {
      audio->mic_started = true;
    }

  return ret;
}

static int test_mic_stop(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_MIC_STOP);

  assert(audio->mic_owned && audio->mic_started);
  if (ret >= 0)
    {
      audio->mic_started = false;
    }

  return ret;
}

static int test_mic_drain(void *context)
{
  struct test_audio_s *audio = context;

  assert(audio->mic_owned && audio->mic_prepared);
  return test_call(audio, TEST_MIC_DRAIN);
}

static int test_mic_release(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_MIC_RELEASE);

  assert(audio->mic_owned);
  if (ret >= 0 && audio->strict_release && audio->mic_started)
    {
      return -EBUSY;
    }

  if (ret >= 0)
    {
      audio->mic_owned = false;
      audio->mic_prepared = false;
      audio->mic_started = false;
    }

  return ret;
}

static int test_dac_acquire(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_DAC_ACQUIRE);

  if (audio->mic_owned)
    {
      audio->overlap = true;
    }

  if (ret >= 0)
    {
      assert(!audio->dac_owned);
      audio->dac_owned = true;
    }

  return ret;
}

static int test_dac_prepare(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_DAC_PREPARE);

  assert(audio->dac_owned);
  if (ret >= 0)
    {
      audio->dac_prepared = true;
    }

  return ret;
}

static int test_dac_start(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_DAC_START);

  assert(audio->dac_prepared);
  if (ret >= 0)
    {
      audio->dac_started = true;
    }

  return ret;
}

static ssize_t test_dac_write(void *context, const uint8_t *pcm,
                              size_t bytes)
{
  struct test_audio_s *audio = context;
  int ret;

  assert(audio->dac_started && pcm != NULL);
  ret = test_call(audio, TEST_DAC_WRITE);
  if (ret < 0)
    {
      return ret;
    }

  return audio->partial_write ? (ssize_t)(bytes - 1u) : (ssize_t)bytes;
}

static int test_dac_drain(void *context)
{
  struct test_audio_s *audio = context;

  assert(audio->dac_owned && audio->dac_prepared);
  return test_call(audio, TEST_DAC_DRAIN);
}

static int test_dac_stop(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_DAC_STOP);

  assert(audio->dac_owned && audio->dac_started);
  if (ret >= 0)
    {
      audio->dac_started = false;
    }

  return ret;
}

static int test_dac_release(void *context)
{
  struct test_audio_s *audio = context;
  int ret = test_call(audio, TEST_DAC_RELEASE);

  assert(audio->dac_owned);
  if (ret >= 0 && audio->strict_release && audio->dac_started)
    {
      return -EBUSY;
    }

  if (ret >= 0)
    {
      audio->dac_owned = false;
      audio->dac_prepared = false;
      audio->dac_started = false;
    }

  return ret;
}

static const struct bkvoice_turn_audio_ops_s g_test_ops =
{
  .mic_acquire = test_mic_acquire,
  .mic_prepare = test_mic_prepare,
  .mic_start = test_mic_start,
  .mic_stop = test_mic_stop,
  .mic_drain = test_mic_drain,
  .mic_release = test_mic_release,
  .dac_acquire = test_dac_acquire,
  .dac_prepare = test_dac_prepare,
  .dac_start = test_dac_start,
  .dac_write = test_dac_write,
  .dac_drain = test_dac_drain,
  .dac_stop = test_dac_stop,
  .dac_release = test_dac_release,
};

static const struct bkvoice_turn_limits_s g_test_limits =
{
  .capture_timeout_ms = 1000,
  .waiting_tts_timeout_ms = 2000,
  .playback_timeout_ms = 3000,
  .audio_frame_bytes = TEST_FRAME_BYTES,
};

static void test_initialize(struct bkvoice_turn_s *turn,
                            struct test_audio_s *audio)
{
  memset(audio, 0, sizeof(*audio));
  assert(bkvoice_turn_initialize(turn, &g_test_ops, audio,
                                 &g_test_limits, 7) == 0);
}

static struct bkvoice_turn_token_s test_event(
  const struct bkvoice_turn_token_s *active, uint32_t sequence)
{
  struct bkvoice_turn_token_s event = *active;

  event.sequence = sequence;
  return event;
}

static void test_assert_idle(const struct bkvoice_turn_s *turn,
                             const struct test_audio_s *audio)
{
  struct bkvoice_turn_snapshot_s snapshot;

  bkvoice_turn_snapshot(turn, &snapshot);
  assert(snapshot.state == BKVOICE_TURN_IDLE);
  assert(snapshot.active.turn_id == 0);
  assert(!snapshot.mic_acquired && !snapshot.mic_prepared &&
         !snapshot.mic_started);
  assert(!snapshot.dac_acquired && !snapshot.dac_prepared &&
         !snapshot.dac_started);
  assert(!audio->mic_owned && !audio->dac_owned && !audio->overlap);
}

static void test_normal_turn(void)
{
  static const enum test_stage_e expected[] =
  {
    TEST_MIC_ACQUIRE, TEST_MIC_PREPARE, TEST_MIC_START,
    TEST_MIC_STOP, TEST_MIC_DRAIN, TEST_MIC_RELEASE,
    TEST_DAC_ACQUIRE, TEST_DAC_PREPARE, TEST_DAC_START,
    TEST_DAC_WRITE, TEST_DAC_DRAIN, TEST_DAC_STOP, TEST_DAC_RELEASE,
  };
  struct bkvoice_turn_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  struct test_audio_s audio;
  uint8_t pcm[TEST_FRAME_BYTES] = {0};

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_session_open(&turn, 9) == 0);
  assert(bkvoice_turn_ptt_down(&turn, 9, 1, 100, &token) == 0);
  assert(token.boot_generation == 7 && token.session_id == 9 &&
         token.turn_id == 1 && token.sequence == 1);
  assert(turn.state == BKVOICE_TURN_CAPTURING);
  assert(turn.deadline_ms == 1100);

  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 200) == 0);
  assert(turn.state == BKVOICE_TURN_WAITING_TTS);
  assert(turn.deadline_ms == 2200);

  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 300) == 0);
  assert(turn.state == BKVOICE_TURN_PLAYING);
  assert(turn.deadline_ms == 3300);
  event.sequence = 2;
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm),
                                400) == 0);
  assert(turn.deadline_ms == 3400);
  event.sequence = 3;
  assert(bkvoice_turn_tts_end(&turn, &event) == 0);
  assert(audio.trace_count == sizeof(expected) / sizeof(expected[0]));
  assert(memcmp(audio.trace, expected, sizeof(expected)) == 0);
  test_assert_idle(&turn, &audio);

  bkvoice_turn_snapshot(&turn, &snapshot);
  assert(snapshot.last_error == 0);
  assert(snapshot.session_id == 9 && snapshot.last_session_id == 9);
  assert(snapshot.last_control_sequence == 2 &&
         snapshot.last_downlink_sequence == 3);
  assert(strcmp(bkvoice_turn_state_name(BKVOICE_TURN_IDLE), "idle") == 0);
  assert(strcmp(bkvoice_turn_state_name((enum bkvoice_turn_state_e)99),
                "invalid") == 0);
}

static void test_order_replay_and_cancel(void)
{
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  struct test_audio_s audio;
  uint8_t pcm[TEST_FRAME_BYTES] = {0};
  size_t trace_count;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_session_open(&turn, 4) == 0);
  assert(bkvoice_turn_ptt_down(&turn, 4, 1, 0, &token) == 0);
  trace_count = audio.trace_count;
  assert(bkvoice_turn_ptt_down(&turn, 4, 2, 0, &event) == -EBUSY);
  assert(audio.trace_count == trace_count);

  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 1) == -EPERM);
  assert(turn.last_downlink_sequence == 1);
  event = test_event(&token, 3);
  event.session_id++;
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -ESTALE);
  event.session_id--;
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -EALREADY);
  event.sequence = 1;
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -ESTALE);

  event = test_event(&token, 2);
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm),
                                1) == -EPERM);
  assert(turn.last_downlink_sequence == 2);
  event.sequence = 3;
  assert(bkvoice_turn_tts_start(&turn, &event, 1) == 0);
  assert(bkvoice_turn_tts_start(&turn, &event, 1) == -EALREADY);
  event.sequence = 5;
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm),
                                1) == -EPROTO);
  assert(turn.last_downlink_sequence == 3);
  assert(bkvoice_turn_ptt_down(&turn, 4, 4, 1, &event) == -EBUSY);

  event = test_event(&token, 5);
  assert(bkvoice_turn_cancel(&turn, &event, -ECANCELED) == 0);
  test_assert_idle(&turn, &audio);
  assert(bkvoice_turn_cancel(&turn, &event, -ECANCELED) == -ESTALE);
  trace_count = audio.trace_count;
  assert(bkvoice_turn_ptt_down(&turn, 4, 1, 2, &event) == -ESTALE);
  assert(bkvoice_turn_ptt_down(&turn, 4, 7, 2, &event) == -EPROTO);
  assert(audio.trace_count == trace_count);
  event = test_event(&token, 4);
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm),
                                2) == -ESTALE);
}

static void test_timeouts(void)
{
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_snapshot_s snapshot;
  struct bkvoice_turn_s turn;
  struct test_audio_s audio;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_timeout(&turn, 0) == -EALREADY);
  assert(bkvoice_turn_session_open(&turn, 1) == 0);
  assert(bkvoice_turn_ptt_down(&turn, 1, 1, 10, &token) == 0);
  assert(bkvoice_turn_timeout(&turn, 1009) == -EAGAIN);
  assert(bkvoice_turn_timeout(&turn, 1010) == 0);
  test_assert_idle(&turn, &audio);
  bkvoice_turn_snapshot(&turn, &snapshot);
  assert(snapshot.last_error == -ETIMEDOUT);

  assert(bkvoice_turn_ptt_down(&turn, 1, 2, 20, &token) == 0);
  event = test_event(&token, 3);
  assert(bkvoice_turn_ptt_up(&turn, &event, 30) == 0);
  assert(bkvoice_turn_timeout(&turn, 2030) == 0);
  test_assert_idle(&turn, &audio);

  assert(bkvoice_turn_ptt_down(&turn, 1, 4, 40, &token) == 0);
  event = test_event(&token, 5);
  assert(bkvoice_turn_ptt_up(&turn, &event, 50) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 60) == 0);
  assert(bkvoice_turn_timeout(&turn, 3060) == 0);
  test_assert_idle(&turn, &audio);

  assert(bkvoice_turn_ptt_down(&turn, 1, 6,
                               UINT64_MAX - 10u, &token) == 0);
  assert(turn.deadline_ms == UINT64_MAX);
  event = test_event(&token, 7);
  assert(bkvoice_turn_cancel(&turn, &event, 0) == 0);
}

static void test_mic_failures(void)
{
  const enum test_stage_e startup[] =
  {
    TEST_MIC_ACQUIRE, TEST_MIC_PREPARE, TEST_MIC_START,
  };
  const enum test_stage_e shutdown[] =
  {
    TEST_MIC_STOP, TEST_MIC_DRAIN, TEST_MIC_RELEASE,
  };
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  struct test_audio_s audio;
  size_t index;

  for (index = 0; index < sizeof(startup) / sizeof(startup[0]); index++)
    {
      test_initialize(&turn, &audio);
      assert(bkvoice_turn_session_open(&turn, 2) == 0);
      audio.fail_stage = startup[index];
      audio.fail_count = 1;
      assert(bkvoice_turn_ptt_down(&turn, 2, 1, 0, &token) == -EIO);
      test_assert_idle(&turn, &audio);
    }

  for (index = 0; index < sizeof(shutdown) / sizeof(shutdown[0]); index++)
    {
      test_initialize(&turn, &audio);
      assert(bkvoice_turn_session_open(&turn, 2) == 0);
      assert(bkvoice_turn_ptt_down(&turn, 2, 1, 0, &token) == 0);
      audio.fail_stage = shutdown[index];
      audio.fail_count = 1;
      event = test_event(&token, 2);
      assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -EIO);
      if (shutdown[index] == TEST_MIC_RELEASE)
        {
          assert(turn.state == BKVOICE_TURN_FAULTED);
          assert(audio.mic_owned);
          assert(bkvoice_turn_timeout(&turn, 10000) == -EIO);
          audio.fail_stage = 0;
          assert(bkvoice_turn_recover(&turn) == 0);
        }

      test_assert_idle(&turn, &audio);
    }

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_session_open(&turn, 2) == 0);
  assert(bkvoice_turn_ptt_down(&turn, 2, 1, 0, &token) == 0);
  audio.strict_release = true;
  audio.fail_stage = TEST_MIC_STOP;
  audio.fail_count = 1;
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -EIO);
  assert(turn.state == BKVOICE_TURN_FAULTED && audio.mic_owned);
  audio.strict_release = false;
  assert(bkvoice_turn_recover(&turn) == 0);
  test_assert_idle(&turn, &audio);

  assert(bkvoice_turn_recover(&turn) == -EALREADY);
}

static void test_prepare_playing(struct bkvoice_turn_s *turn,
                                 struct test_audio_s *audio,
                                 struct bkvoice_turn_token_s *token)
{
  struct bkvoice_turn_token_s event;

  test_initialize(turn, audio);
  assert(bkvoice_turn_session_open(turn, 3) == 0);
  assert(bkvoice_turn_ptt_down(turn, 3, 1, 0, token) == 0);
  event = test_event(token, 2);
  assert(bkvoice_turn_ptt_up(turn, &event, 1) == 0);
  event = test_event(token, 1);
  assert(bkvoice_turn_tts_start(turn, &event, 2) == 0);
}

static void test_dac_failures(void)
{
  const enum test_stage_e startup[] =
  {
    TEST_DAC_ACQUIRE, TEST_DAC_PREPARE, TEST_DAC_START,
  };
  const enum test_stage_e shutdown[] =
  {
    TEST_DAC_DRAIN, TEST_DAC_STOP, TEST_DAC_RELEASE,
  };
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  struct test_audio_s audio;
  uint8_t pcm[TEST_FRAME_BYTES] = {0};
  size_t index;

  for (index = 0; index < sizeof(startup) / sizeof(startup[0]); index++)
    {
      test_initialize(&turn, &audio);
      assert(bkvoice_turn_session_open(&turn, 3) == 0);
      assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
      event = test_event(&token, 2);
      assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
      audio.fail_stage = startup[index];
      audio.fail_count = 1;
      event = test_event(&token, 1);
      assert(bkvoice_turn_tts_start(&turn, &event, 2) == -EIO);
      test_assert_idle(&turn, &audio);
    }

  test_prepare_playing(&turn, &audio, &token);
  audio.fail_stage = TEST_DAC_WRITE;
  audio.fail_count = 1;
  event = test_event(&token, 2);
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm), 3) ==
         -EIO);
  test_assert_idle(&turn, &audio);

  test_prepare_playing(&turn, &audio, &token);
  audio.partial_write = true;
  event = test_event(&token, 2);
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm), 3) ==
         -EIO);
  test_assert_idle(&turn, &audio);

  for (index = 0; index < sizeof(shutdown) / sizeof(shutdown[0]); index++)
    {
      test_prepare_playing(&turn, &audio, &token);
      audio.fail_stage = shutdown[index];
      audio.fail_count = 1;
      event = test_event(&token, 2);
      assert(bkvoice_turn_tts_end(&turn, &event) == -EIO);
      if (shutdown[index] == TEST_DAC_RELEASE)
        {
          assert(turn.state == BKVOICE_TURN_FAULTED);
          assert(audio.dac_owned);
          audio.fail_stage = 0;
          assert(bkvoice_turn_recover(&turn) == 0);
        }

      test_assert_idle(&turn, &audio);
    }

  test_prepare_playing(&turn, &audio, &token);
  audio.strict_release = true;
  audio.fail_stage = TEST_DAC_STOP;
  audio.fail_count = 1;
  event = test_event(&token, 2);
  assert(bkvoice_turn_tts_end(&turn, &event) == -EIO);
  assert(turn.state == BKVOICE_TURN_FAULTED && audio.dac_owned);
  audio.strict_release = false;
  assert(bkvoice_turn_recover(&turn) == 0);
  test_assert_idle(&turn, &audio);
}

static void test_session_lifecycle(void)
{
  struct bkvoice_turn_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_s turn;
  struct test_audio_s audio;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_session_close(&turn, 0) == -EALREADY);
  assert(bkvoice_turn_session_open(&turn, 5) == 0);
  assert(bkvoice_turn_session_open(&turn, 6) == -EBUSY);
  assert(bkvoice_turn_ptt_down(&turn, 5, 1, 0, &token) == 0);
  assert(bkvoice_turn_session_close(&turn, -ENOTCONN) == 0);
  test_assert_idle(&turn, &audio);
  bkvoice_turn_snapshot(&turn, &snapshot);
  assert(snapshot.session_id == 0 && snapshot.last_session_id == 5);
  assert(snapshot.last_error == -ENOTCONN);
  assert(bkvoice_turn_ptt_down(&turn, 5, 2, 0, &token) == -ENOTCONN);
  assert(bkvoice_turn_session_open(&turn, 5) == -ESTALE);
  assert(bkvoice_turn_session_open(&turn, 4) == -ESTALE);
  assert(bkvoice_turn_session_open(&turn, 6) == 0);
  assert(bkvoice_turn_session_close(&turn, 0) == 0);
  turn.last_session_id = UINT32_MAX;
  assert(bkvoice_turn_session_open(&turn, 7) == -EOVERFLOW);
}

static void test_validation_and_overflow(void)
{
  struct bkvoice_turn_audio_ops_s bad_ops = g_test_ops;
  struct bkvoice_turn_limits_s bad_limits = g_test_limits;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  struct test_audio_s audio;
  uint8_t pcm[TEST_FRAME_BYTES] = {0};

  memset(&turn, 0, sizeof(turn));
  memset(&audio, 0, sizeof(audio));
  assert(bkvoice_turn_initialize(NULL, &g_test_ops, &audio,
                                 &g_test_limits, 7) == -EINVAL);
  bad_ops.dac_write = NULL;
  assert(bkvoice_turn_initialize(&turn, &bad_ops, &audio,
                                 &g_test_limits, 7) == -EINVAL);
  bad_limits.audio_frame_bytes = 0;
  assert(bkvoice_turn_initialize(&turn, &g_test_ops, &audio,
                                 &bad_limits, 7) == -EINVAL);

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 1, 1, 0, &token) == -ENOTCONN);
  assert(bkvoice_turn_session_open(&turn, 1) == 0);
  turn.last_turn_id = UINT32_MAX;
  assert(bkvoice_turn_ptt_down(&turn, 1, 1, 0, &token) == -EOVERFLOW);
  assert(turn.session_id == 0 && turn.last_error == -EOVERFLOW);
  test_assert_idle(&turn, &audio);

  turn.last_turn_id = 0;
  assert(bkvoice_turn_session_open(&turn, 2) == 0);
  assert(bkvoice_turn_ptt_down(&turn, 2, 1, 0, &token) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 2) == 0);
  event.sequence = 2;
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm,
                                sizeof(pcm) - 1u, 3) == -EMSGSIZE);
  assert(turn.last_downlink_sequence == 2);
  test_assert_idle(&turn, &audio);

  assert(bkvoice_turn_ptt_down(&turn, 2, 3, 4, &token) == 0);
  event = test_event(&token, 4);
  assert(bkvoice_turn_ptt_up(&turn, &event, 5) == 0);
  event = test_event(&token, 3);
  assert(bkvoice_turn_tts_start(&turn, &event, 6) == 0);
  event.sequence = 4;
  assert(bkvoice_turn_tts_audio(&turn, &event, NULL,
                                sizeof(pcm), 7) == -EINVAL);
  assert(turn.last_downlink_sequence == 4);
  test_assert_idle(&turn, &audio);

  turn.last_control_sequence = UINT32_MAX - 1u;
  assert(bkvoice_turn_ptt_down(&turn, 2, UINT32_MAX, 8, &token) ==
         -EOVERFLOW);
  assert(turn.session_id == 0 && turn.last_error == -EOVERFLOW);
  assert(!audio.mic_owned && !audio.dac_owned);

  assert(bkvoice_turn_session_open(&turn, 3) == 0);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 9, &token) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 10) == 0);
  turn.last_downlink_sequence = UINT32_MAX - 1u;
  event = test_event(&token, UINT32_MAX);
  assert(bkvoice_turn_tts_start(&turn, &event, 11) == -EOVERFLOW);
  assert(turn.session_id == 0 && turn.last_error == -EOVERFLOW);
  assert(!audio.mic_owned && !audio.dac_owned);
}

int main(void)
{
  test_normal_turn();
  test_order_replay_and_cancel();
  test_timeouts();
  test_mic_failures();
  test_dac_failures();
  test_session_lifecycle();
  test_validation_and_overflow();
  printf("BKVOICE_TURN_HOST_PASS\n");
  return 0;
}
