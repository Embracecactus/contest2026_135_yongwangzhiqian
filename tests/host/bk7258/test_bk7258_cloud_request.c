/* SPDX-License-Identifier: Apache-2.0 */
#ifdef TEST_AGENT_FINAL_STREAM
#include "llm/llm_stream.h"
#include "llm/llm_proxy.h"
#include "infra/http_proxy.h"
#include "infra/vela_tls.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char spoken[256];
static size_t spoken_size;
static int reject_delta;
/* No network is used: both built-in routes must remain unreachable. */
bool http_proxy_is_enabled(void) { return false; }
proxy_conn_t *proxy_conn_open(const char *host, int port, int timeout)
{ (void)host; (void)port; (void)timeout; abort(); }
int proxy_conn_write(proxy_conn_t *c, const char *p, int n)
{ (void)c; (void)p; (void)n; abort(); }
int proxy_conn_read(proxy_conn_t *c, char *p, int n, int timeout)
{ (void)c; (void)p; (void)n; (void)timeout; abort(); }
void proxy_conn_close(proxy_conn_t *c) { (void)c; abort(); }
int vela_https_post_json(const char *host, const char *port, const char *path,
    const vela_header_t *headers, const char *body, char *out, size_t cap)
{ (void)host; (void)port; (void)path; (void)headers; (void)body;
  (void)out; (void)cap; abort(); }
int vela_http_post_json(const char *host, const char *port, const char *path,
    const vela_header_t *headers, const char *body, char *out, size_t cap)
{ return vela_https_post_json(host, port, path, headers, body, out, cap); }

static const char *plan_finish, *plan_calls;
static int plan_transport(const char *request, char *response, size_t capacity,
    size_t *length, int *status, void *context,
    int (*check)(void *), void *request_context)
{
  (void)context;
  assert(strstr(request, "\"messages\""));
  if (check && check(request_context)) return -ECANCELED;
  int n = snprintf(response, capacity,
      "{\"choices\":[{\"finish_reason\":\"%s\",\"message\":{"
      "\"content\":\"draft\"%s}}]}", plan_finish, plan_calls);
  assert(n > 0 && (size_t)n < capacity);
  *length = (size_t)n; *status = 200;
  return 0;
}

static void test_plan_phase(void)
{
  assert(llm_set_transport("fixture", "fixture", plan_transport, NULL, NULL) == 0);
  cJSON *messages = cJSON_CreateArray();
  assert(messages);
  llm_response_t response;
  const char *invalid[] = { "length", "content_filter", "stop", "tool_calls" };
  plan_calls = "";
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    plan_finish = invalid[i];
    assert(llm_chat_plan_checked("system", messages, "[]", &response,
        NULL, NULL) == -EPROTO);
    assert(!response.text && !response.call_count);
    llm_response_free(&response);
  }
  plan_finish = "stop";
  assert(llm_chat_tools_checked("system", messages, NULL, &response,
      NULL, NULL) == 0);
  assert(!strcmp(response.text, "draft"));
  llm_response_free(&response);
  plan_finish = "end_turn"; /* Legacy synchronous provider compatibility. */
  assert(llm_chat_tools_checked("system", messages, NULL, &response,
      NULL, NULL) == 0);
  llm_response_free(&response);
  plan_finish = "tool_calls";
  plan_calls = ",\"tool_calls\":[{\"id\":\"one\",\"type\":\"function\","
      "\"function\":{\"name\":\"get_weather\",\"arguments\":\"{}\"}}]";
  assert(llm_chat_plan_checked("system", messages, "[]", &response,
      NULL, NULL) == 0);
  assert(response.tool_phase_complete && response.call_count == 1 &&
      !strcmp(response.calls[0].name, "get_weather"));
  llm_response_free(&response);
  plan_calls = ",\"tool_calls\":[{\"id\":\"final\",\"type\":\"function\","
      "\"function\":{\"name\":\"agent_finalize\",\"arguments\":\"{}\"}}]";
  assert(llm_chat_plan_checked("system", messages, "[]", &response,
      NULL, NULL) == 0);
  assert(response.tool_phase_complete && response.call_count == 1 &&
      !strcmp(response.calls[0].name, "agent_finalize"));
  llm_response_free(&response);
  cJSON_Delete(messages);
  assert(llm_clear_transport() == 0);
}
static int body_delta(void *context, const char *text, size_t length)
{
  (void)context;
  if (reject_delta) return -ECANCELED;
  assert(length < sizeof(spoken) - spoken_size);
  memcpy(spoken + spoken_size, text, length);
  spoken_size += length;
  spoken[spoken_size] = 0;
  return 0;
}

static llm_final_stream_t *fresh(size_t limit)
{
  spoken_size = 0;
  spoken[0] = 0;
  reject_delta = 0;
  llm_final_stream_t *p = llm_final_stream_new(body_delta, NULL, limit);
  assert(p);
  return p;
}

int main(void)
{
  test_plan_phase();
  const char first[] = "data: {\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"reasoning_content\":\"NEVER_SPEAK\"}}]}\r\n\r\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"你好。\"}}]}\n\n";
  const char tail[] = "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"再见\"},\"finish_reason\":\"stop\"}]}\n\n"
    "data: {\"choices\":[],\"usage\":{\"total_tokens\":3}}\n\ndata: [DONE]\n\n";
  for (size_t chunk = 1; chunk <= strlen(first); chunk++)
    {
      llm_final_stream_t *p = fresh(255);
      for (size_t at = 0; at < strlen(first); at += chunk)
        {
          size_t n = strlen(first) - at;
          if (n > chunk) n = chunk;
          assert(llm_final_stream_feed(p, first + at, n) == 0);
        }
      assert(!strcmp(spoken, "你好。")); /* Delivered before stop/DONE. */
      char *text = NULL;
      assert(llm_final_stream_finish(p, &text) == -EPROTO && !text);
      assert(llm_final_stream_feed(p, tail, strlen(tail)) == 0);
      assert(llm_final_stream_finish(p, &text) == 0);
      assert(!strcmp(text, "你好。再见") && !strcmp(text, spoken));
      free(text);
      llm_final_stream_free(p);
    }
  const char *bad[] = {
    "data: [DONE]\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[]}}]}\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"length\"}]}\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"content_filter\"}]}\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"x\\u0000y\"}}]}\n\n",
    "data: {\"choices\":[{\"index\":1,\"delta\":{\"content\":\"x\"}}]}\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"\xc0\xaf\"}}]}\n\n"
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
      llm_final_stream_t *p = fresh(255);
      assert(llm_final_stream_feed(p, bad[i], strlen(bad[i])) < 0);
      assert(!spoken_size);
      char *text = NULL;
      assert(llm_final_stream_finish(p, &text) < 0 && !text);
      llm_final_stream_free(p);
    }
  llm_final_stream_t *p = fresh(255);
  reject_delta = 1;
  assert(llm_final_stream_feed(p, first, strlen(first)) == -ECANCELED);
  assert(llm_final_stream_feed(p, tail, strlen(tail)) == -ECANCELED);
  llm_final_stream_free(p);
  p = fresh(2);
  assert(llm_final_stream_feed(p, first, strlen(first)) == -E2BIG);
  llm_final_stream_free(p);
  p = fresh(255);
  char oversized[16385];
  memset(oversized, 'x', sizeof(oversized));
  assert(llm_final_stream_feed(p, oversized, sizeof(oversized)) == -E2BIG);
  llm_final_stream_free(p);
  puts("final-body SSE: planning rejects length/filter/draft, ordinary sync and tool/finalize retained, fragmented UTF-8, early delta, reasoning/tool exclusion, EOF, bounds and cancellation PASS");
  return 0;
}
#elif defined(TEST_AGENT_ENDPOINT)
#include "voice/voice_endpoint.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
  /* Source-independent synthetic signal tests validate timing, not CER. */
  static int16_t pcm[48000];
  for (unsigned int i = 0; i < 48000; i++)
    {
      int speech = (i >= 3200 && i < 16000) || (i >= 25600 && i < 30400);
      pcm[i] = (i & 1 ? 1 : -1) * (speech ? 80 : 8);
    }
  uint64_t reference = 0;
  for (size_t chunk = 1; chunk <= 1024; chunk *= 2)
    {
      voice_endpoint_t state;
      assert(!voice_endpoint_init(&state, 16000, 900));
      int ret = 0;
      for (size_t off = 0; off < 48000 && ret != 2; off += chunk)
        {
          size_t n = 48000 - off;
          if (n > chunk) n = chunk;
          ret = voice_endpoint_feed(&state, pcm + off, n);
          assert(ret >= 0);
          if (off < 30400) assert(ret != 2); /* 600 ms middle pause retained. */
        }
      assert(ret == 2);
      assert(state.samples == 44800); /* Exactly 900 ms after final speech. */
      if (!reference) reference = state.samples;
      assert(reference == state.samples);
    }
  voice_endpoint_t state;
  assert(!voice_endpoint_init(&state, 16000, 900));
  for (size_t i = 0; i < 48000; i++) pcm[i] = 1000; /* DC is not speech. */
  assert(voice_endpoint_feed(&state, pcm, 48000) == 0);
  assert(!state.started);
  assert(voice_endpoint_init(&state, 16000, 200) < 0);
  assert(voice_endpoint_init(&state, 0, 900) < 0);
  puts("sample-clock endpoint: fragment invariance, quiet speech, middle pause, DC, bounded tail PASS (not CER)");
  return 0;
}
#else
#include "bk7258_cloud_request.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int emit(void *context, const void *data, size_t size)
{
  size_t *total = context;
  assert(size <= 1024);
  *total += size;
  return fwrite(data, 1, size, stdout) == size ? 0 : -EIO;
}

static int cancel(void *context, const void *data, size_t size)
{
  unsigned int *calls = context;
  (void)data; (void)size;
  return ++*calls == 1 ? -ECANCELED : 0;
}

int main(int argc, char **argv)
{
  if (argc == 5)
    {
      assert(!strcmp(argv[1], "image"));
      size_t jpeg_size = strtoul(argv[2], NULL, 10);
      size_t chunk = strtoul(argv[4], NULL, 10);
      char prefix[256];
      const char suffix[] = "\"}}]}]}";
      struct bkcloud_image_source_s source;
      assert(jpeg_size >= 4 && jpeg_size <= BKCLOUD_JPEG_MAX);
      int length = snprintf(prefix, sizeof(prefix),
        "{\"model\":\"%s\",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"describe\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,",
        argv[3]);
      assert(length > 0 && (size_t)length < sizeof(prefix));
      uint8_t *jpeg = malloc(jpeg_size);
      assert(jpeg != NULL);
      memset(jpeg, 0x5a, jpeg_size);
      jpeg[0] = 0xff; jpeg[1] = 0xd8;
      jpeg[jpeg_size - 2] = 0xff; jpeg[jpeg_size - 1] = 0xd9;
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, jpeg_size) == 0);
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, 3) == -EINVAL);
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, BKCLOUD_JPEG_MAX + 1) == -EINVAL);
      jpeg[0] = 0;
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, jpeg_size) == -EINVAL);
      jpeg[0] = 0xff;
      jpeg[jpeg_size - 1] = 0;
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, jpeg_size) == -EINVAL);
      jpeg[jpeg_size - 1] = 0xd9;
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, jpeg_size) == 0);
      size_t total = 0;
      char buffer[1024];
      while (source.offset < source.length)
        {
          size_t count = sizeof(buffer);
          const void *data;
          assert(bkcloud_image_body(buffer, &count, &data, chunk, &source) == 0);
          assert(count > 0 && count <= chunk);
          assert(emit(&total, data, count) == 0);
        }
      bkcloud_image_source_clear(&source);
      for (size_t i = 0; i < sizeof(source); i++)
        assert(((unsigned char *)&source)[i] == 0);
      free(jpeg);
      return 0;
    }
  assert(argc == 4);
  const char *model = argv[2];
  const char good[] = "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\","
    "\"message\":{\"content\":\"  reply\\n\"}}]}";
  const char *bad[] = {
    "{}", "{\"choices\":[]}",
    "{\"choices\":[{\"index\":0,\"finish_reason\":\"length\",\"message\":{\"content\":\"x\"}}]}",
    "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\",\"message\":{\"content\":\"x\\u0000y\"}}]}",
    "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\",\"message\":{\"content\":\"x\",\"tool_calls\":[{}]}}]}"
  };
  char output[32];
  assert(bkcloud_text_parse(good, strlen(good), output, sizeof(output)) == 0);
  assert(strcmp(output, "reply") == 0);
  assert(bkcloud_text_parse(good, strlen(good), output, 5) == -ENOSPC);
  assert(output[0] == 0);
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
      memset(output, 'x', sizeof(output));
      assert(bkcloud_text_parse(bad[i], strlen(bad[i]), output, sizeof(output)) < 0);
      for (size_t n = 0; n < sizeof(output); n++) assert(output[n] == 0);
    }
  for (size_t n = 0; n < strlen(good); n++)
    assert(bkcloud_text_parse(good, n, output, sizeof(output)) < 0);
  char trailing[sizeof(good) + 4];
  snprintf(trailing, sizeof(trailing), "%sx", good);
  assert(bkcloud_text_parse(trailing, strlen(trailing), output, sizeof(output)) < 0);
  size_t expected, total = 0;
  assert(bkcloud_asr_size(model, 0, &expected) < 0);
  assert(bkcloud_asr_size(model, 3, &expected) < 0);
  assert(bkcloud_asr_size(model, BKCLOUD_PCM_MAX + 2, &expected) < 0);
  assert(bkcloud_asr_size("bad\"model", 2, &expected) < 0);
  size_t size = strtoul(argv[1], NULL, 10);
  assert(bkcloud_asr_size(model, size, &expected) == 0);
  uint8_t *pcm = malloc(size);
  assert(pcm != NULL);
  for (size_t i = 0; i < size; i++) pcm[i] = i % 251;
  unsigned int calls = 0;
  assert(bkcloud_asr_write(model, pcm, size, cancel, &calls) == -ECANCELED);
  assert(calls == 1);
  size_t chunk = strtoul(argv[3], NULL, 10);
  if (chunk == 0)
    assert(bkcloud_asr_write(model, pcm, size, emit, &total) == 0);
  else
    {
      struct bkcloud_asr_source_s source;
      assert(bkcloud_asr_source_init(&source, model, pcm, size) == 0);
      char buffer[1024];
      while (source.offset < source.length)
        {
          size_t count = sizeof(buffer);
          const void *data;
          assert(bkcloud_asr_body(buffer, &count, &data, chunk, &source) == 0);
          assert(count > 0 && count <= chunk);
          assert(emit(&total, data, count) == 0);
        }
      size_t count = sizeof(buffer);
      const void *data;
      assert(bkcloud_asr_body(buffer, &count, &data, chunk, &source) == 0);
      assert(count == 0);
      bkcloud_asr_source_clear(&source);
      for (size_t i = 0; i < sizeof(source); i++)
        assert(((unsigned char *)&source)[i] == 0);
    }
  assert(total == expected);
  free(pcm);
  return 0;
}
#endif
