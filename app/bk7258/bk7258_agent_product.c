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
#ifdef CONFIG_BK7258_VOICE_TLS
#include "bk7258_agent_cloud.h"
#include "bk7258_agent_ota.h"
#include "bk7258_cloud_config.h"
#include "bk7258_provision_identity.h"
#include "bk7258_provision_claim.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_storage.h"
#include "bk7258_provision_owner.h"
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_network.h"
#include "bk7258_voice_media.h"
#include "bk7258_voice_volume_store.h"
#include <arch/chip/bk7258_wifi.h>
#include <arch/chip/bk7258_active_image.h>
#include <arch/chip/bk7258_ota_rpmsg.h>
#include "core/session_mgr.h"
#include "bk7258_voice_config.h"
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#include "voice/voice_channel.h"
#include "voice/audio_capture.h"
#include <media_recorder.h>
#include "agent_config.h"
#include "infra/config_store.h"
#endif
#if defined(CONFIG_BK7258_AUD) && !defined(CONFIG_MEDIA)
extern void bk7258_agent_media_player_link(void);
#endif

#if defined(CONFIG_BK7258_MIC) && !defined(CONFIG_MEDIA)
extern void bk7258_agent_media_recorder_link(void);
#endif

extern int ai_agent_main(int argc, FAR char *argv[]);

#ifdef CONFIG_BK7258_VOICE_TLS
/* The existing provisioning owner borrows this identity for its lifetime.
 * It is never copied or freed while a claim/control session uses it. */
static struct bkprov_identity_s g_identity;
static bool g_identity_bound;
static bool g_configured;
static bool g_trigger_started;
static int g_product_error;
static int g_service_result = -ENOTCONN;
static uint64_t g_config_revision;
static sem_t g_product_wake;
static atomic_uint g_product_events;
static atomic_bool g_voice_initialized;
static atomic_bool g_agent_ready;

extern int bk7258_agent_trigger_start(void);
extern int bk7258_agent_trigger_stop(void);
extern int bk7258_agent_trigger_process(void);
extern int bk7258_agent_trigger_model_step(void);
extern bool bk7258_agent_trigger_model_pending(void);
extern int bk7258_agent_trigger_rearm(void);
extern bool bk7258_agent_trigger_armed(void);
extern int bk7258_agent_trigger_control(void *, enum bkcontrol_command_e,
  uint32_t, uint32_t, const uint8_t *, size_t, struct bkcontrol_status_s *);

/* The official voice channel is the sole conversational lifecycle owner.
 * These flags describe product readiness and existing protocol work only. */
void bk7258_agent_product_event(int event, int result)
{
  unsigned int flags = 0;
  if (event == VOICE_CHANNEL_EVENT_INITIALIZED) {
    atomic_store(&g_voice_initialized, result == 0);
    flags = 1;
  } else if (event == VOICE_CHANNEL_EVENT_SERVICE_READY) {
    atomic_store(&g_agent_ready, result == 0);
    flags = 1;
  } else if (event == VOICE_CHANNEL_EVENT_TURN_COMPLETE) flags = 2;
  else if (event == 100) flags = 4;
  if (flags) { atomic_fetch_or(&g_product_events, flags); sem_post(&g_product_wake); }
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
      case BKCONTROL_CANCEL: ret = voice_channel_cancel(); break;
      case BKCONTROL_CLEAR_HISTORY:
        if (!voice_channel_is_idle()) return -EBUSY;
        ret = session_clear("voice");
        break;
      case BKCONTROL_VOLUME:
        if (value > 100u) return -EINVAL;
        if (!voice_channel_is_idle()) return -EBUSY;
        ret = bkvoice_media_volume(true, value, &volume);
        if (!ret) {
          status->volume = volume;
          ret = bkvoice_volume_store_set(volume);
        }
        break;
      default: return -ENOTSUP;
    }
  if (ret < 0) return ret;
  bool idle = atomic_load(&g_voice_initialized) && voice_channel_is_idle();
  status->flags = (g_configured && bk7258_agent_trigger_armed() ? 1u : 0u) |
                  (!idle || bkagent_ota_busy() ? 2u : 0u) | 16384u;
#ifdef BKAGENT_APP_OTA_ENABLED
  status->flags |= 4096u | 8192u;
#endif
  /* Expose the official whole-turn idle result using the existing SDC1 value.
   * Unknown phases require a zero error field in SDC1. No recorder state is
   * used to infer completion, and no second turn state is retained. */
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
  return atomic_load(&g_voice_initialized) && atomic_load(&g_agent_ready) &&
         voice_channel_is_idle() && !g_configured && !bkagent_ota_busy();
}

static int product_config(void *context, enum bkcontrol_command_e command,
  uint32_t kind, uint32_t offset, const uint8_t *record, size_t size,
  struct bkcontrol_status_s *status)
{
  if (bkagent_ota_busy()) return -EBUSY;
  return bk7258_agent_trigger_control(context, command, kind, offset,
                                     record, size, status);
}

#ifdef BKAGENT_APP_OTA_ENABLED
static int product_ota(void *context, enum bkcontrol_command_e command,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  if (command == BKCONTROL_OTA_START) {
    if (bkagent_ota_busy() || !g_identity_bound ||
        !atomic_load(&g_voice_initialized) || !voice_channel_is_idle() ||
        bkprov_network_busy() || bk7258_agent_trigger_model_pending()) return -EBUSY;
    int ret;
    if (g_trigger_started) {
      ret = bk7258_agent_trigger_stop();
      if (ret) return ret;
      g_trigger_started = false;
    }
    if (!voice_channel_is_idle()) return -EBUSY;
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
  if (!ret) {
    const char *backend = decoded->dialect == 2 ? "mimo" : "openai-audio";
    char asr[64] = {0}, tts[64] = {0}, location[32] = {0};
    (void)claw_config_get("asr_backend", asr, sizeof(asr));
    (void)claw_config_get(AGENT_CFG_KEY_TTS_BACKEND, tts, sizeof(tts));
    (void)claw_config_get(AGENT_CFG_KEY_TTS_LOCATION, location, sizeof(location));
    if (!asr[0]) snprintf(asr, sizeof(asr), "%s", backend);
    if (!tts[0] && (!location[0] || !strcmp(location, "remote")))
      snprintf(tts, sizeof(tts), "%s", backend);
    ret = voice_asr_set_backend(asr);
    syslog(LOG_INFO, "BKVOICE ASR activation backend=%s result=%d\n", asr, ret);
    if (!ret) {
      ret = bkagent_cloud_activate_llm();
      syslog(LOG_INFO, "BKVOICE LLM activation result=%d\n", ret);
    }
    if (!ret) {
      ret = tts[0] ? voice_tts_set_backend(tts) : -ENOTSUP;
      syslog(LOG_INFO, "BKVOICE TTS activation backend=%s result=%d\n", tts, ret);
    }
  }
  bkcloud_config_clear(decoded);
  free(decoded);
  g_service_result = ret ? ret : -EAGAIN;
  return ret;
}

static int product_connect(void *unused)
{
  (void)unused;
  g_service_result = bkagent_cloud_verify_service();
  return g_service_result;
}

static int product_ready(void *unused)
{ (void)unused; return g_service_result == 0 ? 1 : g_service_result; }

static int product_clear(void *unused)
{
  (void)unused;
  if (!voice_channel_is_idle()) return -EBUSY;
  g_configured = false;
  g_service_result = -ENOTCONN;
  if (g_trigger_started) {
    int ret = bk7258_agent_trigger_stop();
    if (ret < 0) return ret;
    g_trigger_started = false;
  }
  return 0;
}

static const struct bkprov_voice_ops_s g_provision_voice = {
  product_available, product_load_legacy, product_connect, product_ready,
  product_clear, product_load_cloud
};

static int product_capture_route(int active)
{ return bkvoice_media_source_set_active(MEDIA_SOURCE_MIC, active != 0); }

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
    if (events & 8) pending = true;
    if (!atomic_load(&g_voice_initialized) || !atomic_load(&g_agent_ready)) continue;
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
    if (pending && !bkagent_ota_busy() && voice_channel_is_idle() &&
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
      voice_channel_is_idle() && !bkprov_network_busy() && !bkagent_ota_busy());
    if (bkagent_ota_busy()) continue;
    int model_result = bk7258_agent_trigger_model_step();
    if (model_result < 0 && model_result != -EBUSY) g_product_error = model_result;
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
    } else if (events & 2) {
      int ret = bk7258_agent_trigger_rearm();
      if (ret < 0) { g_product_error = ret; g_configured = false; }
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
  ret = audio_capture_set_route(product_capture_route);
  if (ret != 0) return ret;
  if (sem_init(&g_product_wake, 0, 0) < 0) return -errno;
  ret = voice_channel_set_event_callback(bk7258_agent_product_event);
  if (ret != 0) return ret;
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

  /* Keep the product media backends reachable from the lifecycle object.
   * The official Agent also provides weak ABI stubs, so relying on unresolved
   * media symbols would not extract these strong implementations from the
   * application archive.
   */

#if defined(CONFIG_BK7258_AUD) && !defined(CONFIG_MEDIA)
  bk7258_agent_media_player_link();
#endif

#if defined(CONFIG_BK7258_MIC) && !defined(CONFIG_MEDIA)
  bk7258_agent_media_recorder_link();
#endif

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
