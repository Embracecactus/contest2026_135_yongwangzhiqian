/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_fixture.h"
#include "bk7258_cloud_tts.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

static void send_all(struct bkcloud_fixture_ctx_s *ctx, const char *text)
{
  const struct bkvoice_wss_tls_ops_s *ops = bkcloud_fixture_tls_ops();
  for (size_t at = 0; at < strlen(text);)
    {
      ssize_t n = ops->send(ctx, (const uint8_t *)text + at, strlen(text) - at, 1);
      assert(n > 0); at += (size_t)n;
    }
}
static size_t read_until(struct bkcloud_fixture_ctx_s *ctx, char *out,
                         size_t size, const char *needle)
{
  const struct bkvoice_wss_tls_ops_s *ops = bkcloud_fixture_tls_ops();
  size_t used = 0;
  while (!strstr(out, needle))
    {
      assert(used + 12 < size);
      ssize_t n = ops->recv(ctx, (uint8_t *)out + used, size - used - 1, 1);
      assert(n > 0); used += (size_t)n; out[used] = 0;
    }
  return used;
}
static void close_ctx(struct bkcloud_fixture_ctx_s *ctx)
{ assert(bkcloud_fixture_tls_ops()->close(ctx) == 0); assert(bkcloud_fixture_end(ctx) == 0); }
static size_t pcm_bytes;
static uint32_t pcm_hash;
static int pcm_sink(void *unused, const void *data, size_t size)
{ (void)unused; const uint8_t *p = data; for (size_t i = 0; i < size; i++)
  { pcm_hash ^= p[i]; pcm_hash *= 16777619u; } pcm_bytes += size; return 0; }
static void plan_once(struct bkcloud_fixture_ctx_s *ctx, char *text, size_t size)
{
  assert(bkcloud_fixture_tls_ops()->open_verified(ctx, "fixture.invalid", 443, 1) == 0);
  send_all(ctx, "POST / HTTP/1.1\r\n\r\n{\"stream\":false}");
  memset(text, 0, size); read_until(ctx, text, size, "agent_finalize");
  assert(!strstr(text, "data:")); close_ctx(ctx);
}

int main(void)
{
  struct bkcloud_fixture_ctx_s final, asr, tts;
  struct bkcloud_fixture_report_s report;
  struct bkcloud_fixture_pcm_expectation_s pcm;
  char text[13000] = {0};
  assert(bkcloud_fixture_reset(BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_fixture_begin(&final, BKCLOUD_FIXTURE_LLM, BKCLOUD_FIXTURE_NORMAL) == 0);
  plan_once(&final, text, sizeof(text));
  assert(bkcloud_fixture_tls_ops()->open_verified(&final, "fixture.invalid", 443, 1) == 0);
  send_all(&final, "POST / HTTP/1.1\r\n\r\n{\"stream\":true}");
  read_until(&final, text, sizeof(text), "这是第一句。");
  assert(!strstr(text, "这是尾句"));
  assert(bkcloud_fixture_media_started() == 0);
  read_until(&final, text, sizeof(text), "[DONE]");
  assert(strstr(text, "这是尾句")); close_ctx(&final);

  assert(bkcloud_fixture_begin(&asr, BKCLOUD_FIXTURE_ASR, BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_fixture_tls_ops()->open_verified(&asr, "fixture.invalid", 443, 1) == 0);
  send_all(&asr, "POST / HTTP/1.1\r\n\r\n{}"); memset(text, 0, sizeof(text));
  read_until(&asr, text, sizeof(text), "固定识别文本");
  size_t used = strlen(text);
  for (;;) {
    ssize_t n = bkcloud_fixture_tls_ops()->recv(&asr, (uint8_t *)text + used,
        sizeof(text) - used - 1, 1);
    if (n == 0) break;
    assert(n > 0); used += (size_t)n; text[used] = 0;
  }
  char asr_text[64]; char *asr_body = strstr(text, "\r\n\r\n");
  assert(asr_body && bkcloud_text_parse(asr_body + 4, strlen(asr_body + 4),
      asr_text, sizeof(asr_text)) == 0 && !strcmp(asr_text, "固定识别文本"));
  close_ctx(&asr);

  assert(bkcloud_fixture_begin(&tts, BKCLOUD_FIXTURE_TTS, BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_fixture_tls_ops()->open_verified(&tts, "fixture.invalid", 443, 1) == 0);
  send_all(&tts, "POST / HTTP/1.1\r\n\r\n{}"); memset(text, 0, sizeof(text));
  read_until(&tts, text, sizeof(text), "[DONE]"); assert(strstr(text, "AQABAQAB"));
  struct bkcloud_tts_s decoder; pcm_bytes = 0; pcm_hash = 2166136261u;
  bkcloud_tts_init(&decoder, pcm_sink, NULL);
  char *body = strstr(text, "\r\n\r\n"); assert(body); body += 4;
  for (size_t at = 0; at < strlen(body); at += 13) {
    size_t n = strlen(body) - at; if (n > 13) n = 13;
    assert(bkcloud_tts_feed(&decoder, body + at, n) == 0);
  }
  assert(bkcloud_tts_finish(&decoder) == 0 && pcm_bytes == 8192);
  assert(bkcloud_fixture_pcm_expectation(&pcm) == 0);
  assert(pcm.source_bytes[0] == 8192 && pcm.expected_16k_bytes[1] == 5462);
  assert(pcm_hash == pcm.source_hash[0]);
  bkcloud_tts_clear(&decoder); close_ctx(&tts);
  assert(bkcloud_fixture_tls_ops()->open_verified(&tts, "fixture.invalid", 443, 1) == 0);
  send_all(&tts, "POST / HTTP/1.1\r\n\r\n{}"); memset(text, 0, sizeof(text));
  read_until(&tts, text, sizeof(text), "[DONE]"); assert(strstr(text, "AgACAgAC"));
  pcm_bytes = 0; pcm_hash = 2166136261u; bkcloud_tts_init(&decoder, pcm_sink, NULL);
  body = strstr(text, "\r\n\r\n"); assert(body); body += 4;
  for (size_t at = 0; at < strlen(body); at += 17) {
    size_t n = strlen(body) - at; if (n > 17) n = 17;
    assert(bkcloud_tts_feed(&decoder, body + at, n) == 0);
  }
  assert(bkcloud_tts_finish(&decoder) == 0 && pcm_bytes == 8192 &&
         pcm_hash == pcm.source_hash[1]);
  bkcloud_tts_clear(&decoder); close_ctx(&tts);
  assert(bkcloud_fixture_report(&report) == 0);
  assert(report.asr_requests == 1 && report.final_requests == 1 && report.tts_requests == 2);
  assert(report.tail_released_after_media && report.source_bytes > 0);
  /* A canceled request never releases the held tail as a successful stream. */
  assert(bkcloud_fixture_reset(BKCLOUD_FIXTURE_CANCEL_TAIL) == 0);
  plan_once(&final, text, sizeof(text));
  assert(bkcloud_fixture_tls_ops()->open_verified(&final, "fixture.invalid", 443, 1) == 0);
  send_all(&final, "POST / HTTP/1.1\r\n\r\n{\"stream\":true}");
  memset(text, 0, sizeof(text));
  read_until(&final, text, sizeof(text), "这是第一句。");
  assert(bkcloud_fixture_cancel(&final) == 0);
  assert(bkcloud_fixture_tls_ops()->recv(&final, (uint8_t *)text, 1, 1) == -ECANCELED);
  close_ctx(&final);
  assert(bkcloud_fixture_report(&report) == 0 && !report.tail_released_after_media);
  assert(bkcloud_fixture_reset(BKCLOUD_FIXTURE_NORMAL) == 0);
  plan_once(&final, text, sizeof(text));
  assert(bkcloud_fixture_tls_ops()->open_verified(&final, "fixture.invalid", 443, 1) == 0);
  send_all(&final, "POST / HTTP/1.1\r\n\r\n{\"stream\":true}");
  memset(text, 0, sizeof(text)); read_until(&final, text, sizeof(text), "这是第一句。");
  assert(bkcloud_fixture_media_started() == 0); read_until(&final, text, sizeof(text), "[DONE]");
  close_ctx(&final);
  return 0;
}
