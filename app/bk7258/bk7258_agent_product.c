/****************************************************************************
 * app/bk7258/bk7258_agent_product.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 product application lifecycle for the official openvela Agent.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_AGENT

#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
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
#include <nuttx/mutex.h>

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
#include "tools/tool_files.h"
#include "voice/voice_channel.h"
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#ifdef CONFIG_BK7258_VISION_SERVICE
#include "bk7258_agent_vision.h"
#endif
#ifdef CONFIG_BK7258_VOICE_TLS
#include "cJSON.h"
#include "tools/tool_registry.h"
#include <nuttx/power/battery_ioctl.h>
#include "bk7258_health_service.h"
#include "bk7258_motion_service.h"
#include "bk7258_haptic_service.h"
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
#include "bk7258_display_service.h"
#include "bk7258_control_ota_request.h"
#include "bk7258_cloud_http.h"
#include "bk7258_voice_tls.h"
#include <mbedtls/sha256.h>
#include <netutils/netlib.h>
#endif
#include "voice/audio_capture.h"
#include <media_recorder.h>
#include "bk7258_agent_cloud.h"
#include "bk7258_agent_ota.h"
#include "bk7258_cloud_config.h"
#include "bk7258_provision_identity.h"
#ifdef CONFIG_BK7258_PROVISION_NATIVE
#include "bk7258_provision_bootstrap.h"
#endif
#include "bk7258_provision_claim.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_config.h"
#include "bk7258_provision_storage.h"
#include "bk7258_provision_time.h"
#include "bk7258_provision_owner.h"
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_network.h"
#include "bk7258_agent_trigger.h"
#include "bk7258_voice_media.h"
#ifdef CONFIG_AI_AGENT_LVGL_UI
/* Official declaration of lvgl_ui_channel_show(); the src include path
 * is exposed by agent_framework.cmake.
 */
#include "lvgl_ui_channel.h"
#endif
#include "bk7258_voice_volume_store.h"
#ifdef CONFIG_BK7258_PRODUCT_KEYS
#include "bk7258_voice_button.h"
#ifdef CONFIG_BK7258_PM_SOFT_OFF
#include "bk7258_media_volume.h"
#include <arch/chip/bk7258_pm.h>
#ifdef CONFIG_BK7258_VISION_SERVICE
#include "bk7258_vision_service.h"
#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
#include "bk7258_haptic_service.h"
#endif
#endif
#endif
#include "bk7258_preferences.h"
#ifdef CONFIG_BK7258_PREFERENCES
#include "bk7258_agent_memory.h"
#endif
#include <arch/chip/bk7258_wifi.h>
#ifdef BKAGENT_APP_OTA_ENABLED
#include <arch/chip/bk7258_active_image.h>
#include <arch/chip/bk7258_ota_rpmsg.h>
#endif
#include "bk7258_voice_config.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The device_control tool declares one shared numeric bound so the JSON
 * schema never advertises a value the execution path must reject. Volume
 * uses 0..MAX and vibration uses 1..MAX milliseconds.
 */
#define BK7258_TOOL_VALUE_MAX 100
#define BK7258_STRINGIFY_(x) #x
#define BK7258_STRINGIFY(x) BK7258_STRINGIFY_(x)

#ifdef CONFIG_BK7258_VOICE_TLS
/* The existing provisioning owner borrows this identity for its lifetime.
 * It is never copied or freed while a claim/control session uses it.
 */

static struct bkprov_identity_s g_identity;
static bool g_identity_bound;
static bool g_control_bound;
static bool g_save_first;
static atomic_bool g_probe_running;
static atomic_int g_probe_worker_result;
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
static atomic_bool g_voice_initialized;
static atomic_bool g_agent_ready;
static atomic_int g_voice_event_result;
static atomic_bool g_trigger_prepare_pending = ATOMIC_VAR_INIT(true);
static mutex_t g_persona_lock = NXMUTEX_INITIALIZER;
static atomic_int g_active_persona = ATOMIC_VAR_INIT(-1);
#if defined(CONFIG_BK7258_PRODUCT_KEYS) && defined(CONFIG_BK7258_PM_SOFT_OFF)
static bool g_power_pending;
static bool g_shutdown_requested;
static uint64_t g_shutdown_deadline;
static bool g_power_volume_owned;
static bool g_power_storage_stopped;
static bool g_power_vision_quiesced;
static bool g_power_haptic_quiesced;
static uint64_t g_power_query_at;
#endif

/* Media Trigger reports only product wake admission here. The official voice
 * channel remains the sole conversational lifecycle owner.
 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

void bk7258_agent_product_wake(void)
{
  atomic_fetch_or(&g_product_events, 4);
  sem_post(&g_product_wake);
}

static void bk7258_agent_voice_event(int event, int result)
{
  unsigned int flags = 0;
  if (event == VOICE_CHANNEL_EVENT_INITIALIZED)
    {
      atomic_store(&g_voice_initialized, result == 0);
      flags = 1;
    }
  else if (event == VOICE_CHANNEL_EVENT_SERVICE_READY)
    {
      atomic_store(&g_agent_ready, result == 0);
      flags = 1;
    }
  else if (event == VOICE_CHANNEL_EVENT_TURN_COMPLETE)
    {
      atomic_store(&g_voice_event_result, result);
      flags = 2;
    }

  if (flags)
    {
      atomic_fetch_or(&g_product_events, flags);
      sem_post(&g_product_wake);
    }
}

static void bk7258_agent_storage_changed(void)
{
  atomic_fetch_or(&g_product_events, 8);
  sem_post(&g_product_wake);
}

static bool product_voice_result_exits_interaction(int result)
{
  /* Use the official endpoint/cancel outcomes as the product interaction
   * boundary. Other completed-turn failures have already released their
   * official owners and may be retried without another wake word. If the
   * user stays silent, the next official auto turn exits with -ENODATA.
   */

  return result == -ENODATA || result == -ECANCELED;
}

#ifdef CONFIG_BK7258_PRODUCT_KEYS
static void product_keys_notify(void)
{
  sem_post(&g_product_wake);
}

#ifdef CONFIG_BK7258_PM_SOFT_OFF
static int product_power_restore(void)
{
  int ret;
  if (g_power_storage_stopped)
    {
      ret = bkprov_storage_start("/cpdata/shaniu");
      if (ret < 0)
        {
          return ret;
        }

      g_power_storage_stopped = false;
    }

  if (g_power_volume_owned)
    {
      ret = bk7258_media_volume_release(BK7258_MEDIA_VOLUME_POWER);
      if (ret < 0)
        {
          return ret;
        }

      g_power_volume_owned = false;
    }

#ifdef CONFIG_BK7258_VISION_SERVICE
  if (g_power_vision_quiesced)
    {
      ret = bk7258_vision_quiesce(false);
      if (ret < 0)
        {
          return ret;
        }

      g_power_vision_quiesced = false;
    }

#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  if (g_power_haptic_quiesced)
    {
      ret = bkhaptic_service_quiesce(false);
      if (ret < 0)
        {
          return ret;
        }

      g_power_haptic_quiesced = false;
    }

#endif
  (void)bkprov_owner_quiesce(false);
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  (void)bk7258_display_power(0);
#endif
#ifdef CONFIG_BK7258_PROVISION_NATIVE
  (void)bkprov_bootstrap_start();
#endif
  return 0;
}

static int product_power_request(void)
{
  int ret;
  /* The intent is latched independently of cloud readiness. This final
   * transition still requires actual resource drain; cancellation alone
   * is not evidence of a completed durable transaction.
   */

  if ((atomic_load(&g_voice_initialized) && !voice_channel_is_idle()) ||
      atomic_load(&g_probe_running) || bkprov_owner_busy() ||
      bkprov_network_busy() || bkprov_config_busy() ||
      bkagent_ota_busy() || bk7258_agent_trigger_model_pending())
    {
      return -EBUSY;
    }

#ifdef CONFIG_BK7258_PROVISION_NATIVE
  bkprov_bootstrap_cancel();
  if (bkprov_bootstrap_busy()) return -EBUSY;
#endif
  ret = bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_POWER);
  if (ret < 0)
    {
      return ret;
    }

  g_power_volume_owned = true;
#ifdef CONFIG_BK7258_VISION_SERVICE
  ret = bk7258_vision_quiesce(true);
  if (ret < 0)
    {
      goto restore;
    }

  g_power_vision_quiesced = true;
#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  ret = bkhaptic_service_quiesce(true);
  if (ret < 0)
    {
      goto restore;
    }

  g_power_haptic_quiesced = true;
#endif
  ret = bkprov_storage_stop();
  if (ret < 0)
    {
      goto restore;
    }

  g_power_storage_stopped = true;
  ret = bk7258_agent_trigger_stop();
  if (ret < 0)
    {
      goto restore;
    }

  g_trigger_started = false;
  atomic_store(&g_trigger_prepare_pending, true);
  sync();
  ret = bk7258_pm_soft_off_request();
  if (ret < 0)
    {
      /* A timeout does not mean the CP did not receive it; when the state is
       * unknown, the cleaned-up resource boundary is retained.
       */

      int pending = bk7258_pm_soft_off_status();
      if (pending == 0)
        {
          goto restore;
        }
    }

  g_power_pending = true;
  g_power_query_at = bkvoice_config_now_ms(NULL) + 500;
  return ret;

restore:
{
  int cleanup = product_power_restore();
  if (cleanup < 0)
    {
      g_power_pending = true;
      g_power_query_at = bkvoice_config_now_ms(NULL) + 500;
      return cleanup;
    }
}

  return ret;
}
#endif

static bool product_keys_step(uint64_t now)
{
  unsigned int volume = 0;
  int steps;
  int ret;
  bool power;
  bkvoice_keys_take(&steps, &power);
#if defined(CONFIG_BK7258_PM_SOFT_OFF) && defined(CONFIG_BK7258_DISPLAY_SERVICE)
  if (!g_power_pending && !g_shutdown_requested)
    (void)bk7258_display_power(bkvoice_keys_power_held() ? 1 : 0);
#endif
#ifdef CONFIG_BK7258_PM_SOFT_OFF
  if (g_power_pending)
    {
      if (now >= g_power_query_at)
        {
          ret = bk7258_pm_soft_off_status();
          g_power_query_at = now + 500;
          if (ret == 0)
            {
              ret = product_power_restore();
              if (!ret)
                {
                  g_power_pending = false;
                }

              syslog(LOG_WARNING, "BKKEYS power canceled restore=%d\n", ret);
            }
        }

      return g_power_pending;
    }

#else
  (void)now;
#endif
  if (power)
    {
#ifdef CONFIG_BK7258_PM_SOFT_OFF
      g_shutdown_requested = true;
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
      (void)bk7258_display_power(2);
#endif
      g_shutdown_deadline = now + 30000u;
      if (atomic_load(&g_voice_initialized)) voice_channel_cancel();
      ret = -EINPROGRESS;
#else
      ret = -ENOTSUP;
#endif
      g_product_error = ret;
      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKKEYS power request result=%d\n", ret);
    }
  else if (steps)
    {
      ret = bkagent_ota_busy() ? -EBUSY :
              bkvoice_media_volume_step(steps, &volume);
      if (!ret)
        {
          ret = bkvoice_volume_store_set(volume);
        }

      g_product_error = ret;
      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKKEYS volume steps=%d observed=%u result=%d\n",
             steps, volume, ret);
    }

#ifdef CONFIG_BK7258_PM_SOFT_OFF
  if (g_shutdown_requested)
    {
      /* Commit workers and Wi-Fi lease cleanup must continue while admission
       * is closed. A successful cancel is not a completed shutdown.
       */
      (void)bkprov_owner_quiesce(true);
      (void)bkprov_network_cancel();
      bkprov_network_step();
      bkprov_config_step();
      if (atomic_load(&g_voice_initialized)) (void)voice_channel_recover();
      ret = product_power_request();
      if (g_power_pending)
        {
          g_shutdown_requested = false;
        }
      else if ((ret != -EBUSY && ret != -EAGAIN) ||
               now >= g_shutdown_deadline)
        {
          if (ret == -EBUSY || ret == -EAGAIN) ret = -ETIMEDOUT;
          int restored = product_power_restore();
          g_shutdown_requested = false;
          g_product_error = restored ? restored : ret;
          syslog(LOG_ERR, "BKKEYS shutdown failed=%d restore=%d\n",
                 ret, restored);
        }
    }

  return g_power_pending || g_shutdown_requested;
#else
  return false;
#endif
}
#endif

static int product_apply_persona(int requested)
{
  static const char *const styles[] = {
    "温柔陪伴，耐心回应，不催促用户。",
    "活泼俏皮，轻松有趣，但不打断或嘲笑用户。",
    "安静倾听，简短回应，给用户留出表达空间。",
    "认真交流，表达清楚，帮助用户理清思路。",
    "带一点俏皮的小别扭，但不羞辱、操控或贬低用户。"
};

  struct bk7258_preferences_s preferences;
  FILE *file;
  int ret = nxmutex_lock(&g_persona_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (requested >= 0)
    {
      const char *name = bk7258_preferences_persona_name(requested);
      ret = name ? bk7258_preferences_set_persona(name) : -EINVAL;
    }

  if (!ret)
    {
      ret = bk7258_preferences_get(&preferences);
    }

  if (!ret && requested >= 0 &&
      (preferences.persona_is_default || preferences.persona != requested))
{
  ret = -EIO;
}

  if (!ret && (unsigned int)preferences.persona >=
      sizeof(styles) / sizeof(styles[0]))
{
  ret = -EBADMSG;
}

  /* The persistent fact still lives in the existing SD configuration
   * store. This only projects it into the persona file that the
   * official agent reads per turn; conversation history is not changed,
   * memory is neither migrated nor decrypted, and LLM requests are not
   * rewritten in the protocol adapter. The atomic replacement keeps the
   * Agent from reading a half-written persona file if it happens to start
   * the next turn.
   */

  if (!ret)
    {
      file = fopen(AGENT_SOUL_FILE ".new", "w");
      if (!file)
        {
          ret = -errno;
        }
      else
        {
          if (fprintf(file, "# 傻妞\n\n"
                      "你是运行在 openvela 设备上的 AI 伴侣，"
                      "不是真人。\n"
                      "当前聊天风格：%s\n"
                      "风格只影响表达，"
                      "不改变官方规则和真实能力边界。\n"
                      "硬件动作必须在本轮真实调用工具"
                      "且成功后才能说已完成。"
                      "用户要求再来一次、再次振动时必须"
                      "重新调用工具；"
                      "不得只模仿声音或沿用历史成功。\n",
                      styles[preferences.persona]) < 0) ret = -EIO;
          if (fclose(file) != 0 && !ret)
            {
              ret = -errno;
            }

          if (!ret && rename(AGENT_SOUL_FILE ".new", AGENT_SOUL_FILE) < 0)
            {
              ret = -errno;
            }

          if (ret)
            {
              (void)unlink(AGENT_SOUL_FILE ".new");
            }
        }
    }

  if (!ret)
    {
      atomic_store(&g_active_persona, preferences.persona);
      syslog(LOG_INFO, "BKVOICE persona applied=%s source=%s\n",
             bk7258_preferences_persona_name(preferences.persona),
             preferences.persona_is_default ? "default" : "persistent");
    }

  nxmutex_unlock(&g_persona_lock);
  return ret;
}

static int product_control(void *context, enum bkcontrol_command_e command,
                            uint32_t value,
                            struct bkcontrol_status_s *status)
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
#ifdef BKAGENT_APP_OTA_ENABLED
        {
          struct bk7258_mcuboot_version_s version;
          struct bk7258_ota_pair_snapshot_s pair;
          ret = bk7258_active_ap_image_version(&version);
          if (!ret) ret = bk7258_ota_rpmsg_pair_status(&pair,
            CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
          if (ret)
            {
              return ret;
            }

          if (pair.state != BK7258_OTA_PAIR_CONFIRMED ||
              !pair.security_counter_present || !pair.security_counter ||
              !bk7258_mcuboot_version_equal(&version, &pair.version))
            {
              return -EAGAIN;
            }

          status->device_info.major = version.major;
          status->device_info.minor = version.minor;
          status->device_info.revision = version.revision;
          status->device_info.build = version.build;
          status->device_info.security_counter = pair.security_counter;
          return 0;
        }

#else
        return -ENOTSUP;
#endif
      case BKCONTROL_STATUS: break;
      case BKCONTROL_CANCEL: ret = voice_channel_cancel(); break;
      case BKCONTROL_CLEAR_HISTORY:
        if (!voice_channel_is_idle())
        {
          return -EBUSY;
        }

        ret = session_clear("voice");
#ifdef CONFIG_BK7258_PREFERENCES
        if (!ret)
        {
          ret = bkagent_memory_commit(atomic_load(&g_active_persona), NULL);
        }
#endif
        break;
#ifdef CONFIG_BK7258_PREFERENCES
      case BKCONTROL_MEMORY_SET:
      case BKCONTROL_MEMORY_DELETE:
        if (!voice_channel_is_idle())
        {
          return -EBUSY;
        }

        ret = bkagent_memory_control(command, value,
                                     atomic_load(&g_active_persona));
        break;
#endif
      case BKCONTROL_VOLUME:
        if (value > 100u)
        {
          return -EINVAL;
        }

        if (!voice_channel_is_idle())
        {
          return -EBUSY;
        }

        ret = bkvoice_media_volume(true, value, &volume);
        if (!ret)
        {
          status->volume = volume;
          ret = bkvoice_volume_store_set(volume);
        }
        break;
      case BKCONTROL_PERSONA:
        if (value > BK7258_PERSONA_TSUNDERE_LITE)
        {
          return -EINVAL;
        }

        if (!voice_channel_is_idle())
        {
          return -EBUSY;
        }

        ret = product_apply_persona((int)value);
        break;
      default: return -ENOTSUP;
    }

  if (ret < 0)
    {
      return ret;
    }

  bool idle = voice_channel_is_idle();
  status->flags = (g_configured && bk7258_agent_trigger_armed() ? 1u : 0u) |
                  (!idle || bkagent_ota_busy() ? 2u : 0u) | 16384u;
#ifdef CONFIG_BK7258_PREFERENCES
  status->flags |= bkagent_memory_flags();
#endif
#ifdef BKAGENT_APP_OTA_ENABLED
  status->flags |= 4096u | 8192u;
#endif
  /* SDC1 reports the official whole-turn state. Recorder state is not used
   * as a proxy for Agent, TTS or Media completion.
   */

  status->error = 0;
  if (idle)
    {
      status->flags |= 4u;
      status->turn = BKCONTROL_TURN_IDLE;
      status->error = g_product_error;
    }

  if (idle && command != BKCONTROL_CANCEL &&
      bkvoice_media_volume(false, 0, &volume) == 0)
    {
      status->volume = volume;
      status->flags |= 8u;
    }

  int persona = atomic_load(&g_active_persona);
  if (persona >= 0)
    {
      status->persona = (uint32_t)persona;
      status->flags |= 16u;
    }

  struct bk7258_wifi_result_s wifi;
  if (bk7258_wifi_read_link(&wifi) == 0)
    {
      status->flags |= 1024u;
      if (wifi.ipaddr && bk7258_wifi_native_lease_matches(&wifi))
        {
          status->flags |= 2048u;
        }
    }

  return 0;
}

static char *product_tools(void)
{
  return strdup("[{\"name\":\"device_status\",\"description\":"
    "\"Read this device's speaker volume, mood and cached battery "
    "voltage/charging state. Battery percentage is unavailable: never "
    "estimate it from voltage.\",\"input_schema\":"
    TOOL_NO_PARAMS "}"
#ifdef CONFIG_BK7258_MOTION_SERVICE
    ",{\"name\":\"device_motion\",\"description\":"
    "\"Read fresh three-axis acceleration including gravity from this "
    "board. It has an accelerometer, not a gyroscope; cannot measure "
    "angular velocity.\",\"input_schema\":"
    TOOL_NO_PARAMS "}"
#endif
    ",{\"name\":\"device_control\",\"description\":"
    "\"Only on the user's request, set speaker volume (value 0-100 "
    "percent), mood/chat style (mood), or vibrate briefly (value 1-100 "
    "milliseconds). Every repeat request needs a fresh tool call; never "
    "claim another action based on an earlier success. "
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
    "Use action eyes with expression to show an emotion when requested "
    "or helpful in conversation; local display handles blinking, never "
    "call per frame. "
#endif
    "Report errors honestly.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{"
    "\"action\":{\"type\":\"string\",\"enum\":[\"volume\",\"mood\""
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
    ",\"vibrate\""
#endif
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
    ",\"eyes\""
#endif
    "]},"
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
    "\"expression\":{\"type\":\"string\",\"enum\":[\"neutral\","
    "\"happy\",\"shy\",\"sad\",\"surprised\",\"thinking\",\"listening\","
    "\"speaking\",\"sleepy\"]},"
#endif
    "\"value\":{\"type\":\"integer\",\"minimum\":0,"
    "\"maximum\":" BK7258_STRINGIFY(BK7258_TOOL_VALUE_MAX) "},"
    "\"mood\":{\"type\":\"string\",\"enum\":[\"gentle\",\"playful\","
    "\"quiet\",\"serious\",\"tsundere_lite\"]}},\"required\":[\"action\"]}}"
    ",{\"name\":\"read_file\",\"description\":"
    "\"Read a UTF-8 text file under /data/agent. Use it to open a skill "
    "document under /data/agent/skills before following that skill's "
    "steps.\",\"input_schema\":{\"type\":"
    "\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},"
    "\"required\":[\"path\"]}}]");
}

static int product_tool_execute(const char *name, const char *input,
  char *output, size_t capacity, int (*check)(void *), void *context)
{
  cJSON *args = NULL;
  int ret;
  int written = 0;
  if (!name || (strcmp(name, "device_status") &&
                strcmp(name, "device_motion") &&
                strcmp(name, "device_control") && strcmp(name, "read_file")))
    {
      return ERROR;
    }

  if (!output || !capacity)
    {
      return ERROR;
    }

  output[0] = '\0';
  ret = check ? check(context) : 0;
  if (ret)
    {
      goto out;
    }

  if (!strcmp(name, "read_file"))
    {
      /* Reuses the official file tool to read a skill document; that tool
       * writes the success/failure text into output, and a failure is not
       * rewritten as a device error so the model can tell "skill not
       * readable" apart from "device operation failed".
       */

      ret = tool_read_file_execute(input ? input : "", output, capacity);
      syslog(LOG_INFO, "BKVOICE tool=read_file result=%d\n", ret);
      return OK;
    }

  args = input ? cJSON_Parse(input) : NULL;
  if (!cJSON_IsObject(args))
    {
      ret = -EINVAL;
      goto out;
    }

  if (!strcmp(name, "device_status"))
    {
      char voltage[16] = "null";
      char level[8] = "null";
      const char *state = "unknown";
      const char *mood =
      bk7258_preferences_persona_name(atomic_load(&g_active_persona));
      unsigned int volume;
#ifdef CONFIG_BK7258_HEALTH_SERVICE
      struct bk7258_health_service_snapshot_s health;
      if (!bk7258_health_service_snapshot(&health))
        {
          if (health.flags & BK7258_HEALTH_SNAPSHOT_BATTERY_VOLTAGE_VALID)
            {
              snprintf(voltage, sizeof(voltage), "%ld",
                       (long)health.battery_voltage_mv);
            }

          if (health.flags & BK7258_HEALTH_SNAPSHOT_BATTERY_STATE_VALID)
            {
              switch (health.battery_state)
                {
                  case BATTERY_FULL:
                    state = "full";
                    break;
                  case BATTERY_CHARGING:
                    state = "charging";
                    break;
                  case BATTERY_DISCHARGING:
                    state = "discharging";
                    break;
                  case BATTERY_IDLE:
                    state = "idle";
                    break;
                  case BATTERY_FAULT:
                    state = "fault";
                    break;
                }
            }
        }

#endif
      if (!bkvoice_media_volume(false, 0, &volume))
        {
          snprintf(level, sizeof(level), "%u", volume);
        }

      written = snprintf(output, capacity,
        "{\"battery_mv\":%s,\"battery_state\":\"%s\","
        "\"battery_percent\":null,\"battery_source\":\"periodic_cache\","
        "\"volume_percent\":%s,\"mood\":\"%s\"}",
        voltage, state, level, mood ? mood : "unknown");
    }
#ifdef CONFIG_BK7258_MOTION_SERVICE
  else if (!strcmp(name, "device_motion"))
    {
      struct bkmotion_rpc_response_s sample;
      ret = bk7258_motion_service_sample(&sample);
      if (!ret && sample.sensor_status < 0)
        {
          ret = sample.sensor_status;
        }

      if (ret)
        {
          goto out;
        }

      written = snprintf(output, capacity,
        "{\"sensor\":\"accelerometer\",\"gyroscope_available\":false,"
        "\"unit\":\"mm/s2\",\"timestamp_us\":%llu,"
        "\"x\":%ld,\"y\":%ld,\"z\":%ld}",
        (unsigned long long)sample.timestamp_us,
        (long)sample.x_mms2, (long)sample.y_mms2, (long)sample.z_mms2);
    }
#endif
  else if (!strcmp(name, "device_control"))
    {
      cJSON *action = cJSON_GetObjectItemCaseSensitive(args, "action");
      cJSON *value = cJSON_GetObjectItemCaseSensitive(args, "value");
      cJSON *mood = cJSON_GetObjectItemCaseSensitive(args, "mood");
      unsigned int observed = 0;
    if (bkagent_ota_busy())
      {
        ret = -EBUSY;
        goto out;
      }

    ret = -EINVAL;
    if (!cJSON_IsString(action))
      {
        goto out;
      }

    /* The official Agent tool phase has already stopped capture and has not
     * started playback yet; the existing product owner is reused, the App's
     * control restriction on a busy turn is not relaxed, and GPIO is never
     * driven directly.
     */

      if (!strcmp(action->valuestring, "mood") && cJSON_IsString(mood))
        {
          for (int i = 0; i <= BK7258_PERSONA_TSUNDERE_LITE; i++)
            {
              if (!strcmp(mood->valuestring,
                          bk7258_preferences_persona_name(i)))
                {
                  ret = product_apply_persona(i);
                  break;
                }
            }
        }
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
      else if (!strcmp(action->valuestring, "eyes"))
        {
          static const char *const expressions[] =
            {
              "neutral", "happy", "shy", "sad", "surprised", "thinking",
              "listening", "speaking", "sleepy"
            };

          cJSON *expression =
            cJSON_GetObjectItemCaseSensitive(args, "expression");
          if (cJSON_IsString(expression))
            {
              for (unsigned int i = 0;
                   i < sizeof(expressions) / sizeof(expressions[0]); i++)
                {
                  if (!strcmp(expression->valuestring, expressions[i]))
                    {
                      ret = bk7258_display_set_expression(expressions[i]);
                      break;
                    }
                }
            }
        }
#endif
      else if (cJSON_IsNumber(value) &&
               value->valuedouble == value->valueint &&
               value->valueint >= 0)
        {
          if (!strcmp(action->valuestring, "volume") &&
              value->valueint <= BK7258_TOOL_VALUE_MAX)
            {
              ret = bkvoice_media_volume(true, value->valueint, &observed);
              if (!ret)
                {
                  ret = bkvoice_volume_store_set(observed);
                }
            }
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
          else if (!strcmp(action->valuestring, "vibrate") &&
                   value->valueint > 0 &&
                   value->valueint <= BK7258_TOOL_VALUE_MAX)
            {
              ret = bkhaptic_service_pulse_wait(value->valueint);
            }
#endif
        }

      if (ret)
        {
          goto out;
        }

      if (!strcmp(action->valuestring, "volume"))
        {
          written = snprintf(output, capacity,
                             "{\"ok\":true,\"volume_percent\":%u}",
                             observed);
        }
      else
        {
          written = snprintf(output, capacity,
                             "{\"ok\":true,\"action\":\"%s\"}",
                             action->valuestring);
        }
    }
  else
    {
      ret = -ENOTSUP;
    }

  if (!ret && (written < 0 || (size_t)written >= capacity))
    {
      ret = -ENOSPC;
    }

out:
  cJSON_Delete(args);
  if (ret)
    {
      snprintf(output, capacity,
               "Error: device operation failed (%d); do not claim success "
               "or invent a reading.", ret);
    }

  syslog(LOG_INFO, "BKVOICE device tool=%s result=%d\n", name, ret);
  return OK;
}

static bool product_available(void *unused)
{
  (void)unused;
  return atomic_load(&g_agent_core_ready) &&
         atomic_load(&g_voice_initialized) &&
         !atomic_load(&g_probe_running) &&
         voice_channel_is_idle() &&
         !bkagent_ota_busy() && !bkprov_network_busy();
}

static int product_models(enum bkcontrol_command_e command, uint32_t offset,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status);

static int product_response_mode(enum bkcontrol_command_e command,
  uint32_t offset, const uint8_t *record, size_t size,
  struct bkcontrol_status_s *status)
{
#ifdef CONFIG_BK7258_PREFERENCES
  bool enabled = false;
  int ret = bkagent_cloud_get_thinking(&enabled);
  if (ret && ret != -EAGAIN)
    {
      return ret;
    }

  if (command == BKCONTROL_CONFIG_READ)
    {
      if (ret)
        {
          return ret;
        }

      if (offset)
        {
          return -ERANGE;
        }

      status->config_total = 12;
      memset(status->config_chunk, 0, sizeof(status->config_chunk));
      memcpy(status->config_chunk, "RSP1", 4);
      status->config_chunk[7] = enabled ? 1 : 0;
      return 0;
    }

  if (command != BKCONTROL_CONFIG_BEGIN && command != BKCONTROL_CONFIG_APPLY)
    {
      return -EINVAL;
    }

  if (!g_cloud_loaded)
    {
      return -EAGAIN;
    }

  if (!voice_channel_is_idle() || bkprov_network_busy())
    {
      return -EBUSY;
    }

  if (size != 12)
    {
      return -EMSGSIZE;
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return 0;
    }

  if (!record || memcmp(record, "RSP1", 4) || record[4] || record[5] ||
      record[6] || record[7] > 1 || record[8] || record[9] || record[10] ||
      record[11]) return -EBADMSG;
  bool requested = record[7] != 0;
  ret = bk7258_preferences_thinking_set(requested);
  if (!ret)
    {
      ret = bk7258_preferences_thinking_get(&enabled);
    }

  if (!ret && enabled != requested)
    {
      ret = -EIO;
    }

  if (!ret)
    {
      bkagent_cloud_set_thinking(enabled);
    }

  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE response mode thinking=%d saved=%d result=%d\n",
         requested, ret == 0, ret);
  return ret;
#else
  return -ENOTSUP;
#endif
}

static int product_wake_threshold(enum bkcontrol_command_e command,
  uint32_t offset, const uint8_t *record, size_t size,
  struct bkcontrol_status_s *status)
{
#ifdef CONFIG_BK7258_PREFERENCES
  if (command == BKCONTROL_CONFIG_READ)
    {
      if (offset)
        {
          return -ERANGE;
        }

      status->config_total = 12;
      memset(status->config_chunk, 0, sizeof(status->config_chunk));
      memcpy(status->config_chunk, "KWT1", 4);
      status->config_chunk[7] = bk7258_agent_trigger_threshold_get();
      return 0;
    }

  if (command != BKCONTROL_CONFIG_BEGIN && command != BKCONTROL_CONFIG_APPLY)
    {
      return -EINVAL;
    }

  if (!voice_channel_is_idle() || bk7258_agent_trigger_model_pending())
    {
      return -EBUSY;
    }

  if (size != 12)
    {
      return -EMSGSIZE;
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return 0;
    }

  if (!record || memcmp(record, "KWT1", 4) || record[4] || record[5] ||
      record[6] || record[7] < 50 || record[7] > 90 || record[8] ||
      record[9] || record[10] || record[11]) return -EBADMSG;
  unsigned int observed = 0;
  int ret = bk7258_preferences_wake_threshold_set(record[7]);
  if (!ret)
    {
      ret = bk7258_preferences_wake_threshold_get(&observed);
    }

  if (!ret && observed != record[7])
    {
      ret = -EIO;
    }

  if (!ret)
    {
      ret = bk7258_agent_trigger_threshold_set(observed);
    }

  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE wake threshold percent=%u saved=%d result=%d\n",
         record[7], ret == 0, ret);
  return ret;
#else
  return -ENOTSUP;
#endif
}

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
static int product_asset_time(void *unused)
{
  uint64_t utc;
  (void)unused;
  return bkprov_time_get(0, &utc);
}

static int product_install_eyes(const uint8_t *record, size_t size)
{
  struct asset_download_s
  {
    struct bkcontrol_ota_request_s source;
    struct bkcloud_config_s endpoint;
    struct bkcloud_http_s http;
    struct bkvoice_tls_s tls;
    mbedtls_x509_crt ca;
    char data[131073];
  };

  struct asset_download_s *download = calloc(1, sizeof(*download));

  if (!download)
    {
      return -ENOMEM;
    }

  uint32_t generation = bkprov_gatt_generation();
  mbedtls_x509_crt_init(&download->ca);
  int ret = bkcontrol_eye_request_parse(record, size, &download->source);
  char scheme[8];
  char path[256];
  struct url_s url =
    {
      .scheme = scheme, .schemelen = sizeof(scheme),
      .host = download->endpoint.host,
      .hostlen = sizeof(download->endpoint.host),
      .port = 443, .path = path, .pathlen = sizeof(path)
    };

  if (!ret)
    {
      ret = netlib_parseurl(download->source.url, &url);
    }

  if (!ret)
    {
      download->endpoint.port = url.port;
      ret = mbedtls_x509_crt_parse(&download->ca,
          (const unsigned char *)download->source.ca_pem,
          strlen(download->source.ca_pem) + 1);
      if (ret)
        {
          ret = -EKEYREJECTED;
        }
    }

  struct bkvoice_tls_config_s tls =
    {
      .server_ca = &download->ca,
      .trusted_time = product_asset_time, .now_ms = bkvoice_config_now_ms,
      .server_auth_only = true
    };

  memcpy(&tls.peer_address.s_addr, download->source.ipv4, 4);
  if (!ret)
    {
      ret = bkvoice_tls_initialize(&download->tls, &tls);
    }

  if (!ret) ret = bkcloud_http_get(&download->http, &download->endpoint,
      download->source.url, bkvoice_tls_ops(), &download->tls,
      bkvoice_config_now_ms(NULL) + 8000, download->data,
      sizeof(download->data));
  uint8_t digest[32];
  if (!ret && download->http.received < 128)
    {
      ret = -EBADMSG;
    }

  if (!ret) ret = mbedtls_sha256((const unsigned char *)download->data,
                                download->http.received, digest, 0);
  if (!ret && memcmp(digest, download->source.catalog_sha256,
                     sizeof(digest)))
    ret = -EBADMSG;
  /* Resources are not activated once the control connection is gone; the
   * download borrows no cloud session and changes no firmware update state.
   */

  if (!ret && generation != bkprov_gatt_generation())
    {
      ret = -ECANCELED;
    }

  if (download->tls.initialized)
    {
      bkvoice_tls_uninitialize(&download->tls);
    }

  if (!ret)
    {
      ret = bk7258_display_import(download->data, download->http.received);
    }

  syslog(ret ? LOG_WARNING : LOG_INFO,
      "BKDISPLAY APP IMPORT transport=https result=%d bytes=%u\n",
      ret, (unsigned int)download->http.received);
  mbedtls_x509_crt_free(&download->ca);
  free(download);
  return ret;
}
#endif

static int product_config(void *context, enum bkcontrol_command_e command,
  uint32_t kind, uint32_t offset, const uint8_t *record, size_t size,
  struct bkcontrol_status_s *status)
{
  if (bkagent_ota_busy())
    {
      return -EBUSY;
    }

  if (kind == BKCONTROL_CONFIG_SETTINGS)
    {
      return bkprov_config_control(command, offset, record, size, status);
    }

  if (kind == BKCONTROL_CONFIG_CLOUD_MODELS)
    {
      return product_models(command, offset, record, size, status);
    }

  if (kind == BKCONTROL_CONFIG_RESPONSE_MODE)
    {
      return product_response_mode(command, offset, record, size, status);
    }

  if (kind == BKCONTROL_CONFIG_WAKE_THRESHOLD)
    {
      return product_wake_threshold(command, offset, record, size, status);
    }

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  if (kind == BKCONTROL_CONFIG_EYE_PACK)
    {
      if (command == BKCONTROL_CONFIG_READ)
        {
          struct bkdisplay_service_status_s display;
          uint8_t wire[108] =
            {
              'E', 'Y', 'E', '1'
            };

          int ret = bk7258_display_get_status(&display);
          if (ret)
            {
              return ret;
            }

          uint32_t fields[] =
            {
              display.state, (uint32_t)display.last_error,
              display.pack_revision, display.render_sequence
            };

          for (unsigned int i = 0; i < 4; i++)
            {
              for (unsigned int byte = 0; byte < 4; byte++)
                {
                  wire[4 + i * 4 + byte] = fields[i] >> (24 - byte * 8);
                }
            }

          memcpy(wire + 20, display.pack_id, sizeof(display.pack_id));
          memcpy(wire + 52, display.source_sha256,
                 sizeof(display.source_sha256));
          memcpy(wire + 84, display.expression, sizeof(display.expression));
          if (offset >= sizeof(wire) || (offset & 15u))
            {
              return -ERANGE;
            }

          size_t count = sizeof(wire) - offset;
          if (count > sizeof(status->config_chunk))
            {
              count = sizeof(status->config_chunk);
            }

          status->config_total = sizeof(wire);
          memset(status->config_chunk, 0, sizeof(status->config_chunk));
          memcpy(status->config_chunk, wire + offset, count);
          return 0;
        }

      if (command != BKCONTROL_CONFIG_BEGIN &&
          command != BKCONTROL_CONFIG_APPLY)
        {
          return -EINVAL;
        }

      if (size < 44 || size > BKCONTROL_OTA_RECORD_MAX)
        {
          return -EMSGSIZE;
        }

      if (!voice_channel_is_idle() || bkprov_network_busy() ||
          bk7258_agent_trigger_model_pending()) return -EBUSY;
      if (command == BKCONTROL_CONFIG_BEGIN)
        {
          return 0;
        }

      return product_install_eyes(record, size);
    }

#endif
  return bk7258_agent_trigger_control(context, command, kind, offset,
                                     record, size, status);
}

#ifdef BKAGENT_APP_OTA_ENABLED
static int product_ota(void *context, enum bkcontrol_command_e command,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  if (command == BKCONTROL_OTA_START)
    {
      if (bkagent_ota_busy() || !g_identity_bound ||
          !voice_channel_is_idle() || bkprov_network_busy() ||
          bk7258_agent_trigger_model_pending()) return -EBUSY;
      int ret = 0;
      if (g_trigger_started)
        {
          ret = bk7258_agent_trigger_stop();
          if (ret)
            {
              return ret;
            }

          g_trigger_started = false;
        }

      atomic_store(&g_trigger_prepare_pending, true);
    }

  return bkagent_ota_control(context, command, record, size, status);
}
#endif

static int product_load_legacy(void *unused, const void *data, size_t size)
{
  (void)unused;
  (void)data;
  (void)size;
  return -ENOTSUP;
}

static int product_load_cloud_models(const void *trust, size_t trust_size,
                                     const void *cloud, size_t cloud_size,
                                     const struct bkcloud_models_s *models)
{
  struct bkcloud_config_s *decoded = calloc(1, sizeof(*decoded));
  if (!decoded)
    {
      return -ENOMEM;
    }

  int ret = bkcloud_config_decode(decoded, cloud, cloud_size);
  if (!ret) ret = models ?
    bkagent_cloud_configure_models(trust, trust_size, cloud, cloud_size,
                                   models) :
    bkagent_cloud_configure(trust, trust_size, cloud, cloud_size);
  if (!ret)
    {
      const char *backend = decoded->dialect == 2 ? "mimo" :
                            decoded->dialect == 1 ? "openai-audio" : NULL;
      char asr[64] =
        {
          0
        };

      char tts[64] =
        {
          0
        };

      char location[32] =
        {
          0
        };

      bool device_tts = false;
      if (!backend)
        {
          ret = -ENOTSUP;
        }

      if (!ret)
        {
          (void)claw_config_get("asr_backend", asr, sizeof(asr));
        }

      if (!ret) (void)claw_config_get(AGENT_CFG_KEY_TTS_BACKEND,
                                     tts, sizeof(tts));
      if (!ret) (void)claw_config_get(AGENT_CFG_KEY_TTS_LOCATION,
                                     location, sizeof(location));
      if (!ret && asr[0] && strcmp(asr, backend))
        {
          ret = -EPERM;
        }

      if (!ret && !asr[0])
        {
          snprintf(asr, sizeof(asr), "%s", backend);
        }

      if (!ret && (!location[0] || !strcmp(location, "remote")))
        {
          if (tts[0] && strcmp(tts, backend))
            {
              ret = -EPERM;
            }
          else if (!tts[0])
            {
              snprintf(tts, sizeof(tts), "%s", backend);
            }
        }
      else if (!ret && !strcmp(location, "device"))
        {
          device_tts = true;
          if (!tts[0] || !strcmp(tts, backend))
            {
              ret = -ENOTSUP;
            }
        }
      else if (!ret)
        {
          ret = -EINVAL;
        }

      if (!ret)
        {
          ret = voice_asr_set_backend(asr);
        }

      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKVOICE ASR activation backend=%s result=%d\n", asr, ret);
      if (!ret)
        {
          ret = bkagent_cloud_activate_llm();
        }

      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKVOICE LLM activation result=%d\n", ret);
      if (!ret)
        {
          ret = tts[0] ? voice_tts_set_backend(tts) : -ENOTSUP;
        }

      if (!ret)
        {
          voice_tts_capabilities_t caps;
          ret = voice_tts_get_capabilities(&caps);
          if (!ret && (device_tts ?
              caps.location != VOICE_TTS_LOCATION_DEVICE ||
              caps.needs_network :
              caps.location != VOICE_TTS_LOCATION_REMOTE ||
              !caps.needs_network))
            {
              ret = -EPERM;
            }
        }

      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKVOICE TTS activation backend=%s location=%s result=%d\n",
             tts, device_tts ? "device" : "remote", ret);
    }

  g_cloud_loaded = ret == 0;
  g_service_result = ret ? ret : -EAGAIN;
  g_probe_result = g_service_result;
  bkcloud_config_clear(decoded);
  free(decoded);
  return ret;
}

static int product_load_cloud(void *unused, const void *trust,
                              size_t trust_size, const void *cloud,
                              size_t cloud_size)
{
  (void)unused;
  if (!g_save_first)
    return product_load_cloud_models(trust, trust_size, cloud, cloud_size, NULL);
  /* SCB4 is authoritative: an old separate preferences override must not
   * silently replace models just saved through the authenticated editor. */
  struct bkcloud_config_s *decoded = calloc(1, sizeof(*decoded));
  if (!decoded) return -ENOMEM;
  struct bkcloud_models_s models = {0};
  int ret = bkcloud_config_decode(decoded, cloud, cloud_size);
  if (!ret)
    {
      memcpy(models.asr_model, decoded->asr_model, sizeof(models.asr_model));
      memcpy(models.chat_model, decoded->chat_model, sizeof(models.chat_model));
      memcpy(models.tts_model, decoded->tts_model, sizeof(models.tts_model));
      ret = product_load_cloud_models(trust, trust_size, cloud, cloud_size, &models);
    }
  bkcloud_config_clear(decoded);
  free(decoded);
  return ret;
}

static int product_models(enum bkcontrol_command_e command, uint32_t offset,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  struct bkcloud_models_s models =
    {
      0
    };

  struct bkcloud_models_s previous =
    {
      0
    };

  int ret;
  if (command == BKCONTROL_CONFIG_READ)
    {
      uint8_t wire[BKCLOUD_MODELS_RECORD_MAX];
      size_t total = 0;
      if (!g_cloud_loaded)
        {
          return g_product_error ? g_product_error : -EAGAIN;
        }

      ret = bkagent_cloud_models_get(&models);
      if (!ret)
        {
          ret = bkcloud_models_encode(&models, wire, sizeof(wire), &total);
        }

      if (ret)
        {
          return ret;
        }

      if (offset >= total || (offset & 15u))
        {
          return -ERANGE;
        }

      status->config_total = total;
      memset(status->config_chunk, 0, sizeof(status->config_chunk));
      size_t count = total - offset;
      if (count > sizeof(status->config_chunk))
        {
          count = sizeof(status->config_chunk);
        }

      memcpy(status->config_chunk, wire + offset, count);
      return 0;
    }

  if (command != BKCONTROL_CONFIG_BEGIN && command != BKCONTROL_CONFIG_APPLY)
    {
      return -EINVAL;
    }

  if (g_save_first) return -ENOTSUP; /* Use the independent SCP1 settings editor. */
  if (atomic_load(&g_probe_running)) return -EBUSY;
  if (size < 12 || size > BKCLOUD_MODELS_RECORD_MAX)
    {
      return -EMSGSIZE;
    }

  if (!g_identity_bound)
    {
      return -ENOKEY;
    }

  if (!voice_channel_is_idle() || bkprov_network_busy() ||
      bk7258_agent_trigger_model_pending()) return -EBUSY;
  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return 0;
    }

  ret = bkcloud_models_decode(&models, record, size);
  if (ret)
    {
      return ret;
    }

  if (!g_cloud_loaded)
    {
      return g_product_error ? g_product_error : -EAGAIN;
    }

  ret = bkagent_cloud_models_get(&previous);
  if (ret)
    {
      return ret;
    }

  /* Reuse the accepted protected configuration and the normal backend
   * activation path. This changes public model names, never Wi-Fi, trust,
   * identity or the independent choice of a local TTS backend.
   */

  struct model_workspace_s
  {
    uint8_t bundle[BKPROV_BUNDLE_MAX];
    uint8_t voice[BKVOICE_CONFIG_MAX_BYTES];
    uint8_t transaction[16];
    struct bkprov_settings_s settings;
  };

  struct model_workspace_s *work = calloc(1, sizeof(*work));

  if (!work)
    {
      return -ENOMEM;
    }

  size_t bundle_size = 0;
  size_t voice_size = 0;
  uint64_t revision = 0;
  ret = bkprov_storage_snapshot(work->bundle, sizeof(work->bundle),
                                &bundle_size, &revision, work->transaction);
  if (!ret && revision != g_config_revision)
    {
      ret = -EAGAIN;
    }

  if (!ret)
    {
      ret = bkprov_settings_decode(&work->settings, work->bundle,
                                   bundle_size);
    }

  if (!ret && !work->settings.cloud_size)
    {
      ret = -ENOTSUP;
    }

  /* The persisted UTC is a floor, not the current time. Match the existing
   * restore path before the config loader installs a fresh clock anchor.
   */

  if (!ret)
    {
      ret = bkprov_time_get(work->settings.utc, &work->settings.utc);
    }

  if (!ret) ret = bkprov_settings_voice(&work->settings,
    g_identity.record + 48, g_identity.certificate_size,
    g_identity.record + 48 + g_identity.certificate_size,
    g_identity.key_size, work->voice, sizeof(work->voice), &voice_size);
  if (!ret && g_trigger_started)
    {
      ret = bk7258_agent_trigger_stop();
      if (!ret)
        {
          g_trigger_started = false;
        }
    }

  if (!ret)
    {
      g_configured = false;
      ret = product_load_cloud_models(work->voice, voice_size,
        work->settings.cloud, work->settings.cloud_size, &models);
      if (!ret)
        {
          g_probe_result = bkagent_cloud_verify_service();
          g_service_result = g_probe_result;
          ret = g_probe_result;
        }

      if (!ret)
        {
          ret = bk7258_preferences_cloud_models_set(&models);
        }

      if (!ret)
        {
          g_configured = g_service_result == 0;
        }
      else
        {
          int failure = ret;
          int restored = product_load_cloud_models(work->voice, voice_size,
            work->settings.cloud, work->settings.cloud_size, &previous);
          if (!restored)
            {
              g_probe_result = bkagent_cloud_verify_service();
              g_service_result = g_probe_result;
              restored = g_probe_result;
            }

          g_configured = restored == 0;
          if (restored)
            {
              ret = restored;
            }
          else ret = failure;
          syslog(restored ? LOG_ERR : LOG_WARNING,
                 "BKVOICE cloud model rollback candidate=%d restored=%d\n",
                 failure, restored);
        }
    }

  g_product_error = ret;
  mbedtls_platform_zeroize(&previous, sizeof(previous));
  mbedtls_platform_zeroize(&models, sizeof(models));
  mbedtls_platform_zeroize(work, sizeof(*work));
  free(work);
  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE cloud model apply result=%d ready=%d\n",
         ret, g_configured);
  return ret;
}

static void *product_probe_worker(void *unused)
{
  (void)unused;
  int ret = bkagent_cloud_verify_service();
  atomic_store(&g_probe_worker_result, ret);
  atomic_store(&g_probe_running, false);
  bk7258_agent_product_wake();
  return NULL;
}

static int product_connect(void *unused)
{
  pthread_attr_t attr;
  pthread_t thread;
  int ret;
  bool expected = false;
  (void)unused;
  if (!atomic_compare_exchange_strong(&g_probe_running, &expected, true))
    return -EBUSY;
  g_probe_result = -EAGAIN;
  atomic_store(&g_probe_worker_result, -EAGAIN);
  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      if (ret == 0) ret = pthread_attr_setstacksize(&attr, 8192);
      if (ret == 0)
        ret = pthread_create(&thread, &attr, product_probe_worker, NULL);
      pthread_attr_destroy(&attr);
    }

  if (ret != 0) atomic_store(&g_probe_running, false);
  return -ret;
}

static int product_ready(void *unused)
{
  (void)unused;
  if (atomic_load(&g_probe_running)) return 0;
  g_probe_result = atomic_load(&g_probe_worker_result);
  g_service_result = g_probe_result;
  return g_probe_result == 0 ? 1 : g_probe_result;
}

static int product_clear(void *unused)
{
  (void)unused;
  if (atomic_load(&g_probe_running) ||
      (atomic_load(&g_voice_initialized) && !voice_channel_is_idle()))
    {
      return -EBUSY;
    }

  if (g_trigger_started)
    {
      int ret = bk7258_agent_trigger_stop();
      if (ret < 0)
        {
          return ret;
        }

      g_trigger_started = false;
    }

  int ret = bkagent_cloud_clear();
  if (ret < 0)
    {
      return ret;
    }

  g_configured = false;
  g_cloud_loaded = false;
  g_service_result = -ENOTCONN;
  g_probe_result = -ENOTCONN;
  atomic_store(&g_trigger_prepare_pending, true);
  return 0;
}

static const struct bkprov_voice_ops_s g_provision_voice =
{
  product_available, product_load_legacy, product_connect, product_ready,
  product_clear, product_load_cloud
};

static int product_capture_route(int active)
{
  return bkvoice_media_source_set_active(MEDIA_SOURCE_MIC, active != 0);
}

/* One bounded, zeroized workspace for protected storage reads. Parsed
 * identity owns its own allocation; settings borrow only this workspace.
 */

struct agent_config_workspace_s
{
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
  if (bkprov_owner_pairing() || bkprov_network_busy())
    {
      return -EBUSY;
    }

  struct agent_config_workspace_s *work = calloc(1, sizeof(*work));
  if (!work)
    {
      return -ENOMEM;
    }

  size_t size = 0;
  uint64_t revision = 0;
  uint8_t transaction[16] =
    {
      0
    };

  int ret = 0;
  if (!g_identity_bound)
    {
#ifdef CONFIG_BK7258_PROVISION_NATIVE
      ret = bkprov_bootstrap_status();
      if (ret < 0) { *storage_waiting = true; goto out; }
#endif
      ret = bkprov_storage_identity(work->identity,
                                  sizeof(work->identity), &size);
      *storage_waiting = storage_unavailable(ret);
      if (!ret)
        {
          ret = bkprov_identity_load(&g_identity, work->identity, size);
        }

      if (!ret)
        {
          ret = bkprov_network_bind(&g_identity, &g_provision_voice, NULL);
        }

      if (!ret) ret = bkprov_owner_bind(&g_identity.certificate,
                                        &g_identity.key, g_identity.secret,
                                        bkprov_network_ops(), NULL);
      if (ret < 0)
        {
          (void)bkprov_network_unbind();
          bkprov_identity_clear(&g_identity);
          goto out;
        }

#ifdef CONFIG_BK7258_PROVISION_NATIVE
      if (g_identity.generated)
        ret = bkprov_owner_window_handler(bkprov_bootstrap_window, &g_identity);
      if (ret < 0) { (void)bkprov_owner_unbind(); goto out; }
#endif
      g_identity_bound = true;
    }

  ret = bkprov_storage_snapshot(work->bundle, sizeof(work->bundle), &size,
                                &revision, transaction);
  *storage_waiting = storage_unavailable(ret);
  if (ret < 0)
    {
      goto out;
    }

  ret = bkprov_settings_decode(&work->settings, work->bundle, size);
  if (ret < 0)
    {
      goto out;
    }

  if (!work->settings.control_key)
    {
      ret = -ENOKEY;
      goto out;
    }

  if (!g_control_bound)
    {
    ret = bkprov_owner_control(work->settings.control_key, product_control,
                               NULL);
    if (!ret)
      {
        ret = bkprov_owner_control_config(product_config);
      }

  #ifdef BKAGENT_APP_OTA_ENABLED
    if (!ret)
      {
        ret = bkprov_owner_control_ota(product_ota);
      }

  #endif
    if (ret < 0)
      {
        goto out;
      }

      g_control_bound = true;
    }

#ifdef CONFIG_BK7258_PREFERENCES
  (void)bkagent_memory_bind(work->settings.control_key);
#endif
  g_save_first = work->settings.deferred;
  if (!work->settings.ssid[0])
    {
      g_config_revision = revision;
      g_configured = false;
      ret = 0;
      goto out;
    }

  if (!atomic_load(&g_agent_core_ready) ||
      !atomic_load(&g_voice_initialized))
    {
      ret = -EBUSY;
      goto out;
    }

  if (g_configured && revision == g_config_revision)
    {
      goto out;
    }

  ret = product_clear(NULL);
  if (!ret)
    {
      ret = bkprov_network_restore(work->bundle, size);
    }

  if (!ret)
    {
      g_config_revision = revision;
    }

out:
  mbedtls_platform_zeroize(work, sizeof(*work));
  free(work);
  return ret;
}

static int bk7258_agent_config_task(int argc, FAR char *argv[])
{
  enum voice_action_e
    {
      VOICE_ACTION_NONE = 0,
      VOICE_ACTION_CONTINUE,
      VOICE_ACTION_REARM
    };

  bool pending = true;
  bool network_was_busy = false;
  bool storage_waiting = false;
  bool voice_interaction_active = false;
  bool preferences_pending = false;
  uint32_t connection_generation = bkprov_gatt_generation();
  uint64_t link_check_at = 0;
  bool link_expected = false;
#ifdef CONFIG_BK7258_PROVISION_NATIVE
  int bootstrap_observed = -EAGAIN;
#endif
  uint64_t storage_deadline = 0;
  uint64_t storage_retry_at = 0;
  uint64_t voice_cleanup_at = 0;
  uint64_t voice_action_at = 0;
  uint64_t preferences_retry_at = 0;
  uint64_t trigger_retry_at = 0;
  uint64_t network_retry_at = 0;
  uint32_t network_backoff = 5000;
  int voice_turn_result = 0;
  enum voice_action_e voice_action = VOICE_ACTION_NONE;
  (void)argc; (void)argv;
  while (1)
    {
      /* The existing BLE TLS/claim and Wi-Fi trial owners need their
       * documented step cadence. Configuration activation itself is
       * event driven.
       */

      struct timespec deadline;
      clock_gettime(CLOCK_REALTIME, &deadline);
      deadline.tv_nsec += 20000000;
      if (deadline.tv_nsec >= 1000000000)
        {
          deadline.tv_sec++;
          deadline.tv_nsec -= 1000000000;
        }

      int waited;
      do
        {
          waited = sem_timedwait(&g_product_wake, &deadline);
        }
      while (waited < 0 && errno == EINTR);

      if (waited < 0 && errno != ETIMEDOUT)
        {
          return -errno;
        }

      unsigned int events = atomic_exchange(&g_product_events, 0);
      bkagent_ota_poll();
      uint64_t now = bkvoice_config_now_ms(NULL);
#ifdef CONFIG_BK7258_PRODUCT_KEYS
      if (product_keys_step(now))
        {
          continue;
        }

#endif
      if (now >= voice_cleanup_at)
        {
          int cleanup = voice_channel_recover();
          voice_cleanup_at = now + 500;
          if (cleanup < 0 && cleanup != -EBUSY)
            {
              g_product_error = cleanup;
            }
        }

      if (events & 2)
        {
          voice_turn_result = atomic_load(&g_voice_event_result);
          g_product_error = voice_turn_result;
          /* TURN_COMPLETE is the official per-turn resource boundary, not
           * the product interaction boundary. One admitted wake keeps
           * using official automatic turns until the official endpoint or
           * cancel outcome exits.
           */

          voice_action = voice_interaction_active &&
                         !product_voice_result_exits_interaction(
                           voice_turn_result) ?
                         VOICE_ACTION_CONTINUE : VOICE_ACTION_REARM;
          if (voice_action == VOICE_ACTION_REARM)
            voice_interaction_active = false;
          voice_action_at = now;
        }

      if (voice_action != VOICE_ACTION_NONE && now >= voice_action_at)
        {
          enum voice_action_e attempted = voice_action;
          int action;
          if (voice_action == VOICE_ACTION_CONTINUE)
            {
              action = voice_channel_start_auto();
            }
          else
            {
              action = bk7258_agent_trigger_rearm();
            }

          if (action == 0 || (voice_action == VOICE_ACTION_REARM &&
                              bk7258_agent_trigger_armed()))
            {
              voice_action = VOICE_ACTION_NONE;
            }
          else
            {
              if (voice_action == VOICE_ACTION_CONTINUE && action != -EBUSY)
                {
                  voice_interaction_active = false;
                  voice_action = VOICE_ACTION_REARM;
                }

              voice_action_at = now + 100;
              g_product_error = action;
            }

          syslog(action ? LOG_WARNING : LOG_INFO,
                 "BKVOICE turn complete=%d interaction=%s next=%s "
                 "result=%d\n",
                 voice_turn_result,
                 attempted == VOICE_ACTION_REARM ? "exit" : "active",
                 attempted == VOICE_ACTION_REARM ? "wake" : "capture",
               action);
        }

      if (events & 8)
        {
          pending = true;
          network_retry_at = 0;
          network_backoff = 5000;
          atomic_store(&g_trigger_prepare_pending, true);
        }

      uint32_t generation = bkprov_gatt_generation();
      if (!storage_deadline)
        {
          storage_deadline = now + 15000;
        }

      if (generation != connection_generation)
        {
          connection_generation = generation;
          /* A later App connection can retry a mount that missed the
           * bounded startup window. It cannot bypass identity or trust
           * validation.
           */

          if (storage_waiting)
            {
              storage_deadline = now + 15000;
              storage_retry_at = now;
            }
        }

      if (storage_retry_at && now >= storage_retry_at)
        {
          storage_retry_at = 0;
          if (now >= storage_deadline)
            {
              syslog(LOG_WARNING,
                     "BKVOICE storage readiness window expired result=%d\n",
                     g_product_error);
            }
          else
            {
              int ret = bkprov_storage_refresh();
              if (ret == -EBUSY)
                {
                  storage_retry_at = now + 250;
                }
              else if (ret)
                {
                  g_product_error = ret;
                }

              /* The existing storage worker owns I/O and publishes
               * completion.
               */
            }
        }

#ifdef CONFIG_BK7258_PROVISION_NATIVE
      int bootstrap_now = bkprov_bootstrap_status();
      if (bootstrap_now != bootstrap_observed)
        {
          bootstrap_observed = bootstrap_now;
          if (bootstrap_now >= 0) pending = true;
        }
#endif
      bkprov_config_step();
      bkprov_network_step();
      bool network_busy = bkprov_network_busy();
      if (network_was_busy && !network_busy)
        {
          int network_result = bkprov_network_result();
          struct bk7258_wifi_result_s link = {0};
          if (bk7258_wifi_read_link(&link) == 0)
            link_expected = link.link_state == BK7258_WIFI_LINK_CONNECTED && link.ipaddr != 0;
          g_configured = network_result == 0 && g_service_result == 0;
          g_product_error = network_result ? network_result : g_service_result;
          if (network_result && network_result != -ECANCELED)
            {
              network_retry_at = now + network_backoff;
              if (network_backoff < 60000) network_backoff *= 2;
              if (network_backoff > 60000) network_backoff = 60000;
            }
          else if (network_result == 0)
            {
              network_retry_at = 0;
              network_backoff = 5000;
            }
          syslog(g_configured ? LOG_INFO : LOG_WARNING,
                 "BKVOICE configuration ready=%d result=%d revision=%llu\n",
                 g_configured, g_product_error,
                 (unsigned long long)g_config_revision);
        }

      network_was_busy = network_busy;
      if (link_expected && !network_busy && now >= link_check_at)
        {
          struct bk7258_wifi_result_s link = {0};
          link_check_at = now + 1000;
          if (bk7258_wifi_read_link(&link) == 0 &&
              link.link_state != BK7258_WIFI_LINK_CONNECTED)
            {
              link_expected = false;
              g_configured = false;
              g_service_result = -ENETDOWN;
              if (atomic_load(&g_voice_initialized)) voice_channel_cancel();
              network_retry_at = now + 1000;
              syslog(LOG_WARNING, "BKVOICE link lost; reconnect scheduled\n");
            }
        }
      if (network_retry_at && now >= network_retry_at)
        {
          network_retry_at = 0;
          pending = true;
        }
      if (pending && !bkagent_ota_busy() &&
          (!atomic_load(&g_voice_initialized) || voice_channel_is_idle()) &&
          !network_busy && !bkprov_owner_pairing())
        {
          int ret = bk7258_agent_activate_cloud(&storage_waiting);
          if (storage_waiting && now < storage_deadline)
            {
              storage_retry_at = now + 250;
            }
          else if (!storage_waiting)
            {
              storage_retry_at = 0;
            }

          pending = ret == -EBUSY;
          if (ret && !pending)
            {
              g_product_error = ret;
              syslog(LOG_WARNING,
                     "BKVOICE configuration unavailable result=%d\n", ret);
            }

          network_was_busy = bkprov_network_busy();
        }

      (void)bkprov_owner_step(bkvoice_config_now_ms(NULL), 0, false, false,
        !bkagent_ota_busy());
      if (!atomic_load(&g_agent_core_ready) ||
          !atomic_load(&g_voice_initialized) || bkagent_ota_busy())
        {
          continue;
        }

      bool model_was_pending = bk7258_agent_trigger_model_pending();
      int model_result = bk7258_agent_trigger_model_step(g_configured);
      if (model_result < 0 && model_result != -EBUSY)
        {
          g_product_error = model_result;
        }

      if (model_was_pending && !bk7258_agent_trigger_model_pending() &&
          model_result == 0)
        {
          atomic_store(&g_trigger_prepare_pending, false);
        }

      /* Only after the existing storage worker has read the same-volume
       * identity can a missing model record be told apart from a mount that
       * is not ready. The built-in model is not selected early during the
       * start-up -EAGAIN window, which would cache a wrong revision 0.
       */

      if (atomic_load(&g_trigger_prepare_pending) && g_identity_bound &&
          !pending && !network_busy &&
          voice_channel_is_idle() && !bkprov_owner_busy() &&
          !bk7258_agent_trigger_model_pending())
        {
      int ret = bk7258_agent_trigger_prepare();
      if (ret != -EBUSY)
        {
          atomic_store(&g_trigger_prepare_pending, false);
        }

      if (ret < 0 && ret != -EBUSY)
        {
          g_product_error = ret;
        }

      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKVOICE wake model prepared=%d result=%d\n", ret == 0, ret);
    }

      if (!g_configured || pending || bkprov_network_busy())
        {
          continue;
        }

      /* Display start-up briefly occupies the same SD; only the busy
       * resource is retried, and it is not mistaken for lost
       * configuration.
       */

      if ((!g_trigger_started || preferences_pending) &&
          !voice_interaction_active && voice_channel_is_idle() &&
          now >= preferences_retry_at)
        {
          preferences_pending = false;
#ifdef CONFIG_BK7258_PREFERENCES
          unsigned int threshold;
          int threshold_ret =
            bk7258_preferences_wake_threshold_get(&threshold);
          if (!threshold_ret)
            {
              bk7258_agent_trigger_threshold_set(threshold);
            }

          preferences_pending = threshold_ret == -EBUSY;
          syslog(threshold_ret ? LOG_WARNING : LOG_INFO,
                 "BKVOICE wake threshold restore=%d percent=%u\n",
                 threshold_ret, bk7258_agent_trigger_threshold_get());
#endif
          /* Does not block voice start-up; the same configuration task
           * completes the restore once the session is idle.
           */

          int persona_ret = product_apply_persona(-1);
          preferences_pending |= persona_ret == -EBUSY;
          if (persona_ret)
            {
              syslog(LOG_WARNING, "BKVOICE persona restore failed=%d\n",
                     persona_ret);
            }

#ifdef CONFIG_BK7258_PREFERENCES
          if (!persona_ret)
            {
              (void)bkagent_memory_restore(atomic_load(&g_active_persona));
            }

          bool thinking;
          int thinking_ret = bk7258_preferences_thinking_get(&thinking);
          if (!thinking_ret)
            {
              bkagent_cloud_set_thinking(thinking);
            }

          preferences_pending |= thinking_ret == -EBUSY;
          syslog(thinking_ret ? LOG_WARNING : LOG_INFO,
                 "BKVOICE response mode restore result=%d thinking=%d\n",
                 thinking_ret, thinking_ret ? -1 : (int)thinking);
#endif
          preferences_retry_at = now + 1000;
        }

      if (!g_trigger_started && now >= trigger_retry_at)
        {
          unsigned int saved;
          unsigned int observed;
          int ret = bkvoice_volume_store_get(&saved);
          if (!ret)
            {
              ret = bkvoice_media_volume(true, saved, &observed);
            }

          if (ret == -ENOENT)
            {
              ret = 0;
            }

          if (!ret)
            {
              ret = bk7258_agent_trigger_start();
            }

          g_trigger_started = ret == 0;
          g_product_error = ret;
          syslog(ret ? LOG_WARNING : LOG_INFO,
                 "BKVOICE wake ready=%d result=%d\n",
                 g_trigger_started, ret);
          /* A failed model/media load is unavailable until a configuration,
           * model or readiness event arrives; do not reopen it at 50 Hz.
           */

          if (ret == -EBUSY)
            {
              trigger_retry_at = now + 1000;
            }
          else if (ret)
            {
              g_configured = false;
            }
        }

      if (g_trigger_started && (events & 4))
        {
          int ret = bk7258_agent_trigger_process();
          if (ret < 0)
            {
              voice_interaction_active = false;
              g_product_error = ret;
              voice_action = VOICE_ACTION_REARM;
              voice_action_at = now + 100;
            }
          else
            {
              voice_interaction_active = true;
            }
        }
    }
}
#endif

volatile int g_bk7258_agent_pid = -1;
volatile int g_bk7258_agent_launch_pid = -1;
volatile int g_bk7258_agent_launch_errno;
volatile uint32_t g_bk7258_agent_launch_stage;

/* Runtime skills are plain UTF-8 markdown files under /data/agent/skills/.
 * /data is tmpfs in this product, so the built-in skill must be installed on
 * every boot; it only documents tools this product actually registers and
 * contains no credentials or user data. Users may add their own <name>.md
 * files at runtime and the official loader picks them up.
 */

static const char g_product_skill_device_assistant[] =
  "# Device Assistant\n"
  "\n"
  "Answer questions about this device and apply the simple device changes "
  "the user asks for.\n"
  "\n"
  "## When to use\n"
  "When the user asks about battery, volume, mood/persona or what the "
  "device is doing, or asks to change volume, mood, eyes or "
  "vibration.\n"
  "\n"
  "## How to use\n"
  "1. Volume / mood / battery: call device_status. Battery percentage is "
  "unavailable; never estimate it from voltage.\n"
  "2. Motion: call device_motion for fresh three-axis acceleration. It "
  "is an accelerometer, not a gyroscope.\n"
  "3. Changes: call device_control with action volume (0-100), mood "
  "(gentle/playful/quiet/serious/tsundere_lite), vibrate (1-100 ms) or "
  "eyes (neutral/happy/shy/sad/surprised/thinking/listening/speaking/"
  "sleepy). Every repeat request needs a fresh tool call; report errors "
  "honestly.\n"
  "\n"
  "## Example\n"
  "User asks about the battery in Chinese -> device_status -> answer with "
  "voltage, charging state, volume and mood; state that the percentage is "
  "unavailable.\n";

static void install_product_skills(void)
{
  struct product_skill_s
    {
      const char *name;
      const char *body;
    };

  static const struct product_skill_s skills[] =
    {
      "device-assistant.md", g_product_skill_device_assistant
    };

  for (unsigned int i = 0; i < sizeof(skills) / sizeof(skills[0]); i++)
    {
      char path[64];
      FILE *f;

      snprintf(path, sizeof(path), "/data/agent/skills/%s", skills[i].name);
      f = fopen(path, "w");
      if (!f)
        {
          syslog(LOG_WARNING, "bk7258: runtime skill %s not installed: %d\n",
                 path, errno);
          continue;
        }

      fputs(skills[i].body, f);
      fclose(f);
      syslog(LOG_INFO, "bk7258: runtime skill installed: %s\n", path);
    }
}

/* The fixed official package has no core-only bootstrap entry: its sole
 * main also starts CLI, WebSocket, cron, heartbeat and a competing network
 * owner. This product entry only initializes the retained official core
 * and maps its outbound voice channel to the official voice output. It
 * does not implement a second Agent loop, session store, ASR/TTS pipeline
 * or recovery scheduler.
 */

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

  for (unsigned int i = 0;
       i < sizeof(directories) / sizeof(directories[0]); i++)
    {
      if (mkdir(directories[i], 0755) < 0 && errno != EEXIST)
        {
          syslog(LOG_ERR, "bk7258: Agent directory %s failed: %d\n",
                 directories[i], errno);
          return ERROR;
        }
    }

  install_product_skills();

  ret = config_store_init();
  if (!ret)
    {
      ret = message_bus_init();
    }

  if (!ret)
    {
      ret = memory_store_init();
    }

  if (!ret)
    {
      ret = session_mgr_init();
    }

  if (!ret)
    {
      ret = http_proxy_init();
    }

  if (!ret)
    {
      ret = llm_proxy_init();
    }

  if (!ret)
    {
      ret = llm_router_init();
    }

  if (!ret)
    {
      ret = tool_guard_init();
    }

#ifdef CONFIG_BK7258_VOICE_TLS
  if (!ret) tool_registry_register_provider_checked("bk7258-device",
    product_tools, product_tool_execute);
#endif
#ifdef CONFIG_BK7258_VISION_SERVICE
  if (!ret)
    {
      bk7258_agent_vision_register();
    }

#endif
  if (!ret)
    {
      ret = agent_loop_init();
    }

  if (!ret)
    {
      ret = voice_channel_init();
    }

  if (!ret)
    {
      ret = agent_loop_start();
    }

  if (ret)
    {
      syslog(LOG_ERR, "bk7258: official Agent core init failed: %d\n", ret);
#ifdef CONFIG_BK7258_VOICE_TLS
      voice_channel_service_ready(ret);
      g_product_error = ret;
#endif
      return ERROR;
    }

#ifdef CONFIG_BK7258_VOICE_TLS
  voice_channel_service_ready(0);
  atomic_store(&g_agent_core_ready, true);
  sem_post(&g_product_wake);
#endif
  syslog(LOG_INFO, "bk7258: official Agent core ready\n");

  while (!agent_shutdown_requested())
    {
      agent_msg_t message;
      ret = message_bus_pop_outbound(&message, 1000);
      if (ret != OK)
        {
          continue;
        }

      if (!strcmp(message.channel, AGENT_CHAN_VOICE) && message.content)
        {
          ret = message.request_complete ?
                voice_channel_speak_reply(message.request_id,
                                          message.content) :
                voice_channel_speak(message.content);
#if defined(CONFIG_BK7258_VOICE_TLS) && defined(CONFIG_BK7258_PREFERENCES)
          if (!ret && message.request_complete)
            (void)bkagent_memory_commit(atomic_load(&g_active_persona),
                                        message.content);
#endif
          if (message.request_complete)
            message.request_complete(message.request_id, ret);
#ifdef CONFIG_BK7258_VOICE_TLS
          if (ret)
            {
              g_product_error = ret;
              syslog(LOG_WARNING,
                     "bk7258: official voice output failed: %d\n", ret);
            }
#endif
        }
      else
        {
          ret = -ENOTSUP;
          if (message.request_complete)
            message.request_complete(message.request_id, ret);
          syslog(LOG_WARNING,
                 "bk7258: unsupported Agent output channel=%s\n",
                 message.channel);
        }

      message_bus_msg_free(&message);
    }

  message_bus_wakeup();
  return OK;
}

#ifdef CONFIG_AI_AGENT_LVGL_UI
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
  if (ret != 0)
    {
      return ret;
    }

  ret = audio_capture_set_route(product_capture_route);
  if (ret != 0)
    {
      return ret;
    }

  if (sem_init(&g_product_wake, 0, 0) < 0)
    {
      return -errno;
    }

#ifdef CONFIG_BK7258_PRODUCT_KEYS
  ret = bkvoice_keys_listen(product_keys_notify);
  if (ret != 0)
    {
      return ret;
    }

#endif
  ret = voice_channel_set_event_callback(bk7258_agent_voice_event);
  if (ret != 0)
    {
      return ret;
    }

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
   * files or uploaded by this candidate. Its migration remains explicit.
   */

  if (mount(NULL, "/data", "tmpfs", 0, NULL) < 0)
    {
      syslog(LOG_ERR, "bk7258: Agent volatile storage unavailable: %d\n",
             errno);
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
      /* Keep local management and power control available on Agent failure. */
    }

#ifdef CONFIG_BK7258_VOICE_TLS
  /* The official Session read uses an 8 KiB line buffer, so extra call-stack
   * headroom is kept.
   */

  configpid = task_create("agent-config", 95, 16384,
                          bk7258_agent_config_task, NULL);
  if (configpid < 0)
    syslog(LOG_ERR, "bk7258: official config activation task failed: %d\n",
           (int)configpid);
#endif

  return OK;
}

#endif /* CONFIG_BK7258_APP_AGENT */
