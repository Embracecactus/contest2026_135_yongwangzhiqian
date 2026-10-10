/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_fixture.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_media = PTHREAD_COND_INITIALIZER;
static bool g_media_started;
static unsigned int g_active;
static enum bkcloud_fixture_mode_e g_mode;
static struct bkcloud_fixture_report_s g_report;

static void scan(struct bkcloud_fixture_ctx_s *ctx, const uint8_t *data, size_t n)
{
  for (size_t i = 0; i < n; i++)
    {
      ctx->source_hash = (ctx->source_hash * 16777619u) ^ data[i];
      if (ctx->scan_size < sizeof(ctx->scan) - 1) ctx->scan[ctx->scan_size++] = data[i];
      else { memmove(ctx->scan, ctx->scan + 1, sizeof(ctx->scan) - 2);
             ctx->scan[sizeof(ctx->scan) - 2] = data[i]; }
      ctx->scan[ctx->scan_size] = 0;
      if (strstr(ctx->scan, "\"stream\":true")) ctx->stream = true;
    }
}

static int make_reply(struct bkcloud_fixture_ctx_s *ctx)
{
  const char *body;
  char recognized[256];
  if (ctx->role == BKCLOUD_FIXTURE_ASR)
    {
      static const char *const focus[] =
        {"开始专注60秒", "专注还剩多久？", "暂停专注", "继续专注",
         "取消当前专注计时", "开始专注1秒"};
      const char *text = ctx->mode >= BKCLOUD_FIXTURE_FOCUS_START &&
                         ctx->mode <= BKCLOUD_FIXTURE_FOCUS_FINISH ?
                         focus[ctx->mode - BKCLOUD_FIXTURE_FOCUS_START] :
                         "固定识别文本";
      snprintf(recognized, sizeof(recognized),
        "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\","
        "\"message\":{\"content\":\"%s\",\"tool_calls\":[]}}]}", text);
      body = recognized;
    }
  else if (ctx->role == BKCLOUD_FIXTURE_TTS)
    {
      const char *quad = (g_report.tts_requests & 1u) ? "AgAC" : "AQAB";
      const char *last = (g_report.tts_requests & 1u) ? "AgA=" : "AQA=";
      size_t at = (size_t)snprintf(ctx->reply, sizeof(ctx->reply),
        "data: {\"choices\":[{\"index\":0,\"delta\":{\"audio\":{\"data\":\"");
      for (unsigned int i = 0; i < 2730; i++)
        { memcpy(ctx->reply + at, quad, 4); at += 4; }
      memcpy(ctx->reply + at, last, 4); at += 4;
      at += (size_t)snprintf(ctx->reply + at, sizeof(ctx->reply) - at,
        "\"}},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
      char header[80];
      int h = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: %zu\r\n\r\n", at);
      if (h < 0 || at + (size_t)h >= sizeof(ctx->reply)) return -E2BIG;
      memmove(ctx->reply + h, ctx->reply, at); memcpy(ctx->reply, header, (size_t)h);
      ctx->reply_size = at + (size_t)h; goto counted;
    }
  else if (!ctx->stream)
    body = "{\"choices\":[{\"index\":0,\"finish_reason\":\"tool_calls\",\"message\":{\"content\":\"\",\"tool_calls\":[{\"id\":\"fixture-final\",\"type\":\"function\",\"function\":{\"name\":\"agent_finalize\",\"arguments\":\"{}\"}}]}}]}";
  else
    body = "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"这是第一句。\"}}]}\n\n"
           "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"这是尾句\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
  int n = snprintf(ctx->reply, sizeof(ctx->reply),
    "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n%s",
    ctx->role == BKCLOUD_FIXTURE_LLM && ctx->stream ? "text/event-stream" :
    "application/json", strlen(body), body);
  if (n < 0 || (size_t)n >= sizeof(ctx->reply)) return -E2BIG;
  ctx->reply_size = (size_t)n;
  const char *tail = ctx->role == BKCLOUD_FIXTURE_LLM && ctx->stream ?
    strstr(strstr(ctx->reply, "data:") + 5, "data:") : NULL;
  ctx->tail_offset = tail ? (size_t)(tail - ctx->reply) : ctx->reply_size;
counted:
  pthread_mutex_lock(&g_lock);
  if (ctx->role == BKCLOUD_FIXTURE_ASR) g_report.asr_requests++;
  else if (ctx->role == BKCLOUD_FIXTURE_TTS) g_report.tts_requests++;
  else if (ctx->stream) g_report.final_requests++;
  else g_report.plan_requests++;
  g_report.source_bytes += ctx->sent;
  g_report.source_hash ^= ctx->source_hash;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

static int open_peer(void *arg, const char *host, uint16_t port, uint64_t due)
{
  struct bkcloud_fixture_ctx_s *ctx = arg;
  (void)host; (void)port; (void)due;
  if (!ctx || ctx->opened) return -EINVAL;
  ctx->offset = ctx->sent = ctx->scan_size = 0; ctx->reply_ready = false;
  ctx->stream = false; ctx->source_hash = 2166136261u;
  pthread_mutex_lock(&g_lock); ctx->mode = g_mode; pthread_mutex_unlock(&g_lock);
  atomic_store(&ctx->canceled, false);
  ctx->opened = true; pthread_mutex_lock(&g_lock); g_active++;
  pthread_mutex_unlock(&g_lock); return 0;
}
static ssize_t send_peer(void *arg, const uint8_t *data, size_t bytes, uint64_t due)
{
  struct bkcloud_fixture_ctx_s *ctx = arg; (void)due;
  if (!ctx || !ctx->opened || atomic_load(&ctx->canceled)) return -ECANCELED;
  if (!data || !bytes) return -EINVAL;
  if (bytes > 19) bytes = 19; /* require caller framing/body fragmentation */
  scan(ctx, data, bytes); ctx->sent += bytes;
  return (ssize_t)bytes;
}
static int wait_media(struct bkcloud_fixture_ctx_s *ctx)
{
  pthread_mutex_lock(&g_lock);
  struct timespec limit; clock_gettime(CLOCK_REALTIME, &limit); limit.tv_sec += 5;
  while ((!g_media_started || ctx->mode == BKCLOUD_FIXTURE_CANCEL_TAIL) &&
         !atomic_load(&ctx->canceled))
    if (pthread_cond_timedwait(&g_media, &g_lock, &limit) == ETIMEDOUT) break;
  bool ready = g_media_started, canceled = atomic_load(&ctx->canceled);
  if (ready && ctx->mode == BKCLOUD_FIXTURE_NORMAL)
    g_report.tail_released_after_media = true;
  pthread_mutex_unlock(&g_lock);
  return canceled ? -ECANCELED : ready && ctx->mode == BKCLOUD_FIXTURE_NORMAL ? 0 : -ETIMEDOUT;
}
static ssize_t recv_peer(void *arg, uint8_t *data, size_t bytes, uint64_t due)
{
  struct bkcloud_fixture_ctx_s *ctx = arg; (void)due;
  if (!ctx || !ctx->opened || !data) return -EINVAL;
  if (!ctx->reply_ready) { int ret = make_reply(ctx); if (ret) return ret; ctx->reply_ready = true; }
  /* Do not expose final SSE tail before Media reports the first actual write. */
  if (ctx->role == BKCLOUD_FIXTURE_LLM && ctx->stream &&
      ctx->offset >= ctx->tail_offset)
    { int ret = wait_media(ctx); if (ret) return ret; }
  if (atomic_load(&ctx->canceled)) return -ECANCELED;
  size_t left = ctx->reply_size - ctx->offset;
  if (!left) return 0;
  if (bytes > left) bytes = left;
  if (ctx->role == BKCLOUD_FIXTURE_LLM && ctx->stream &&
      ctx->offset < ctx->tail_offset && bytes > ctx->tail_offset - ctx->offset)
    bytes = ctx->tail_offset - ctx->offset;
  if (bytes > 11) bytes = 11;
  memcpy(data, ctx->reply + ctx->offset, bytes); ctx->offset += bytes;
  return (ssize_t)bytes;
}
static int interrupt_peer(void *arg) { return bkcloud_fixture_cancel(arg); }
static int close_peer(void *arg) { struct bkcloud_fixture_ctx_s *ctx = arg;
  if (!ctx || !ctx->opened) return -EINVAL;
  ctx->opened = false;
  pthread_mutex_lock(&g_lock); g_active--; pthread_mutex_unlock(&g_lock); return 0; }
static const struct bkvoice_wss_tls_ops_s g_ops = {
  open_peer, send_peer, recv_peer, interrupt_peer, close_peer, NULL, NULL };

int bkcloud_fixture_begin(struct bkcloud_fixture_ctx_s *ctx,
  enum bkcloud_fixture_role_e role, enum bkcloud_fixture_mode_e mode)
{ if (!ctx || role > BKCLOUD_FIXTURE_TTS) return -EINVAL; memset(ctx, 0, sizeof(*ctx));
  ctx->role = role; ctx->mode = mode; ctx->source_hash = 2166136261u; return 0; }
const struct bkvoice_wss_tls_ops_s *bkcloud_fixture_tls_ops(void) { return &g_ops; }
int bkcloud_fixture_media_started(void)
{ pthread_mutex_lock(&g_lock); g_media_started = true;
  pthread_cond_broadcast(&g_media); pthread_mutex_unlock(&g_lock); return 0; }
int bkcloud_fixture_cancel(struct bkcloud_fixture_ctx_s *ctx)
{ if (!ctx) return -EINVAL; pthread_mutex_lock(&g_lock); atomic_store(&ctx->canceled, true);
  pthread_cond_broadcast(&g_media); pthread_mutex_unlock(&g_lock); return 0; }
int bkcloud_fixture_end(struct bkcloud_fixture_ctx_s *ctx)
{ return ctx ? (ctx->opened ? -EBUSY : 0) : -EINVAL; }
int bkcloud_fixture_reset(enum bkcloud_fixture_mode_e mode)
{ if (mode > BKCLOUD_FIXTURE_FOCUS_FINISH) return -EINVAL;
  pthread_mutex_lock(&g_lock); if (g_active) { pthread_mutex_unlock(&g_lock); return -EBUSY; }
  memset(&g_report, 0, sizeof(g_report)); g_media_started = false; g_mode = mode;
  pthread_mutex_unlock(&g_lock); return 0; }
int bkcloud_fixture_report(struct bkcloud_fixture_report_s *report)
{ if (!report) return -EINVAL; pthread_mutex_lock(&g_lock); *report = g_report;
  pthread_mutex_unlock(&g_lock); return 0; }
int bkcloud_fixture_pcm_expectation(struct bkcloud_fixture_pcm_expectation_s *out)
{ if (!out) return -EINVAL; *out = (struct bkcloud_fixture_pcm_expectation_s)
  { { 8192, 8192 }, { 0, 0 }, { 5462, 5462 }, { 0, 0 } };
  out->source_hash[0] = 0; out->source_hash[1] = 0;
  for (size_t j = 0; j < 2; j++) {
    uint32_t h = 2166136261u;
    uint8_t value = j ? 2 : 1;
    for (size_t i = 0; i < 8192; i++) { uint8_t b = i % 3 == 1 ? 0 : value; h ^= b; h *= 16777619u; }
    out->source_hash[j] = h;
    uint32_t r = 2166136261u;
    int16_t pending[3];
    for (size_t sample = 0; sample < 4096; sample++) {
      uint8_t lo = (sample * 2) % 3 == 1 ? 0 : value;
      uint8_t hi = (sample * 2 + 1) % 3 == 1 ? 0 : value;
      pending[sample % 3] = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
      if (sample % 3 == 2) {
        int16_t output[2] = { pending[0], (int16_t)(((int32_t)pending[1] + pending[2]) / 2) };
        for (size_t x = 0; x < 2; x++) for (size_t b = 0; b < 2; b++)
          { r ^= ((uint16_t)output[x] >> (8 * b)) & 0xff; r *= 16777619u; }
      }
    }
    /* 4096 source samples leaves one pending sample after 1365 triples. */
    int16_t last = (int16_t)((uint16_t)value);
    r ^= (uint8_t)last; r *= 16777619u; r ^= (uint8_t)((uint16_t)last >> 8); r *= 16777619u;
    out->expected_16k_hash[j] = r;
  } return 0; }
