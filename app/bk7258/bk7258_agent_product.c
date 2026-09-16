/****************************************************************************
 * app/bk7258/bk7258_agent_product.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 product application lifecycle for the official openvela Agent.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_AGENT

#include <errno.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <time.h>
#include <syslog.h>
#include <unistd.h>
#include <mbedtls/platform_util.h>

#include <nuttx/signal.h>

#include <arch/board/board.h>
#include "agent_compat.h"
#include "agent_config.h"
#include "core/agent_loop.h"
#include "core/memory_store.h"
#include "core/message_bus.h"
#include "core/session_mgr.h"
#include "infra/config_store.h"
#include "infra/http_proxy.h"
#include "llm/llm_proxy.h"
#include "llm/llm_router.h"
#include "tools/tool_guard.h"
#include "voice/voice_channel.h"
#ifdef CONFIG_BK7258_VOICE_TLS
#include "bk7258_agent_cloud.h"
#include "bk7258_agent_ota.h"
#include "bk7258_cloud_config.h"
#include "bk7258_provision_identity.h"
#include "bk7258_provision_claim.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_storage.h"
#include "bk7258_provision_time.h"
#include "bk7258_provision_owner.h"
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_network.h"
#include "bk7258_voice_media.h"
#include "bk7258_voice_volume_store.h"
#include "bk7258_preferences.h"
#include <arch/chip/bk7258_wifi.h>
#include <arch/chip/bk7258_active_image.h>
#include <arch/chip/bk7258_ota_rpmsg.h>
#include "bk7258_voice_config.h"
#endif

#ifdef CONFIG_BK7258_VOICE_TLS
/* The existing provisioning owner borrows this identity for its lifetime.
 * It is never copied or freed while a claim/control session uses it. */
static struct bkprov_identity_s g_identity;
static bool g_identity_bound;
static bool g_cloud_loaded;
static bool g_configured;
static bool g_trigger_started;
static int g_product_error;
static int g_service_result = -ENOTCONN;
static int g_probe_result = -ENOTCONN;
static uint64_t g_config_revision;
static sem_t g_product_wake;
static atomic_uint g_product_events;
static atomic_bool g_agent_core_ready;
static atomic_bool g_trigger_prepare_pending = ATOMIC_VAR_INIT(true);

extern int bk7258_agent_trigger_prepare(void);
extern int bk7258_agent_trigger_start(void);
extern int bk7258_agent_trigger_stop(void);
extern int bk7258_agent_trigger_process(void);
extern int bk7258_agent_trigger_model_step(bool arm);
extern bool bk7258_agent_trigger_model_pending(void);
extern int bk7258_agent_trigger_rearm(void);
extern bool bk7258_agent_trigger_armed(void);
extern int bk7258_agent_trigger_control(void *, enum bkcontrol_command_e,
  uint32_t, uint32_t, const uint8_t *, size_t, struct bkcontrol_status_s *);

/* Media Trigger reports only product wake admission here. The official voice
 * channel remains the sole conversational lifecycle owner. */
void bk7258_agent_product_wake(void)
{
  atomic_fetch_or(&g_product_events, 4);
  sem_post(&g_product_wake);
}

static void bk7258_agent_storage_changed(void)
{
  atomic_fetch_or(&g_product_events, 8);
  sem_post(&g_product_wake);
}

static int product_control(void *context, enum bkcontrol_command_e command,
                            uint32_t value, struct bkcontrol_status_s *status)
{
  int ret = 0;
  unsigned int volume;
  (void)context;
  memset(status, 0xff, sizeof(*status));
  status->flags = 0;
  if (bkagent_ota_busy() && command != BKCONTROL_INFO &&
      command != BKCONTROL_STATUS) return -EBUSY;
  switch (command)
    {
      case BKCONTROL_INFO:
        {
          struct bk7258_mcuboot_version_s version;
          struct bk7258_ota_pair_snapshot_s pair;
          ret = bk7258_active_ap_image_version(&version);
          if (!ret) ret = bk7258_ota_rpmsg_pair_status(&pair,
            CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
          if (ret) return ret;
          if (pair.state != BK7258_OTA_PAIR_CONFIRMED ||
              !pair.security_counter_present || !pair.security_counter ||
              !bk7258_mcuboot_version_equal(&version, &pair.version)) return -EAGAIN;
          status->device_info.major = version.major;
          status->device_info.minor = version.minor;
          status->device_info.revision = version.revision;
          status->device_info.build = version.build;
          status->device_info.security_counter = pair.security_counter;
          return 0;
        }
      case BKCONTROL_STATUS: break;
      case BKCONTROL_CANCEL:
        /* The fixed official API exposes finalize-and-dispatch, not request
         * cancellation. Do not turn an App cancel into a submitted utterance. */
        ret = -ENOTSUP;
        break;
      case BKCONTROL_CLEAR_HISTORY:
        if (g_trigger_started && !bk7258_agent_trigger_armed()) return -EBUSY;
        ret = session_clear("voice");
        break;
      case BKCONTROL_VOLUME:
        if (value > 100u) return -EINVAL;
        ret = bkvoice_media_volume(true, value, &volume);
        if (!ret) {
          status->volume = volume;
          ret = bkvoice_volume_store_set(volume);
        }
        break;
      default: return -ENOTSUP;
    }
  if (ret < 0) return ret;
  bool idle = !g_configured || bk7258_agent_trigger_armed();
  status->flags = (g_configured && bk7258_agent_trigger_armed() ? 1u : 0u) |
                  (!idle || bkagent_ota_busy() ? 2u : 0u) | 16384u;
#ifdef BKAGENT_APP_OTA_ENABLED
  status->flags |= 4096u | 8192u;
#endif
  /* SDC1 reports only the product admission boundary. The fixed official
   * channel has no public whole-turn completion state. */
  status->error = 0;
  if (idle) {
    status->flags |= 4u;
    status->turn = BKCONTROL_TURN_IDLE;
    status->error = g_product_error;
  }
  if (idle && command != BKCONTROL_CANCEL &&
      bkvoice_media_volume(false, 0, &volume) == 0) {
    status->volume = volume;
    status->flags |= 8u;
  }
  struct bk7258_wifi_result_s wifi;
  if (bk7258_wifi_read_link(&wifi) == 0) {
    status->flags |= 1024u;
    if (wifi.ipaddr && bk7258_wifi_native_lease_matches(&wifi)) status->flags |= 2048u;
  }
  return 0;
}

static bool product_available(void *unused)
{
  (void)unused;
  return !g_configured && !bkagent_ota_busy() && !bkprov_network_busy();
}

static int product_models(enum bkcontrol_command_e command, uint32_t offset,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status);

static int product_config(void *context, enum bkcontrol_command_e command,
  uint32_t kind, uint32_t offset, const uint8_t *record, size_t size,
  struct bkcontrol_status_s *status)
{
  if (bkagent_ota_busy()) return -EBUSY;
  if (kind == BKCONTROL_CONFIG_CLOUD_MODELS)
    return product_models(command, offset, record, size, status);
  return bk7258_agent_trigger_control(context, command, kind, offset,
                                     record, size, status);
}

#ifdef BKAGENT_APP_OTA_ENABLED
static int product_ota(void *context, enum bkcontrol_command_e command,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  if (command == BKCONTROL_OTA_START) {
    if (bkagent_ota_busy() || !g_identity_bound ||
        bkprov_network_busy() || bk7258_agent_trigger_model_pending()) return -EBUSY;
    if (g_trigger_started && !bk7258_agent_trigger_armed()) return -EBUSY;
    int ret;
    ret = bk7258_agent_trigger_stop();
    if (ret) return ret;
    g_trigger_started = false;
    atomic_store(&g_trigger_prepare_pending, true);
  }
  return bkagent_ota_control(context, command, record, size, status);
}
#endif

static int product_load_legacy(void *unused, const void *data, size_t size)
{ (void)unused; (void)data; (void)size; return -ENOTSUP; }

static int product_load_cloud(void *unused, const void *trust, size_t trust_size,
                               const void *cloud, size_t cloud_size)
{
  (void)unused;
  struct bkcloud_config_s *decoded = calloc(1, sizeof(*decoded));
  if (!decoded) return -ENOMEM;
  int ret = bkcloud_config_decode(decoded, cloud, cloud_size);
  if (!ret) ret = bkagent_cloud_configure(trust, trust_size, cloud, cloud_size);
  if (!ret) ret = bkagent_cloud_activate_asr(decoded->dialect);
  g_cloud_loaded = ret == 0;
  if (!ret)
    {
      /* Fixed upstream streaming ASR/TTS still calls the Volc implementation,
       * and its LLM transport cannot consume this protected CA. Keep the
       * configuration readable, but do not arm a path that would cross the
       * selected provider or weaken certificate verification. */
      g_service_result = -ENOTSUP;
      syslog(LOG_WARNING,
             "BKVOICE cloud loaded; official stream/LLM adapters unavailable=%d\n",
             g_service_result);
    }
  else
    {
      g_service_result = ret;
    }
  bkcloud_config_clear(decoded);
  free(decoded);
  return ret;
}

static int product_models(enum bkcontrol_command_e command, uint32_t offset,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  struct bkcloud_models_s models;
  int ret;
  if (command == BKCONTROL_CONFIG_READ) {
    uint8_t wire[BKCLOUD_MODELS_RECORD_MAX];
    size_t total = 0;
    if (!g_cloud_loaded) return g_product_error ? g_product_error : -EAGAIN;
    ret = bkagent_cloud_models_get(&models);
    if (!ret) ret = bkcloud_models_encode(&models, wire, sizeof(wire), &total);
    if (ret) return ret;
    if (offset >= total || (offset & 15u)) return -ERANGE;
    status->config_total = total;
    memset(status->config_chunk, 0, sizeof(status->config_chunk));
    size_t count = total - offset;
    if (count > sizeof(status->config_chunk)) count = sizeof(status->config_chunk);
    memcpy(status->config_chunk, wire + offset, count);
    return 0;
  }
  if (command != BKCONTROL_CONFIG_BEGIN && command != BKCONTROL_CONFIG_APPLY)
    return -EINVAL;
  if (size < 12 || size > BKCLOUD_MODELS_RECORD_MAX) return -EMSGSIZE;
  if (!g_identity_bound) return -ENOKEY;
  if ((g_trigger_started && !bk7258_agent_trigger_armed()) ||
      bkprov_network_busy() ||
      bk7258_agent_trigger_model_pending()) return -EBUSY;
  if (command == BKCONTROL_CONFIG_BEGIN) return 0;
  ret = bkcloud_models_decode(&models, record, size);
  if (ret) return ret;

  /* Reuse the accepted protected configuration and the normal backend
   * activation path. This changes public model names, never Wi-Fi, trust,
   * identity or the independent choice of a local TTS backend. */
  struct model_workspace_s {
    uint8_t bundle[BKPROV_BUNDLE_MAX];
    uint8_t voice[BKVOICE_CONFIG_MAX_BYTES];
    uint8_t transaction[16];
    struct bkprov_settings_s settings;
  } *work = calloc(1, sizeof(*work));
  if (!work) return -ENOMEM;
  size_t bundle_size = 0, voice_size = 0;
  uint64_t revision = 0;
  ret = bkprov_storage_snapshot(work->bundle, sizeof(work->bundle),
                                &bundle_size, &revision, work->transaction);
  if (!ret && revision != g_config_revision) ret = -EAGAIN;
  if (!ret) ret = bkprov_settings_decode(&work->settings, work->bundle, bundle_size);
  if (!ret && !work->settings.cloud_size) ret = -ENOTSUP;
  /* The persisted UTC is a floor, not the current time. Match the existing
   * restore path before the config loader installs a fresh clock anchor. */
  if (!ret) ret = bkprov_time_get(work->settings.utc, &work->settings.utc);
  if (!ret) ret = bkprov_settings_voice(&work->settings,
    g_identity.record + 48, g_identity.certificate_size,
    g_identity.record + 48 + g_identity.certificate_size, g_identity.key_size,
    work->voice, sizeof(work->voice), &voice_size);
  if (!ret && g_trigger_started) {
    ret = bk7258_agent_trigger_stop();
    if (!ret) g_trigger_started = false;
  }
  if (!ret) ret = bk7258_preferences_cloud_models_set(&models);
  if (!ret) {
    g_configured = false;
    ret = product_load_cloud(NULL, work->voice, voice_size,
                              work->settings.cloud, work->settings.cloud_size);
    g_configured = ret == 0 && g_service_result == 0;
  }
  g_product_error = ret;
  mbedtls_platform_zeroize(work, sizeof(*work));
  free(work);
  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE cloud model apply result=%d ready=%d\n", ret, g_configured);
  return ret;
}

static int product_connect(void *unused)
{
  (void)unused;
  g_probe_result = bkagent_cloud_verify_service();
  return g_probe_result;
}

static int product_ready(void *unused)
{ (void)unused; return g_probe_result == 0 ? 1 : g_probe_result; }

static int product_clear(void *unused)
{
  (void)unused;
  if (g_trigger_started && !bk7258_agent_trigger_armed()) return -EBUSY;
  g_configured = false;
  g_cloud_loaded = false;
  g_service_result = -ENOTCONN;
  g_probe_result = -ENOTCONN;
  int ret = bk7258_agent_trigger_stop();
  if (ret < 0) return ret;
  g_trigger_started = false;
  atomic_store(&g_trigger_prepare_pending, true);
  return 0;
}

static const struct bkprov_voice_ops_s g_provision_voice = {
  product_available, product_load_legacy, product_connect, product_ready,
  product_clear, product_load_cloud
};

/* One bounded, zeroized workspace for protected storage reads. Parsed
 * identity owns its own allocation; settings borrow only this workspace. */
struct agent_config_workspace_s {
  uint8_t bundle[BKPROV_BUNDLE_MAX];
  uint8_t identity[8192];
  struct bkprov_settings_s settings;
};

static bool storage_unavailable(int result)
{
  return result == -ENODEV || result == -ENOTCONN ||
         result == -EXDEV || result == -ETIMEDOUT;
}

static int bk7258_agent_activate_cloud(bool *storage_waiting)
{
  *storage_waiting = false;
  if (bkprov_owner_busy() || bkprov_network_busy()) return -EBUSY;
  struct agent_config_workspace_s *work = calloc(1, sizeof(*work));
  if (!work) return -ENOMEM;
  size_t size = 0;
  uint64_t revision = 0;
  uint8_t transaction[16] = {0};
  int ret = 0;
  if (!g_identity_bound) {
    ret = bkprov_storage_identity(work->identity, sizeof(work->identity), &size);
    *storage_waiting = storage_unavailable(ret);
    if (!ret) ret = bkprov_identity_load(&g_identity, work->identity, size);
    if (!ret) ret = bkprov_network_bind(&g_identity, &g_provision_voice, NULL);
    if (!ret) ret = bkprov_owner_bind(&g_identity.certificate, &g_identity.key,
                                      g_identity.secret, bkprov_network_ops(), NULL);
    if (ret < 0) {
      (void)bkprov_network_unbind();
      bkprov_identity_clear(&g_identity);
      goto out;
    }
    g_identity_bound = true;
  }
  ret = bkprov_storage_snapshot(work->bundle, sizeof(work->bundle), &size,
                                &revision, transaction);
  *storage_waiting = storage_unavailable(ret);
  if (ret < 0) goto out;
  ret = bkprov_settings_decode(&work->settings, work->bundle, size);
  if (ret < 0) goto out;
  if (!work->settings.control_key) { ret = -ENOKEY; goto out; }
  ret = bkprov_owner_control(work->settings.control_key, product_control, NULL);
  if (!ret) ret = bkprov_owner_control_config(product_config);
#ifdef BKAGENT_APP_OTA_ENABLED
  if (!ret) ret = bkprov_owner_control_ota(product_ota);
#endif
  if (ret < 0) goto out;
  if (g_configured && revision == g_config_revision) goto out;
  ret = product_clear(NULL);
  if (!ret) ret = bkprov_network_restore(work->bundle, size);
  if (!ret) g_config_revision = revision;
out:
  mbedtls_platform_zeroize(work, sizeof(*work));
  free(work);
  return ret;
}

static int bk7258_agent_config_task(int argc, FAR char *argv[])
{
  bool pending = true;
  bool network_was_busy = false;
  bool storage_waiting = false;
  uint32_t connection_generation = bkprov_gatt_generation();
  uint64_t storage_deadline = 0;
  uint64_t storage_retry_at = 0;
  (void)argc; (void)argv;
  while (1) {
    /* The existing BLE TLS/claim and Wi-Fi trial owners need their documented
     * step cadence. Configuration activation itself is event driven. */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 20000000;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    int waited;
    do { waited = sem_timedwait(&g_product_wake, &deadline); }
    while (waited < 0 && errno == EINTR);
    if (waited < 0 && errno != ETIMEDOUT) return -errno;
    unsigned int events = atomic_exchange(&g_product_events, 0);
    bkagent_ota_poll();
    if (events & 8) {
      pending = true;
      atomic_store(&g_trigger_prepare_pending, true);
    }
    uint64_t now = bkvoice_config_now_ms(NULL);
    uint32_t generation = bkprov_gatt_generation();
    if (!storage_deadline) storage_deadline = now + 15000;
    if (generation != connection_generation) {
      connection_generation = generation;
      /* A later App connection can retry a mount that missed the bounded
       * startup window. It cannot bypass identity or trust validation. */
      if (storage_waiting) {
        storage_deadline = now + 15000;
        storage_retry_at = now;
      }
    }
    if (storage_retry_at && now >= storage_retry_at) {
      storage_retry_at = 0;
      if (now >= storage_deadline) {
        syslog(LOG_WARNING, "BKVOICE storage readiness window expired result=%d\n",
               g_product_error);
      } else {
        int ret = bkprov_storage_refresh();
        if (ret == -EBUSY) storage_retry_at = now + 250;
        else if (ret) g_product_error = ret;
        /* The existing storage worker owns I/O and publishes completion. */
      }
    }
    bkprov_network_step();
    bool network_busy = bkprov_network_busy();
    if (network_was_busy && !network_busy) {
      g_configured = g_service_result == 0;
      g_product_error = g_service_result;
      syslog(g_configured ? LOG_INFO : LOG_WARNING,
             "BKVOICE configuration ready=%d result=%d revision=%llu\n",
             g_configured, g_product_error, (unsigned long long)g_config_revision);
    }
    network_was_busy = network_busy;
    if (!atomic_load(&g_agent_core_ready)) continue;
    if (pending && !bkagent_ota_busy() &&
        (!g_trigger_started || bk7258_agent_trigger_armed()) &&
        !network_busy && !bkprov_owner_busy()) {
      int ret = bk7258_agent_activate_cloud(&storage_waiting);
      if (storage_waiting && now < storage_deadline) storage_retry_at = now + 250;
      else if (!storage_waiting) storage_retry_at = 0;
      pending = ret == -EBUSY;
      if (ret && !pending) {
        g_product_error = ret;
        syslog(LOG_WARNING, "BKVOICE configuration unavailable result=%d\n", ret);
      }
      network_was_busy = bkprov_network_busy();
    }
    (void)bkprov_owner_step(bkvoice_config_now_ms(NULL), 0, false, false,
      (!g_trigger_started || bk7258_agent_trigger_armed()) &&
      !bkprov_network_busy() && !bkagent_ota_busy());
    if (bkagent_ota_busy()) continue;
    bool model_was_pending = bk7258_agent_trigger_model_pending();
    int model_result = bk7258_agent_trigger_model_step(g_configured);
    if (model_result < 0 && model_result != -EBUSY) g_product_error = model_result;
    if (model_was_pending && !bk7258_agent_trigger_model_pending() &&
        model_result == 0)
      atomic_store(&g_trigger_prepare_pending, false);
    if (atomic_load(&g_trigger_prepare_pending) && !pending && !network_busy &&
        !bkprov_owner_busy() && !bk7258_agent_trigger_model_pending()) {
      int ret = bk7258_agent_trigger_prepare();
      if (ret != -EBUSY) atomic_store(&g_trigger_prepare_pending, false);
      if (ret < 0 && ret != -EBUSY) g_product_error = ret;
      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKVOICE wake model prepared=%d result=%d\n", ret == 0, ret);
    }
    if (!g_configured || pending || bkprov_network_busy()) continue;
    if (!g_trigger_started) {
      unsigned int saved, observed;
      int ret = bkvoice_volume_store_get(&saved);
      if (!ret) ret = bkvoice_media_volume(true, saved, &observed);
      if (ret == -ENOENT) ret = 0;
      if (!ret) ret = bk7258_agent_trigger_start();
      g_trigger_started = ret == 0;
      g_product_error = ret;
      syslog(ret ? LOG_WARNING : LOG_INFO, "BKVOICE wake ready=%d result=%d\n",
             g_trigger_started, ret);
      /* A failed model/media load is unavailable until a configuration,
       * model or readiness event arrives; do not reopen it at 50 Hz. */
      if (ret) g_configured = false;
    }
    if (g_trigger_started && (events & 4)) {
      int ret = bk7258_agent_trigger_process();
      if (ret < 0) g_product_error = ret;
    }
  }
}
#endif

volatile int g_bk7258_agent_pid = -1;
volatile int g_bk7258_agent_launch_pid = -1;
volatile int g_bk7258_agent_launch_errno;
volatile uint32_t g_bk7258_agent_launch_stage;

/* The fixed official package has no core-only bootstrap entry: its sole main
 * also starts CLI, WebSocket, cron, heartbeat and a competing network owner.
 * This product entry only initializes the retained official core and maps its
 * outbound voice channel to the official voice output. It does not implement
 * a second Agent loop, session store, ASR/TTS pipeline or recovery scheduler. */
int ai_agent_main(int argc, FAR char *argv[])
{
  static const char *directories[] =
    {
      "/data/agent", "/data/agent/config", "/data/agent/memory",
      "/data/agent/sessions", "/data/agent/skills"
    };
  int ret;

  (void)argc;
  (void)argv;

  for (unsigned int i = 0; i < sizeof(directories) / sizeof(directories[0]); i++)
    {
      if (mkdir(directories[i], 0755) < 0 && errno != EEXIST)
        {
          syslog(LOG_ERR, "bk7258: Agent directory %s failed: %d\n",
                 directories[i], errno);
          return ERROR;
        }
    }

  ret = config_store_init();
  if (!ret) ret = message_bus_init();
  if (!ret) ret = memory_store_init();
  if (!ret) ret = session_mgr_init();
  if (!ret) ret = http_proxy_init();
  if (!ret) ret = llm_proxy_init();
  if (!ret) ret = llm_router_init();
  if (!ret) ret = tool_guard_init();
  if (!ret) ret = agent_loop_init();
  if (!ret) ret = voice_channel_init();
  if (!ret) ret = agent_loop_start();
  if (ret)
    {
      syslog(LOG_ERR, "bk7258: official Agent core init failed: %d\n", ret);
#ifdef CONFIG_BK7258_VOICE_TLS
      g_product_error = ret;
#endif
      return ERROR;
    }

#ifdef CONFIG_BK7258_VOICE_TLS
  atomic_store(&g_agent_core_ready, true);
  sem_post(&g_product_wake);
#endif
  syslog(LOG_INFO, "bk7258: official Agent core ready\n");

  while (!agent_shutdown_requested())
    {
      agent_msg_t message;
      ret = message_bus_pop_outbound(&message, 1000);
      if (ret != OK) continue;
      if (!strcmp(message.channel, AGENT_CHAN_VOICE) && message.content)
        {
          ret = voice_channel_speak(message.content);
#ifdef CONFIG_BK7258_VOICE_TLS
          if (!ret && g_trigger_started)
            {
              ret = bk7258_agent_trigger_rearm();
            }
          if (ret)
            {
              g_product_error = ret;
              syslog(LOG_WARNING,
                     "bk7258: official voice output/rearm failed: %d\n", ret);
            }
#endif
        }
      else
        {
          syslog(LOG_WARNING, "bk7258: unsupported Agent output channel=%s\n",
                 message.channel);
        }
      message_bus_msg_free(&message);
    }

  message_bus_wakeup();
  return OK;
}

#ifdef CONFIG_AI_AGENT_LVGL_UI
extern void lvgl_ui_channel_show(void);
volatile uint32_t g_bk7258_agent_ui_show_attempts;

static int bk7258_agent_ui_show_task(int argc, FAR char *argv[])
{
  unsigned int attempt;

  (void)argc;
  (void)argv;

  /* Agent phase-3 initializes the widgets and phase-5 marks the LVGL channel
   * running.  show() is idempotent after the chat screen becomes visible, so
   * retry for one bounded startup window.
   */

  for (attempt = 0; attempt < 30; attempt++)
    {
      nxsig_usleep(1000000u);
      g_bk7258_agent_ui_show_attempts = attempt + 1u;
      lvgl_ui_channel_show();
    }

  return OK;
}
#endif

int bk7258_agent_product_prepare(void)
{
#ifdef CONFIG_BK7258_VOICE_TLS
  int ret = bkagent_cloud_register();
  if (ret != 0) return ret;
  if (sem_init(&g_product_wake, 0, 0) < 0) return -errno;
  bkprov_storage_set_notify(bk7258_agent_storage_changed);
#endif
#ifdef CONFIG_AI_AGENT_LVGL_UI
  return bk7258_board_ui_initialize();
#else
  return OK;
#endif
}

static int bk7258_agent_launch_task(int argc, FAR char *argv[])
{
  pid_t agentpid;
#ifdef CONFIG_AI_AGENT_LVGL_UI
  pid_t showpid;
  int ret;
#endif

  (void)argc;
  (void)argv;
  g_bk7258_agent_launch_stage = 2u;

#ifdef CONFIG_AI_AGENT_LVGL_UI
  ret = bk7258_board_ui_wait_ready();
  if (ret < 0)
    {
      g_bk7258_agent_launch_stage = 0x82u;
      syslog(LOG_ERR, "bk7258: Agent LVGL wait failed: %d\n", ret);
      g_bk7258_agent_pid = ret;
      return ERROR;
    }
#endif

  g_bk7258_agent_launch_stage = 3u;

  /* Current-conversation files use the official session manager on volatile
   * storage. Existing encrypted persistent memory is not converted to plain
   * files or uploaded by this candidate. Its migration remains explicit. */
  if (mount(NULL, "/data", "tmpfs", 0, NULL) < 0)
    {
      syslog(LOG_ERR, "bk7258: Agent volatile storage unavailable: %d\n", errno);
      return ERROR;
    }

  agentpid = task_create("ai_agent",
                         CONFIG_EXAMPLES_AI_AGENT_VELA_PRIORITY,
                         CONFIG_EXAMPLES_AI_AGENT_VELA_STACKSIZE,
                         (main_t)ai_agent_main, NULL);
  g_bk7258_agent_launch_errno = agentpid < 0 ? errno : 0;
  g_bk7258_agent_pid = (int)agentpid;
  g_bk7258_agent_launch_stage = agentpid < 0 ? 0x84u : 4u;
  if (agentpid < 0)
    {
      syslog(LOG_ERR, "bk7258: official Agent launch failed: %d\n",
             (int)agentpid);
      return ERROR;
    }

#ifdef CONFIG_AI_AGENT_LVGL_UI
  showpid = task_create("agent-ui-show", 90, 2048,
                        bk7258_agent_ui_show_task, NULL);
  if (showpid < 0)
    {
      syslog(LOG_ERR, "bk7258: Agent UI show task failed: %d\n",
             (int)showpid);
    }
  else
    {
      g_bk7258_agent_launch_stage = 5u;
    }
#endif

  return OK;
}

int bk7258_agent_product_start(void)
{
  pid_t launchpid;
  pid_t configpid;

  g_bk7258_agent_launch_stage = 1u;
  launchpid = task_create("agent-start", 99, 4096,
                          bk7258_agent_launch_task, NULL);
  g_bk7258_agent_launch_pid = (int)launchpid;
  if (launchpid < 0)
    {
      g_bk7258_agent_launch_errno = errno;
      g_bk7258_agent_launch_stage = 0x81u;
      g_bk7258_agent_pid = (int)launchpid;
      syslog(LOG_ERR, "bk7258: Agent coordinator failed: %d\n",
             (int)launchpid);
      return (int)launchpid;
    }

#ifdef CONFIG_BK7258_VOICE_TLS
  configpid = task_create("agent-config", 95, 8192,
                          bk7258_agent_config_task, NULL);
  if (configpid < 0)
    syslog(LOG_ERR, "bk7258: official config activation task failed: %d\n",
           (int)configpid);
#endif

  return OK;
}

#endif /* CONFIG_BK7258_APP_AGENT */
