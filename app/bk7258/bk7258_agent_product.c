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

#include <nuttx/signal.h>

#include <arch/board/board.h>
#if defined(CONFIG_BK7258_AUD) && !defined(CONFIG_MEDIA)
extern void bk7258_agent_media_player_link(void);
#endif

#if defined(CONFIG_BK7258_MIC) && !defined(CONFIG_MEDIA)
extern void bk7258_agent_media_recorder_link(void);
#endif

extern int ai_agent_main(int argc, FAR char *argv[]);

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

  return OK;
}

#endif /* CONFIG_BK7258_APP_AGENT */
