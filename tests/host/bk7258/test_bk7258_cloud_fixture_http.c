/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_audio.h"
#include "bk7258_cloud_fixture.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static struct bkcloud_config_s config;
static size_t pcm_bytes;
static uint32_t pcm_hash;
static int pcm(void *unused, const void *data, size_t size)
{ (void)unused; const uint8_t *p = data; for (size_t i = 0; i < size; i++)
  { pcm_hash ^= p[i]; pcm_hash *= 16777619u; } pcm_bytes += size; return 0; }
static void setup(void)
{ memset(&config, 0, sizeof(config)); config.dialect = 2; config.port = 443;
  strcpy(config.host, "fixture.invalid"); strcpy(config.base_path, "/v1");
  strcpy(config.api_key, "fixture-only"); strcpy(config.asr_model, "fixture-asr");
  strcpy(config.tts_model, "fixture-tts"); strcpy(config.tts_voice, "mimo_default"); }
static int stream(void *ctx, const void *data, size_t size)
{ (void)data; (void)size; unsigned *calls = ctx; if (++*calls == 1) assert(bkcloud_fixture_media_started() == 0); return 0; }
static int body(void *buffer, size_t *size, const void **data, size_t wanted, void *ctx)
{ (void)wanted; const char *text = ctx; assert(*size >= strlen(text)); memcpy(buffer, text, strlen(text)); *data = buffer; *size = strlen(text); return 0; }
struct cancel_s { pthread_mutex_t lock; pthread_cond_t changed; bool first; struct bkcloud_fixture_ctx_s *ctx; int result; };
static int wait_stream(void *arg, const void *data, size_t size)
{ (void)data; (void)size; struct cancel_s *s = arg; pthread_mutex_lock(&s->lock);
  s->first = true; pthread_cond_signal(&s->changed); pthread_mutex_unlock(&s->lock); return 0; }
static void *cancel_worker(void *arg)
{ struct cancel_s *s = arg; struct bkcloud_http_s http;
  s->result = bkcloud_http_events(&http, &config, bkcloud_fixture_tls_ops(), s->ctx,
    1, "{\"stream\":true}", 15, wait_stream, s, 4096); return NULL; }
int main(void)
{
  struct bkcloud_fixture_ctx_s asr, tts, llm;
  struct bkcloud_client_s client;
  struct bkcloud_tts_s decoder;
  struct bkcloud_fixture_pcm_expectation_s expected;
  unsigned char input[640] = {0}; char text[64]; unsigned calls = 0;
  setup(); assert(bkcloud_fixture_reset(BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_fixture_begin(&asr, BKCLOUD_FIXTURE_ASR, BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_recognize(&client, &config, bkcloud_fixture_tls_ops(), &asr, 1,
      input, sizeof(input), text, sizeof(text)) == 0 && !strcmp(text, "固定识别文本"));
  assert(bkcloud_fixture_begin(&tts, BKCLOUD_FIXTURE_TTS, BKCLOUD_FIXTURE_NORMAL) == 0);
  pcm_bytes = 0; pcm_hash = 2166136261u;
  assert(bkcloud_synthesize(&client, &decoder, &config, bkcloud_fixture_tls_ops(),
      &tts, 1, "第一句", pcm, NULL) == 0 && pcm_bytes == 8192);
  assert(bkcloud_fixture_pcm_expectation(&expected) == 0 && pcm_hash == expected.source_hash[0]);
  assert(bkcloud_fixture_begin(&tts, BKCLOUD_FIXTURE_TTS, BKCLOUD_FIXTURE_NORMAL) == 0);
  pcm_bytes = 0; pcm_hash = 2166136261u;
  assert(bkcloud_synthesize(&client, &decoder, &config, bkcloud_fixture_tls_ops(),
      &tts, 1, "第二句", pcm, NULL) == 0 && pcm_hash == expected.source_hash[1]);
  assert(bkcloud_fixture_begin(&llm, BKCLOUD_FIXTURE_LLM, BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_http_events(&client.http, &config, bkcloud_fixture_tls_ops(), &llm,
      1, "{\"stream\":true}", 15, stream, &calls, 4096) == 0 && calls > 1);
  char reply[512] = {0};
  assert(bkcloud_fixture_begin(&llm, BKCLOUD_FIXTURE_LLM, BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_http_post(&client.http, &config, "chat/completions",
      bkcloud_fixture_tls_ops(), &llm, 1, body, "{\"stream\":false}", 16,
      reply, sizeof(reply)) == 0 && strstr(reply, "agent_finalize"));

  assert(bkcloud_fixture_reset(BKCLOUD_FIXTURE_CANCEL_TAIL) == 0);
  struct cancel_s cancel = { .lock = PTHREAD_MUTEX_INITIALIZER,
    .changed = PTHREAD_COND_INITIALIZER, .ctx = &llm, .result = -1 };
  pthread_t worker; assert(pthread_create(&worker, NULL, cancel_worker, &cancel) == 0);
  pthread_mutex_lock(&cancel.lock); while (!cancel.first)
    assert(pthread_cond_wait(&cancel.changed, &cancel.lock) == 0);
  pthread_mutex_unlock(&cancel.lock);
  assert(bkcloud_fixture_media_started() == 0);
  assert(bkcloud_fixture_cancel(&llm) == 0);
  assert(pthread_join(worker, NULL) == 0 && cancel.result == -ECANCELED);
  struct bkcloud_fixture_report_s report;
  assert(bkcloud_fixture_report(&report) == 0 && !report.tail_released_after_media);
  assert(bkcloud_fixture_end(&llm) == 0);
  assert(bkcloud_fixture_reset(BKCLOUD_FIXTURE_NORMAL) == 0);
  calls = 0;
  assert(bkcloud_http_events(&client.http, &config, bkcloud_fixture_tls_ops(), &llm,
      1, "{\"stream\":true}", 15, stream, &calls, 4096) == 0 && calls > 1);
  puts("CONTRACT_PASS");
  return 0;
}
