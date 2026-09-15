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
#include <stdint.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <mbedtls/platform_util.h>

#include <nuttx/signal.h>

#include <arch/board/board.h>
#ifdef CONFIG_BK7258_VOICE_TLS
#include "bk7258_agent_cloud.h"
#include "bk7258_cloud_config.h"
#include "bk7258_provision_identity.h"
#include "bk7258_provision_claim.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_storage.h"
#include "bk7258_voice_config.h"
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#endif
#if defined(CONFIG_BK7258_AUD) && !defined(CONFIG_MEDIA)
extern void bk7258_agent_media_player_link(void);
#endif

#if defined(CONFIG_BK7258_MIC) && !defined(CONFIG_MEDIA)
extern void bk7258_agent_media_recorder_link(void);
#endif

extern int ai_agent_main(int argc, FAR char *argv[]);

#ifdef CONFIG_BK7258_VOICE_TLS
static int bk7258_agent_activate_cloud(void)
{
  uint8_t bundle[BKPROV_BUNDLE_MAX];
  uint8_t identity_record[8192];
  uint8_t trust[BKVOICE_CONFIG_MAX_BYTES];
  struct bkprov_identity_s identity;
  struct bkprov_settings_s settings;
  struct bkcloud_config_s cloud;
  size_t bundle_size = 0;
  size_t identity_size = 0;
  size_t trust_size = 0;
  uint64_t revision = 0;
  uint8_t transaction[16];
  int ret;

  memset(&settings, 0, sizeof(settings));

  ret = bkprov_storage_snapshot(bundle, sizeof(bundle), &bundle_size,
                                &revision, transaction);
  if (ret < 0) return ret;
  ret = bkprov_storage_identity(identity_record, sizeof(identity_record),
                                &identity_size);
  if (ret < 0) return ret;
  memset(&identity, 0, sizeof(identity));
  ret = bkprov_identity_load(&identity, identity_record, identity_size);
  if (ret < 0) goto out;
  ret = bkprov_settings_decode(&settings, bundle, bundle_size);
  if (ret < 0 || settings.cloud == NULL || settings.cloud_size == 0)
    {
      if (ret == 0) ret = -ENOENT;
      goto clear_identity;
    }
  ret = bkprov_settings_voice(&settings, identity_record + 48,
                              identity.certificate_size,
                              identity_record + 48 + identity.certificate_size,
                              identity.key_size, trust, sizeof(trust),
                              &trust_size);
  if (ret == 0)
    ret = bkagent_cloud_configure(trust, trust_size, settings.cloud,
                                  settings.cloud_size);
  if (ret == 0)
    ret = bkcloud_config_decode(&cloud, settings.cloud, settings.cloud_size);
  if (ret == 0)
    {
      const char *backend = cloud.dialect == 2 ? "mimo" : "openai-audio";
      ret = voice_asr_set_backend(backend);
      if (ret == 0) ret = voice_tts_set_backend(backend);
      if (ret == 0)
        syslog(LOG_INFO, "BKVOICE official backends active=%s config_revision=%llu\n",
               backend, (unsigned long long)revision);
      bkcloud_config_clear(&cloud);
    }

clear_identity:
  bkprov_identity_clear(&identity);
out:
  mbedtls_platform_zeroize(bundle, sizeof(bundle));
  mbedtls_platform_zeroize(identity_record, sizeof(identity_record));
  mbedtls_platform_zeroize(trust, sizeof(trust));
  mbedtls_platform_zeroize(&settings, sizeof(settings));
  return ret;
}

static int bk7258_agent_config_task(int argc, FAR char *argv[])
{
  unsigned int attempt;
  int ret = -EAGAIN;
  (void)argc;
  (void)argv;
  for (attempt = 0; attempt < 150 && ret == -EAGAIN; attempt++)
    {
      ret = bk7258_agent_activate_cloud();
      if (ret == -EAGAIN) nxsig_usleep(200000);
    }
  if (ret < 0)
    syslog(LOG_WARNING, "BKVOICE official config activation unavailable: %d\n", ret);
  return ret;
}
#endif

extern int bk7258_agent_trigger_start(void);

static int bk7258_agent_trigger_task(int argc, FAR char *argv[])
{
  (void)argc;
  (void)argv;
  nxsig_usleep(2000000u);
  return bk7258_agent_trigger_start();
}

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

  if (mkdir("/data", 0755) < 0 && errno != EEXIST)
    {
      syslog(LOG_ERR, "bk7258: Agent data mountpoint failed: %d\n", errno);
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

  if (task_create("agent-trigger", 90, 8192,
                  bk7258_agent_trigger_task, NULL) < 0)
    syslog(LOG_WARNING, "bk7258: official Trigger task unavailable\n");

  return OK;
}

#endif /* CONFIG_BK7258_APP_AGENT */
