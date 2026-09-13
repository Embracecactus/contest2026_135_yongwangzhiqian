/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_ptt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bk7258_voice_ptt.h"

#define TEST_TRACE_MAX 64u

enum test_call_e
{
  TEST_MIC_ACQUIRE = 1,
  TEST_MIC_PREPARE,
  TEST_MIC_START,
  TEST_SOURCE_ATTACH,
  TEST_SINK_START,
  TEST_SINK_AUDIO,
  TEST_SOURCE_INTERRUPT,
  TEST_MIC_STOP,
  TEST_SOURCE_DETACH,
  TEST_MIC_DRAIN,
  TEST_MIC_RELEASE,
  TEST_SINK_END,
  TEST_SINK_CANCEL,
  TEST_DAC_ACQUIRE,
  TEST_DAC_PREPARE,
  TEST_DAC_START,
  TEST_DAC_WRITE,
  TEST_DAC_DRAIN,
  TEST_DAC_STOP,
  TEST_DAC_RELEASE,
};

struct test_trace_s
{
  enum test_call_e calls[TEST_TRACE_MAX];
  size_t count;
};

struct test_audio_s
{
  struct test_trace_s *trace;
  bool mic_owned;
  bool mic_prepared;
  bool mic_started;
  bool dac_owned;
  bool dac_prepared;
  bool dac_started;
};

struct test_source_s
{
  struct test_trace_s *trace;
  struct test_audio_s *audio;
  sem_t audio_sent;
  sem_t wake_reader;
  unsigned int reads;
  unsigned int interrupts;
  bool attached;
  bool wake_on_interrupt;
};

struct test_sink_s
{
  struct test_trace_s *trace;
  struct test_source_s *source;
  struct test_audio_s *audio;
  struct bkvoice_turn_token_s token;
  unsigned int audio_calls;
  unsigned int end_calls;
  unsigned int cancel_calls;
  uint8_t audio_markers[64];
  int audio_error;
  int cancel_reason;
};

struct test_prefill_s
{
  struct test_source_s *source;
  unsigned int read_calls;
  size_t fail_index;
  int error;
};

struct test_live_observer_s
{
  const struct test_sink_s *sink;
  unsigned int calls;
  uint8_t marker;
};

static const struct bkvoice_turn_limits_s g_turn_limits =
{
  .capture_timeout_ms = 1000,
  .waiting_tts_timeout_ms = 1000,
  .playback_timeout_ms = 1000,
  .audio_frame_bytes = BKVOICE_CAPTURE_FRAME_BYTES,
};

static void test_trace(struct test_trace_s *trace, enum test_call_e call)
{
  assert(trace->count < TEST_TRACE_MAX);
  trace->calls[trace->count++] = call;
}

static void test_sem_wait(sem_t *sem)
{
  struct timespec deadline;
  int ret;

  assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
  deadline.tv_sec += 2;
  do
    {
      ret = sem_timedwait(sem, &deadline);
    }
  while (ret < 0 && errno == EINTR);

  assert(ret == 0);
}

static int test_mic_acquire(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_MIC_ACQUIRE);
  assert(!audio->mic_owned && !audio->dac_owned);
  audio->mic_owned = true;
  return 0;
}

static int test_mic_prepare(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_MIC_PREPARE);
  assert(audio->mic_owned && !audio->mic_prepared);
  audio->mic_prepared = true;
  return 0;
}

static int test_mic_start(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_MIC_START);
  assert(audio->mic_prepared && !audio->mic_started);
  audio->mic_started = true;
  return 0;
}

static int test_mic_stop(void *context)
{
  struct test_audio_s *audio = context;

  assert(audio->mic_owned);
  if (audio->mic_started)
    {
      test_trace(audio->trace, TEST_MIC_STOP);
      audio->mic_started = false;
    }

  return 0;
}

static int test_mic_drain(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_MIC_DRAIN);
  assert(audio->mic_owned && audio->mic_prepared && !audio->mic_started);
  return 0;
}

static int test_mic_release(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_MIC_RELEASE);
  assert(audio->mic_owned && !audio->mic_started);
  audio->mic_owned = false;
  audio->mic_prepared = false;
  return 0;
}

static int test_dac_acquire(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_DAC_ACQUIRE);
  assert(!audio->mic_owned && !audio->dac_owned);
  audio->dac_owned = true;
  return 0;
}

static int test_dac_prepare(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_DAC_PREPARE);
  audio->dac_prepared = true;
  return 0;
}

static int test_dac_start(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_DAC_START);
  audio->dac_started = true;
  return 0;
}

static ssize_t test_dac_write(void *context, const uint8_t *pcm,
                              size_t bytes)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_DAC_WRITE);
  assert(audio->dac_started && pcm != NULL);
  return (ssize_t)bytes;
}

static int test_dac_drain(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_DAC_DRAIN);
  return 0;
}

static int test_dac_stop(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_DAC_STOP);
  audio->dac_started = false;
  return 0;
}

static int test_dac_release(void *context)
{
  struct test_audio_s *audio = context;

  test_trace(audio->trace, TEST_DAC_RELEASE);
  audio->dac_owned = false;
  audio->dac_prepared = false;
  return 0;
}

static const struct bkvoice_turn_audio_ops_s g_audio_ops =
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

static int test_source_attach(void *context)
{
  struct test_source_s *source = context;

  test_trace(source->trace, TEST_SOURCE_ATTACH);
  assert(!source->attached && source->audio->mic_started);
  source->attached = true;
  return 0;
}

static ssize_t test_source_read(void *context, void *pcm, size_t bytes)
{
  struct test_source_s *source = context;
  int ret;

  assert(source->attached && pcm != NULL && bytes != 0);
  source->reads++;
  if (source->reads == 1)
    {
      assert(bytes == BKVOICE_CAPTURE_FRAME_BYTES);
      memset(pcm, 0x5a, bytes);
      return (ssize_t)bytes;
    }

  do
    {
      ret = sem_wait(&source->wake_reader);
    }
  while (ret < 0 && errno == EINTR);

  assert(ret == 0);
  return -EPIPE;
}

static int test_source_interrupt(void *context)
{
  struct test_source_s *source = context;

  test_trace(source->trace, TEST_SOURCE_INTERRUPT);
  assert(source->attached);
  source->interrupts++;
  (void)test_mic_stop(source->audio);
  if (source->wake_on_interrupt)
    {
      assert(sem_post(&source->wake_reader) == 0);
    }

  return 0;
}

static int test_source_detach(void *context)
{
  struct test_source_s *source = context;

  test_trace(source->trace, TEST_SOURCE_DETACH);
  assert(source->attached);
  source->attached = false;
  return 0;
}

static const struct bkvoice_capture_source_ops_s g_source_ops =
{
  .attach = test_source_attach,
  .read = test_source_read,
  .interrupt = test_source_interrupt,
  .detach = test_source_detach,
};

static int test_sink_start(void *context,
                           const struct bkvoice_turn_token_s *token)
{
  struct test_sink_s *sink = context;

  test_trace(sink->trace, TEST_SINK_START);
  memcpy(&sink->token, token, sizeof(*token));
  return 0;
}

static int test_sink_audio(void *context,
                           const struct bkvoice_turn_token_s *token,
                           const uint8_t *pcm, size_t bytes)
{
  struct test_sink_s *sink = context;

  test_trace(sink->trace, TEST_SINK_AUDIO);
  assert(memcmp(token, &sink->token, sizeof(*token)) == 0);
  assert(pcm != NULL && bytes == BKVOICE_CAPTURE_FRAME_BYTES);
  assert(sink->audio_calls < sizeof(sink->audio_markers));
  sink->audio_markers[sink->audio_calls] = pcm[0];
  sink->audio_calls++;
  assert(sem_post(&sink->source->audio_sent) == 0);
  return sink->audio_error;
}

static int test_prefill_read(
  void *context, size_t frame_index,
  uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES])
{
  struct test_prefill_s *prefill = context;

  assert(prefill != NULL && prefill->source != NULL && pcm != NULL);
  assert(prefill->source->attached && prefill->source->reads == 0);
  assert(frame_index == prefill->read_calls);
  prefill->read_calls++;
  if (prefill->error < 0 && frame_index == prefill->fail_index)
    {
      return prefill->error;
    }

  memset(pcm, (int)(0xa0u + frame_index),
         BKVOICE_CAPTURE_FRAME_BYTES);
  return 0;
}

static void test_live_observe(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES])
{
  struct test_live_observer_s *observer = context;

  assert(observer != NULL && observer->sink != NULL && token != NULL &&
         pcm != NULL);
  assert(memcmp(token, &observer->sink->token, sizeof(*token)) == 0);
  observer->calls++;
  observer->marker = pcm[0];
}

static int test_sink_end(void *context,
                         const struct bkvoice_turn_token_s *token)
{
  struct test_sink_s *sink = context;

  test_trace(sink->trace, TEST_SINK_END);
  assert(memcmp(token, &sink->token, sizeof(*token)) == 0);
  assert(!sink->audio->mic_owned);
  sink->end_calls++;
  return 0;
}

static int test_sink_cancel(void *context,
                            const struct bkvoice_turn_token_s *token,
                            int reason)
{
  struct test_sink_s *sink = context;

  test_trace(sink->trace, TEST_SINK_CANCEL);
  assert(memcmp(token, &sink->token, sizeof(*token)) == 0);
  assert(reason < 0);
  sink->cancel_calls++;
  sink->cancel_reason = reason;
  return 0;
}

static const struct bkvoice_capture_sink_ops_s g_sink_ops =
{
  .start = test_sink_start,
  .audio = test_sink_audio,
  .end = test_sink_end,
  .cancel = test_sink_cancel,
};

static void test_initialize(struct bkvoice_ptt_s *ptt,
                            struct test_trace_s *trace,
                            struct test_audio_s *audio,
                            struct test_source_s *source,
                            struct test_sink_s *sink,
                            uint32_t join_timeout_ms)
{
  const struct bkvoice_ptt_worker_config_s worker_config =
  {
    .stack_size = 0,
    .priority = 0,
    .join_timeout_ms = join_timeout_ms,
  };

  memset(ptt, 0, sizeof(*ptt));
  memset(trace, 0, sizeof(*trace));
  memset(audio, 0, sizeof(*audio));
  memset(source, 0, sizeof(*source));
  memset(sink, 0, sizeof(*sink));
  audio->trace = trace;
  source->trace = trace;
  source->audio = audio;
  source->wake_on_interrupt = true;
  sink->trace = trace;
  sink->source = source;
  sink->audio = audio;
  assert(sem_init(&source->audio_sent, 0, 0) == 0);
  assert(sem_init(&source->wake_reader, 0, 0) == 0);
  assert(bkvoice_ptt_initialize(ptt, &g_audio_ops, audio, &g_source_ops,
                                source, &g_turn_limits, &worker_config,
                                7) == 0);
  assert(bkvoice_ptt_session_open(ptt, 3, &g_sink_ops, sink) == 0);
}

static void test_destroy(struct bkvoice_ptt_s *ptt,
                         struct test_source_s *source)
{
  assert(bkvoice_ptt_uninitialize(ptt) == 0);
  assert(sem_destroy(&source->audio_sent) == 0);
  assert(sem_destroy(&source->wake_reader) == 0);
}

static void test_finish(struct bkvoice_ptt_s *ptt,
                        struct test_source_s *source)
{
  assert(bkvoice_ptt_session_close(ptt, -ENOTCONN) == 0);
  test_destroy(ptt, source);
}

static void test_normal_ptt_order(void)
{
  static const enum test_call_e expected[] =
  {
    TEST_MIC_ACQUIRE, TEST_MIC_PREPARE, TEST_MIC_START,
    TEST_SOURCE_ATTACH, TEST_SINK_START, TEST_SINK_AUDIO,
    TEST_SOURCE_INTERRUPT, TEST_MIC_STOP, TEST_SOURCE_DETACH,
    TEST_MIC_DRAIN, TEST_MIC_RELEASE, TEST_SINK_END,
  };
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_down(&ptt, 11, &token) == -EBUSY);
  assert(bkvoice_ptt_up(&ptt, 20) == 0);
  assert(trace.count == sizeof(expected) / sizeof(expected[0]));
  assert(memcmp(trace.calls, expected, sizeof(expected)) == 0);
  assert(sink.audio_calls == 1 && sink.end_calls == 1 &&
         sink.cancel_calls == 0);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_WAITING_TTS);
  assert(snapshot.capture.state == BKVOICE_CAPTURE_IDLE);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_finish(&ptt, &source);
}

static void test_quiescent_ota_admission_state(void)
{
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  memset(&ptt, 0, sizeof(ptt));
  assert(!bkvoice_ptt_quiescent(&ptt));
  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  /* A cloud-open PTT session has capture_ready set, but owns no hardware. */
  assert(bkvoice_ptt_quiescent(&ptt));

  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(!bkvoice_ptt_quiescent(&ptt));
  assert(bkvoice_ptt_up(&ptt, 20) == 0);
  assert(!bkvoice_ptt_quiescent(&ptt)); /* WAITING_TTS owns the turn. */

  token = ptt.token;
  token.sequence = 1;
  assert(bkvoice_turn_tts_start(&ptt.turn, &token, 30) == 0);
  assert(!bkvoice_ptt_quiescent(&ptt)); /* PLAYING owns the DAC. */
  assert(bkvoice_ptt_cancel(&ptt, -ECANCELED) == 0);
  assert(bkvoice_ptt_quiescent(&ptt));

  ptt.turn.state = BKVOICE_TURN_FAULTED;
  assert(!bkvoice_ptt_quiescent(&ptt));
  assert(bkvoice_turn_recover(&ptt.turn) == 0);
  assert(bkvoice_ptt_quiescent(&ptt));
  ptt.worker_joinable = true;
  assert(!bkvoice_ptt_quiescent(&ptt));
  ptt.worker_joinable = false;
  assert(bkvoice_ptt_quiescent(&ptt));
  test_finish(&ptt, &source);
}

static void test_prefill_order(void)
{
  static const enum test_call_e expected[] =
  {
    TEST_MIC_ACQUIRE, TEST_MIC_PREPARE, TEST_MIC_START,
    TEST_SOURCE_ATTACH, TEST_SINK_START,
    TEST_SINK_AUDIO, TEST_SINK_AUDIO, TEST_SINK_AUDIO,
    TEST_SOURCE_INTERRUPT, TEST_MIC_STOP, TEST_SOURCE_DETACH,
    TEST_MIC_DRAIN, TEST_MIC_RELEASE, TEST_SINK_END,
  };
  static const uint8_t expected_audio[] = {0xa0, 0xa1, 0x5a};
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct test_live_observer_s observer;
  struct test_prefill_s prefill;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  memset(&prefill, 0, sizeof(prefill));
  memset(&observer, 0, sizeof(observer));
  prefill.source = &source;
  observer.sink = &sink;
  assert(bkvoice_ptt_down_prefill(&ptt, 10, test_prefill_read,
                                   &prefill, 2, test_live_observe,
                                   &observer, &token) == 0);
  test_sem_wait(&source.audio_sent);
  test_sem_wait(&source.audio_sent);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_up(&ptt, 20) == 0);
  assert(trace.count == sizeof(expected) / sizeof(expected[0]));
  assert(memcmp(trace.calls, expected, sizeof(expected)) == 0);
  assert(memcmp(sink.audio_markers, expected_audio,
                sizeof(expected_audio)) == 0);
  assert(observer.calls == 1 && observer.marker == 0x5a);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.capture.frames_sent == 3);
  assert(snapshot.capture.prefill_frames_sent == 2);
  assert(snapshot.capture.bytes_sent ==
         3u * BKVOICE_CAPTURE_FRAME_BYTES);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_finish(&ptt, &source);
}

static void test_prefill_failure_cleans_up(void)
{
  static const enum test_call_e expected[] =
  {
    TEST_MIC_ACQUIRE, TEST_MIC_PREPARE, TEST_MIC_START,
    TEST_SOURCE_ATTACH, TEST_SINK_START, TEST_SINK_AUDIO,
    TEST_SOURCE_INTERRUPT, TEST_MIC_STOP, TEST_SOURCE_DETACH,
    TEST_SINK_CANCEL, TEST_MIC_DRAIN, TEST_MIC_RELEASE,
  };
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct test_prefill_s prefill;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  memset(&prefill, 0, sizeof(prefill));
  prefill.source = &source;
  prefill.fail_index = 1;
  prefill.error = -EIO;
  assert(bkvoice_ptt_down_prefill(&ptt, 10, test_prefill_read,
                                   &prefill, 2, NULL, NULL,
                                   &token) == -EIO);
  assert(trace.count == sizeof(expected) / sizeof(expected[0]));
  assert(memcmp(trace.calls, expected, sizeof(expected)) == 0);
  assert(sink.audio_calls == 1 && sink.audio_markers[0] == 0xa0);
  assert(sink.cancel_calls == 1 && sink.cancel_reason == -EIO);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(snapshot.capture.state == BKVOICE_CAPTURE_IDLE);
  assert(snapshot.capture.prefill_frames_sent == 1);
  assert(snapshot.session_open && snapshot.capture_ready);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_finish(&ptt, &source);
}

static void test_worker_failure_cancels(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  sink.audio_error = -ENETDOWN;
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_up(&ptt, 20) == -ENETDOWN);
  assert(sink.audio_calls == 1 && sink.end_calls == 0 &&
         sink.cancel_calls == 1);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(snapshot.capture.state == BKVOICE_CAPTURE_IDLE);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_finish(&ptt, &source);
}

static void test_join_timeout_is_retryable(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 20);
  source.wake_on_interrupt = false;
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_up(&ptt, 20) == -ETIMEDOUT);
  assert(!bkvoice_ptt_quiescent(&ptt));
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.worker_joinable && snapshot.capture.source_attached);
  assert(snapshot.turn.mic_acquired && source.attached);

  source.wake_on_interrupt = true;
  assert(bkvoice_ptt_up(&ptt, 30) == 0);
  assert(!bkvoice_ptt_quiescent(&ptt));
  assert(source.interrupts == 2 && sink.end_calls == 1);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(!snapshot.worker_joinable && !snapshot.turn.mic_acquired &&
         !snapshot.capture.source_attached);
  assert(bkvoice_ptt_cancel(&ptt, -ECANCELED) == 0);
  assert(bkvoice_ptt_quiescent(&ptt));
  test_finish(&ptt, &source);
}

static void test_cancel_active_capture(void)
{
  static const enum test_call_e expected[] =
  {
    TEST_MIC_ACQUIRE, TEST_MIC_PREPARE, TEST_MIC_START,
    TEST_SOURCE_ATTACH, TEST_SINK_START, TEST_SINK_AUDIO,
    TEST_SOURCE_INTERRUPT, TEST_MIC_STOP, TEST_SOURCE_DETACH,
    TEST_SINK_CANCEL, TEST_MIC_DRAIN, TEST_MIC_RELEASE,
  };
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_cancel(&ptt, -ENOTCONN) == 0);
  assert(trace.count == sizeof(expected) / sizeof(expected[0]));
  assert(memcmp(trace.calls, expected, sizeof(expected)) == 0);
  assert(sink.end_calls == 0 && sink.cancel_calls == 1);
  assert(sink.cancel_reason == -ENOTCONN);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(snapshot.turn.last_error == -ENOTCONN);
  assert(snapshot.capture.state == BKVOICE_CAPTURE_IDLE);
  assert(snapshot.session_open && snapshot.capture_ready);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_finish(&ptt, &source);
}

static void test_cancel_join_timeout_is_retryable(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 20);
  source.wake_on_interrupt = false;
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_cancel(&ptt, -ENOTCONN) == -ETIMEDOUT);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.worker_joinable && snapshot.capture.source_attached);
  assert(snapshot.turn.mic_acquired && source.attached);

  source.wake_on_interrupt = true;
  assert(bkvoice_ptt_cancel(&ptt, -ENOTCONN) == 0);
  assert(source.interrupts == 2 && sink.cancel_calls == 1);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(!snapshot.worker_joinable && !snapshot.turn.mic_acquired &&
         !snapshot.capture.source_attached);
  test_finish(&ptt, &source);
}

static void test_capture_timeout(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_timeout(&ptt, 1009) == -EAGAIN);
  assert(source.interrupts == 0);
  assert(bkvoice_ptt_timeout(&ptt, 1010) == 0);
  assert(sink.end_calls == 0 && sink.cancel_calls == 1);
  assert(sink.cancel_reason == -ETIMEDOUT);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(snapshot.turn.last_error == -ETIMEDOUT);
  assert(snapshot.capture.state == BKVOICE_CAPTURE_IDLE);
  assert(snapshot.session_open && snapshot.capture_ready);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_finish(&ptt, &source);
}

static void test_waiting_tts_timeout(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_up(&ptt, 20) == 0);
  assert(bkvoice_ptt_timeout(&ptt, 1019) == -EAGAIN);
  assert(bkvoice_ptt_timeout(&ptt, 1020) == 0);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(snapshot.turn.last_error == -ETIMEDOUT);
  assert(sink.end_calls == 1 && sink.cancel_calls == 0);
  assert(snapshot.session_open && snapshot.capture_ready);
  test_finish(&ptt, &source);
}

static void test_session_close_stops_active_capture(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_session_close(&ptt, -ENOTCONN) == 0);
  assert(sink.end_calls == 0 && sink.cancel_calls == 1);
  assert(sink.cancel_reason == -ENOTCONN);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(!snapshot.session_open && !snapshot.capture_ready);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_destroy(&ptt, &source);
}

static void test_session_close_join_timeout_is_retryable(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 20);
  source.wake_on_interrupt = false;
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_session_close(&ptt, -ENOTCONN) == -ETIMEDOUT);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.session_open && snapshot.capture_ready);
  assert(snapshot.worker_joinable && snapshot.capture.source_attached);

  source.wake_on_interrupt = true;
  assert(bkvoice_ptt_session_close(&ptt, -ENOTCONN) == 0);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(!snapshot.session_open && !snapshot.capture_ready);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_destroy(&ptt, &source);
}

static void test_control_sequence_overflow_is_terminal(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  ptt.turn.last_control_sequence = UINT32_MAX - 2u;
  assert(bkvoice_ptt_down(&ptt, 10, &token) == 0);
  assert(token.sequence == UINT32_MAX - 1u);
  test_sem_wait(&source.audio_sent);
  assert(bkvoice_ptt_up(&ptt, 20) == -EOVERFLOW);
  assert(sink.end_calls == 0 && sink.cancel_calls == 1);
  assert(sink.cancel_reason == -EOVERFLOW);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(snapshot.turn.last_error == -EOVERFLOW);
  assert(!snapshot.session_open && !snapshot.capture_ready);
  assert(!snapshot.worker_joinable && !audio.mic_owned && !source.attached);
  test_destroy(&ptt, &source);
}

static void test_idle_control_sequence_overflow_is_terminal(void)
{
  struct bkvoice_ptt_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct bkvoice_ptt_s ptt;
  struct test_trace_s trace;
  struct test_audio_s audio;
  struct test_source_s source;
  struct test_sink_s sink;

  test_initialize(&ptt, &trace, &audio, &source, &sink, 1000);
  ptt.turn.last_control_sequence = UINT32_MAX - 1u;
  assert(bkvoice_ptt_down(&ptt, 10, &token) == -EOVERFLOW);
  bkvoice_ptt_snapshot(&ptt, &snapshot);
  assert(snapshot.turn.state == BKVOICE_TURN_IDLE);
  assert(snapshot.turn.last_error == -EOVERFLOW);
  assert(!snapshot.session_open && !snapshot.capture_ready);
  assert(!snapshot.worker_joinable && trace.count == 0);
  test_destroy(&ptt, &source);
}

static void test_guards(void)
{
  struct bkvoice_ptt_worker_config_s config =
  {
    .stack_size = 0,
    .priority = 0,
    .join_timeout_ms = 0,
  };
  struct bkvoice_ptt_s ptt;
  struct test_audio_s audio;
  struct test_source_s source;

  memset(&ptt, 0, sizeof(ptt));
  memset(&audio, 0, sizeof(audio));
  memset(&source, 0, sizeof(source));
  assert(bkvoice_ptt_initialize(NULL, &g_audio_ops, &audio, &g_source_ops,
                                &source, &g_turn_limits, &config, 7) ==
         -EINVAL);
  assert(bkvoice_ptt_initialize(&ptt, &g_audio_ops, &audio, &g_source_ops,
                                &source, &g_turn_limits, &config, 7) ==
         -EINVAL);
  assert(bkvoice_ptt_up(&ptt, 0) == -EINVAL);
}

int main(void)
{
  test_normal_ptt_order();
  test_quiescent_ota_admission_state();
  test_prefill_order();
  test_prefill_failure_cleans_up();
  test_worker_failure_cancels();
  test_join_timeout_is_retryable();
  test_cancel_active_capture();
  test_cancel_join_timeout_is_retryable();
  test_capture_timeout();
  test_waiting_tts_timeout();
  test_session_close_stops_active_capture();
  test_session_close_join_timeout_is_retryable();
  test_control_sequence_overflow_is_terminal();
  test_idle_control_sequence_overflow_is_terminal();
  test_guards();
  puts("BKVOICE_PTT_HOST_PASS");
  return 0;
}
