/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include "bk7258_engineering_test.h"
#include "bk7258_cloud_config.h"
#include "bk7258_provision_settings.h"

/* Exercise the actual product adapter with transport/provider boundaries
 * replaced. A discovery window without a phone is the HIL baseline. */
#define BKVOICE_CONFIG_MAX_BYTES 8192u
static bool g_cloud_loaded, g_configured, g_identity_bound, g_control_bound;
static bool g_benchmark_cloud, g_trigger_started;
static atomic_bool g_benchmark_clear, g_agent_core_ready, g_voice_initialized;
static atomic_uint g_benchmark_session;
static int g_probe_result, g_service_result, g_product_error;
static struct { uint8_t record[64]; size_t certificate_size, key_size; }
  g_identity = {{0}, 1, 1};
static bool network_busy, pairing;
static uint32_t peer_generation;
static int configure_calls;
#ifdef CONFIG_BK7258_TTS_ISOLATION
typedef struct { char channel[16]; char *content; uint64_t request_id; }
  agent_msg_t;
static agent_msg_t outbound;
static int submitted_text, submitted_pcm, isolated_speech, focus_dispatch;
static int product_tts_isolation_text(uint64_t, const char *, int (*)(uint64_t));
static int request_status(uint64_t request) { return request == 123 ? 0 : -ESTALE; }
static uint64_t voice_channel_request_id(void) { return 123; }
static int voice_channel_speak_reply(uint64_t request, const char *text)
{ assert(request == 123 && !strcmp(text, "fixed")); isolated_speech++; return 0; }
static int product_focus_text(uint64_t request, const char *text,
                               int (*check)(uint64_t))
{ (void)request; (void)text; (void)check; focus_dispatch++; return 0; }
static int message_bus_push_outbound(agent_msg_t *message)
{ assert(!outbound.content); outbound = *message; return 0; }
#endif
static bool bkprov_network_busy(void) { return network_busy; }
static bool __attribute__((unused)) bkprov_owner_pairing(void) { return pairing; }
static uint32_t __attribute__((unused)) bkprov_gatt_generation(void)
{ return peer_generation; }
static bool voice_channel_is_idle(void) { return true; }
static bool bkagent_ota_busy(void) { return false; }
static int bkagent_cloud_clear(void) { return 0; }
static int bkagent_cloud_verify_service(void) { return 0; }
static int bk7258_agent_trigger_stop(void) { return 0; }
static int bk7258_agent_power_status(void *context, uint32_t *state,
                                     int32_t *error)
{ (void)context; *state = 0; *error = 0; return 0; }
static int voice_channel_submit_text_checked(const char *text,
                                             int (*check)(void *), void *ctx)
{
#ifdef CONFIG_BK7258_TTS_ISOLATION
  submitted_text++;
  int ret = check(ctx);
  if (ret) return ret;
  ret = product_tts_isolation_text(123, text, request_status);
  if (ret == 1)
    {
      unsigned int session = atomic_exchange(&g_benchmark_session, 0);
      bkengvoice_finish(session, 123, 0);
      return 0;
    }
  return ret;
#else
  (void)text; return check(ctx);
#endif
}
static int voice_channel_submit_pcm_checked(const uint8_t *data, size_t size,
                                            int (*check)(void *), void *ctx)
{
#ifdef CONFIG_BK7258_TTS_ISOLATION
  submitted_pcm++;
#endif
  (void)data; (void)size; return check(ctx);
}
static void mbedtls_platform_zeroize(void *p, size_t size) { memset(p, 0, size); }
int bkcloud_config_decode(struct bkcloud_config_s *cloud,
                          const void *data, size_t size)
{ (void)data; assert(size == 24); strcpy(cloud->host, "fixture.invalid"); return 0; }
int bkprov_settings_voice(const struct bkprov_settings_s *settings,
                          const uint8_t *cert, size_t cert_size,
                          const uint8_t *key, size_t key_size,
                          void *out, size_t capacity, size_t *size)
{
  (void)settings; (void)cert; (void)key;
  assert(cert_size == 1 && key_size == 1 && capacity >= 1);
  *(uint8_t *)out = 1; *size = 1; return 0;
}
static int product_load_cloud_models(const void *trust, size_t trust_size,
                                     const void *cloud, size_t cloud_size,
                                     const struct bkcloud_models_s *models)
{
  assert(trust && trust_size == 1 && cloud && cloud_size == 24 && !models);
  configure_calls++; g_cloud_loaded = true; return 0;
}
#include "bk7258_voice_benchmark.inc"

static void put32(uint8_t *p, uint32_t n)
{ p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n; }

static int send(uint32_t op, uint32_t session, uint32_t seq,
                uint32_t mode, uint32_t value, const void *data, size_t size)
{
  uint8_t p[64] = {'B', 'V', 'B', '1'};
  put32(p + 4, 1); put32(p + 8, op); put32(p + 12, session);
  put32(p + 16, seq); put32(p + 20, mode); put32(p + 24, value);
  put32(p + 28, size);
  if (size) memcpy(p + 32, data, size);
  return bkengvoice_command(p, 32 + size);
}

int main(void)
{
  uint8_t config[57] = {'B', 'V', 'C', '2'};
  struct bkengvoice_config_s view;
  put32(config + 4, 1); put32(config + 12, 1791775062);
  put32(config + 16, 1); put32(config + 20, 24);
  config[24] = 1;
  g_identity_bound = true;
  pairing = true; /* Bootstrap discovery is open, with no connected phone. */
  assert(product_benchmark_config(config, sizeof(config)) == 0);
  assert(configure_calls == 1 && g_configured);
  assert(product_benchmark_config(config, sizeof(config)) == -EBUSY);
  g_cloud_loaded = g_configured = false;
  peer_generation = 1;
  assert(product_benchmark_config(config, sizeof(config)) == -EBUSY);
  peer_generation = 0;
  g_control_bound = true;
  assert(product_benchmark_config(config, sizeof(config)) == -EBUSY);
  g_control_bound = false;
  network_busy = true;
  assert(product_benchmark_config(config, sizeof(config)) == -EBUSY);
  network_busy = false;
  assert(configure_calls == 1);
  product_benchmark_step(); /* No queued input: no second executor. */
  assert(bkengvoice_config_decode(config, sizeof(config), &view) == 0);
  assert(view.utc == 1791775062 && view.ca == config + 32 && view.cloud == config + 33);
  assert(bkengvoice_config_decode(config, sizeof(config) - 1, &view) == -EINVAL);
  config[31] = 1;
  assert(bkengvoice_config_decode(config, sizeof(config), &view) == -EINVAL);
  config[31] = 0;
  put32(config + 16, UINT32_MAX);
  assert(bkengvoice_config_decode(config, sizeof(config), &view) == -EINVAL);
  struct bkengvoice_input_s input;
  struct bkcontrol_status_s status;
  assert(bkengvoice_take(&input) == -EAGAIN);
  assert(send(1, 1, 1, 2, 480002, NULL, 0) == -E2BIG);
  assert(send(1, 1, 1, 2, 6, NULL, 0) == 0);
  assert(send(2, 1, 2, 2, 2, "xx", 2) == -ERANGE);
  assert(send(2, 1, 2, 2, 0, "abcdef", 6) == 0);
  assert(send(2, 1, 2, 2, 0, "abcdef", 6) == -ESTALE);
  assert(send(3, 1, 3, 2, 0, NULL, 0) == 0);
  assert(send(1, 2, 1, 1, 4, NULL, 0) == -EBUSY);
  assert(bkengvoice_take(&input) == 0);
  assert(input.session == 1 && input.mode == 2 && input.size == 6);
  assert(!memcmp(input.data, "abcdef", 6));
  assert(!bkengvoice_canceled(1));
  assert(send(4, 1, 4, 2, 0, NULL, 0) == 0);
  assert(bkengvoice_canceled(1));
  /* The ASR caller still owns these bytes until it has returned. */
  assert(!memcmp(input.data, "abcdef", 6));
  bkengvoice_finish(1, 7, -ECANCELED);
  assert(send(1, 2, 1, 1, 4, NULL, 0) == -EBUSY);
  bkengvoice_release(1);
  assert(send(1, 1, 1, 1, 4, NULL, 0) == -ESTALE);
  assert(send(1, 2, 1, 1, 4, NULL, 0) == 0);
  assert(send(3, 2, 2, 1, 0, NULL, 0) == -ENODATA);
  assert(send(2, 2, 2, 1, 0, "next", 4) == 0);
  assert(send(3, 2, 3, 1, 0, NULL, 0) == 0);
  assert(bkengvoice_take(&input) == 0 && input.session == 2);
  assert(bkengvoice_read(256, &status) == -EBUSY);
  assert(!bkengvoice_trace_poll());
  agent_audio_observe(8, 4, 100, 150, 512, 65536);
  bkengvoice_finish(2, 8, 0);
  bkengvoice_rearm(7, 2, 200); /* Stale turn cannot rearm this receipt. */
  assert(bkengvoice_read(256, &status) == 0 && status.config_total == 88);
  for (unsigned int i = 0; i < 4; i++) assert(bkengvoice_trace_poll());
  assert(!bkengvoice_trace_poll());
  assert(!memcmp(status.config_chunk, "BVA2", 4));
  assert(bkengvoice_read(257, &status) == 0);
  assert(status.config_chunk[3] == 1 && status.config_chunk[11] == 0);
  bkengvoice_rearm(8, 2, 250);
  assert(bkengvoice_read(257, &status) == 0);
  assert(status.config_chunk[11] == 0); /* Frozen readback. */
  assert(bkengvoice_read(256, &status) == 0);
  assert(bkengvoice_read(257, &status) == 0);
  assert(status.config_chunk[11] == 2 && status.config_chunk[15] == 1);
  assert(bkengvoice_read(260, &status) == 0);
  assert(status.config_chunk[3] == 1 && status.config_chunk[7] == 4);
  assert(status.config_chunk[11] == 100 && status.config_chunk[15] == 150);
  assert(bkengvoice_read(261, &status) == 0);
  assert(status.config_chunk[2] == 2 && status.config_chunk[5] == 1);
  assert(bkengvoice_read(262, &status) == -ERANGE);
  bkengvoice_release(2);
  assert(bkengvoice_read(0, &status) == 0 && status.config_total == 64);
  assert(!memcmp(status.config_chunk, "BVS1", 4));
  assert(bkengvoice_read(64, &status) == -ERANGE);
  assert(send(1, 3, 1, 1, 4, NULL, 0) == 0);
  assert(!bkengvoice_trace_poll());
  assert(send(2, 3, 2, 1, 0, "lost", 4) == 0);
  bkengvoice_disconnect();
  assert(bkengvoice_take(&input) == -EAGAIN);
  assert(send(3, 3, 3, 1, 0, NULL, 0) < 0);
  assert(send(1, 4, 1, 1, 4, NULL, 0) == 0);
  assert(send(2, 4, 2, 1, 0, "full", 4) == 0);
  assert(send(3, 4, 3, 1, 0, NULL, 0) == 0);
  assert(bkengvoice_take(&input) == 0);
  for (unsigned int i = 0; i < 8193; i++)
    agent_audio_observe(9, 4, i, i, 2, 2);
  bkengvoice_finish(4, 9, 0);
  bkengvoice_release(4);
  assert(bkengvoice_read(256, &status) == 0);
  assert(status.config_total == 64 + 8192 * 24);
  assert(bkengvoice_read(257, &status) == 0);
  assert(status.config_chunk[2] == 32 && status.config_chunk[7] == 1);
  assert(status.config_chunk[11] == 0 && status.config_chunk[15] == 0);
  assert(bkengvoice_read(260 + (8192 * 24 - 16) / 16, &status) == 0);
  assert(bkengvoice_read(UINT32_MAX, &status) == -ERANGE);
#ifdef CONFIG_BK7258_TTS_ISOLATION
  /* Same authenticated upload and owner, but no ASR or Agent dispatch.
   * TTS must execute on the existing output owner, not the service loop. */
  assert(send(1, 5, 1, 3, 4097, NULL, 0) == -E2BIG);
  assert(send(1, 5, 1, 3, 5, NULL, 0) == 0);
  assert(send(2, 5, 2, 3, 0, "fixed", 5) == 0);
  assert(send(3, 5, 3, 3, 0, NULL, 0) == 0);
  atomic_store(&g_agent_core_ready, true);
  atomic_store(&g_voice_initialized, true);
  g_cloud_loaded = g_configured = true;
  product_benchmark_step();
  assert(outbound.content && !strcmp(outbound.content, "fixed"));
  assert(!submitted_text && !submitted_pcm && !isolated_speech);
  int result = -1;
  assert(product_tts_isolation_dispatch(&outbound, &result));
  assert(result == 0 && submitted_text == 1 && isolated_speech == 1);
  assert(!submitted_pcm && !focus_dispatch);
  free(outbound.content); memset(&outbound, 0, sizeof(outbound));
  assert(bkengvoice_read(0, &status) == 0);
  assert(status.config_chunk[11] == 5); /* Actual completion. */
  assert(product_tts_isolation_text(123, "normal", request_status) == 0);
  assert(focus_dispatch == 1); /* Ordinary local handling preserved. */
  assert(send(1, 6, 1, 3, 5, NULL, 0) == 0);
  assert(send(2, 6, 2, 3, 0, "fixed", 5) == 0);
  assert(send(3, 6, 3, 3, 0, NULL, 0) == 0);
  product_benchmark_step();
  assert(send(4, 6, 4, 3, 0, NULL, 0) == 0);
  assert(product_tts_isolation_dispatch(&outbound, &result));
  assert(result == -ECANCELED && isolated_speech == 1);
  free(outbound.content); memset(&outbound, 0, sizeof(outbound));
  assert(send(1, 7, 1, 3, 4, NULL, 0) == 0);
  assert(send(2, 7, 2, 3, 0, "a\0bc", 4) == 0);
  assert(send(3, 7, 3, 3, 0, NULL, 0) == -EINVAL);
  bkengvoice_disconnect();
#else
  assert(send(1, 5, 1, 3, 5, NULL, 0) == -EINVAL);
#endif
  puts("CONTRACT_PASS voice-input bounded sequence ownership cancel disconnect next");
  return 0;
}
