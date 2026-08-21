/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_platform.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mandatory CP/AP platform-service initialization for the Beken BK7258.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <debug.h>
#include <nuttx/mutex.h>

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
#  include <arch/chip/bk7258_gpio.h>
#endif

#ifdef CONFIG_BK7258_SARADC_SERVER
#  include <arch/chip/bk7258_saradc_server.h>
#endif

#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
#  include <arch/chip/bk7258_sdk_runtime.h>
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
#  include <arch/chip/bk7258_debug.h>
#endif

#ifdef CONFIG_BK7258_PSRAM
#  include <arch/chip/bk7258_psram.h>
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
#  include <arch/chip/bk7258_pm.h>
#endif

#ifdef CONFIG_BK7258_TEMPERATURE
#  include <arch/chip/bk7258_temperature.h>
#endif

#ifdef CONFIG_BK7258_AP_CONTROL
#  include <arch/chip/bk7258_image_layout.h>
#  include <arch/chip/bk7258_amp.h>
#endif

#ifdef CONFIG_BK7258_BT_IPC
#  include <arch/chip/bk7258_bt_ipc.h>
#endif

#ifdef CONFIG_BK7258_WIFI_VNET
#  include <arch/chip/bk7258_wifi.h>
#endif

#ifdef CONFIG_BK7258_WDT
#  include "bk7258_wdt.h"
#endif

#ifdef CONFIG_BK7258_IRDA
#  include <arch/chip/bk7258_irda.h>
#endif

#if defined(CONFIG_BK7258_PM_COORDINATED_STANDBY) && \
    !defined(CONFIG_BK7258_AP_CORE)
#  include "bk7258_pm_coord.h"
#endif

#ifdef CONFIG_BK7258_TOUCH
#  include <arch/chip/bk7258_touch.h>
#  include <nuttx/input/buttons.h>
#endif

#include "bk7258_internal.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_platform_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_platform_initialized;
static int g_bk7258_platform_result;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_platform_initialize
 *
 * Description:
 *   Initialize mandatory CP/AP platform services once.  This runs from
 *   board_late_initialize() independently of NSH/BOARDIOC_INIT.
 *
 * Returned Value:
 *   Zero (OK) on success.
 *
 ****************************************************************************/

int bk7258_platform_initialize(void)
{
  int result = OK;
  int lockret;

  lockret = nxmutex_lock(&g_bk7258_platform_lock);
  if (lockret < 0)
    {
      return lockret;
    }

  if (g_bk7258_platform_initialized)
    {
      int saved_result = g_bk7258_platform_result;

      nxmutex_unlock(&g_bk7258_platform_lock);
      return saved_result;
    }

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_ENTRY);
#endif

#if defined(CONFIG_BK7258_AP_CONTROL) || \
    defined(CONFIG_BK7258_SARADC_SERVER) || \
    defined(CONFIG_BK7258_SDK_IPC_RUNTIME) || \
    defined(CONFIG_BK7258_PM_CLOCK) || \
    defined(CONFIG_BK7258_TEMPERATURE) || \
    (defined(CONFIG_BK7258_BT_IPC) && !defined(CONFIG_BK7258_AP_CORE)) || \
    (defined(CONFIG_BK7258_WIFI_VNET) && !defined(CONFIG_BK7258_AP_CORE))
  int apret = OK;
#endif

#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
  apret = bk7258_sdk_runtime_initialize();
#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_AFTER_SDK);
#endif
  if (apret < 0)
    {
      _err("bk7258: SDK IPC runtime init failed: %d\n", apret);
    }
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
  /* Reassert the configured BL1/BL2 SWD route immediately after the SDK
   * runtime is ready.  The pin group and target core come from Kconfig; SDK
   * leaves which can touch the selected SWD pins reassert the same mapping
   * through the wrapper.
   */

  int swdret = bk7258_swd_initialize();

  if (swdret < 0)
    {
      _err("bk7258: SWD pinmux failed: %d\n", swdret);
    }

  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_AFTER_SWD);
#endif

#ifdef CONFIG_BK7258_SARADC_SERVER
  if (apret >= 0)
    {
      apret = bk7258_saradc_server_initialize();
    }

  if (apret < 0)
    {
      _err("bk7258: SARADC server init failed: %d\n", apret);
    }
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
  /* Register the CP clock service before AP is released.  RPMsg transport
   * creation is asynchronous; registering the callback early lets the AP
   * endpoint bind as soon as its RPTUN device appears.
   */

  if (apret >= 0)
    {
      apret = bk7258_pm_initialize();
    }

  if (apret < 0)
    {
      _err("bk7258: PM clock service init failed: %d\n", apret);
    }
#endif

#ifdef CONFIG_BK7258_TEMPERATURE
  /* Register the CP server before AP release and the AP client before its
   * RPTUN device appears.  Callback registration is transport-order safe;
   * actual sampling remains strictly on demand.
   */

  if (apret >= 0)
    {
      apret = bk7258_temperature_initialize();
    }

  if (apret < 0)
    {
      _err("bk7258: temperature service init failed: %d\n", apret);
    }
#endif

#if !defined(CONFIG_BK7258_AP_CORE) && \
    (defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET))
  if (apret >= 0)
    {
      apret = bk7258_mac_storage_initialize();
    }

  if (apret < 0)
    {
      _err("bk7258: MAC storage binding init failed: %d\n", apret);
    }
#endif

#if defined(CONFIG_BK7258_WIFI_VNET) && !defined(CONFIG_BK7258_AP_CORE)

  /* CP owns RF/PHY/MAC and must publish the official Wi-Fi controller
   * mailbox endpoints before AP starts its vnet proxy.
   */

  if (apret >= 0)
    {
      apret = bk7258_wifi_controller_initialize();
    }

  if (apret < 0)
    {
      _err("bk7258: Wi-Fi controller init failed: %d\n", apret);
    }
#endif

#if defined(CONFIG_BK7258_BT_IPC) && !defined(CONFIG_BK7258_AP_CORE)
  /* CP owns the controller side of Bluetooth IPC.  Publish it before AP is
   * released, just like the Wi-Fi controller endpoints above.  Otherwise a
   * cold AP can reach its synchronous HCI open while CP is still creating
   * the peer endpoint; a warm AP restart hides that ordering bug.
   */

  if (apret >= 0)
    {
      apret = bk7258_bt_controller_ipc_initialize();
      if (apret < 0)
        {
          _err("bk7258: Bluetooth controller IPC init failed: %d\n",
               apret);
        }
    }
#endif

#ifdef CONFIG_BK7258_AP_CONTROL
  static const struct bk7258_ap_image_desc_s ap_image =
    {
      .slot_start = BK7258_AP_FLASH_ADDR,
      .slot_end = BK7258_AP_FLASH_ADDR + BK7258_AP_FLASH_SIZE,
      .vector_addr = BK7258_AP_VECTOR_ADDR,
    };


  if (apret >= 0)
    {
      apret = bk7258_ap_control_initialize(&ap_image);
      if (apret < 0)
        {
          _err("bk7258: AP control init failed: %d\n", apret);
        }
    }

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
  /* The AP SDK GPIO driver is a synchronous IPC client of the CP GPIO
   * service.  Publish the service before AP is released, exactly like the
   * Wi-Fi and Bluetooth controller endpoints above; a cold AP otherwise
   * hangs in its first bk_gpio_* call waiting for a peer that CP has not
   * created yet.
   */

  if (apret >= 0)
    {
      apret = bk7258_gpio_lowerhalf_initialize();
      if (apret < 0)
        {
          _err("bk7258: GPIO lower-half init failed: %d\n", apret);
        }
    }
#endif
#endif

#ifdef CONFIG_BK7258_PSRAM
  struct bk7258_psram_info_s psram;
  int psramret;

  /* Match the official CP startup order: finish the PHY/RF calibration and
   * Bluetooth IPC leaf sequence before PSRAM is powered and configured.
   * CP remains the sole PSRAM hardware owner, and AP is still held in reset
   * until this destructive gate and the CP-local heap are both complete.
   *
   * In particular, do not move this back to __start().  A factory image
   * takes the long first-calibration path, whose final analog programming is
   * allowed to precede PSRAM initialization in the immutable SDK.
   */

  psramret = bk7258_psram_initialize();
  (void)bk7258_psram_get_info(&psram);
  if (psramret < 0)
    {
      syslog(LOG_ERR,
             "BPSR BOOT FAIL status=%d id=%04lx config=%04lx fail=%08lx "
             "expected=%08lx actual=%08lx\n",
             psramret, (unsigned long)psram.chip_id,
             (unsigned long)psram.config_value,
             (unsigned long)psram.boot_test_fail_address,
             (unsigned long)psram.boot_test_expected,
             (unsigned long)psram.boot_test_actual);
    }
  else
    {
      syslog(LOG_INFO,
             "BPSR BOOT PASS id=%04lx config=%04lx capacity=%lu "
             "heap=%08lx+%lu raw=%lu/%lu mpu=%lu\n",
             (unsigned long)psram.chip_id,
             (unsigned long)psram.config_value,
             (unsigned long)psram.capacity,
             (unsigned long)psram.heap_base,
             (unsigned long)psram.heap_size,
             (unsigned long)psram.boot_test_passes,
             (unsigned long)psram.boot_test_runs,
             (unsigned long)psram.mpu_valid);
    }
#endif

#ifdef CONFIG_BK7258_AP_CONTROL
#ifdef CONFIG_BK7258_PSRAM
  if (apret >= 0 && psramret < 0)
    {
      apret = psramret;
    }
#endif

#ifdef CONFIG_BK7258_AP_SUPERVISOR
  if (apret >= 0)
    {
      apret = bk7258_ap_supervisor_initialize();
      if (apret < 0)
        {
          _err("bk7258: AP supervisor init failed: %d\n", apret);
        }
    }

#endif
#ifdef CONFIG_BK7258_AP_AUTOSTART
  if (apret >= 0)
    {
      apret = bk7258_ap_start(CONFIG_BK7258_AP_AUTOSTART_TIMEOUT_MS);
      if (apret < 0)
        {
          _err("bk7258: AP autostart failed: %d\n", apret);
        }
    }

#endif

#ifdef CONFIG_BK7258_WIFI_VNET
  if (apret >= 0)
    {
      apret = bk7258_wifi_control_initialize();
      if (apret < 0)
        {
          _err("bk7258: Wi-Fi control init failed: %d\n", apret);
        }
    }
#endif
#endif

#ifdef CONFIG_BK7258_WDT
  /* The CP reset entry already closed the bootloader's AON and APB
   * watchdogs.  Register and arm the NuttX automonitor only after the
   * bounded AP startup has returned.  Advanced AP profiles may legitimately
   * spend longer than the normal eight-second watchdog period in their
   * aggregate SMP gates; arming earlier turns a slow-but-bounded self-test
   * into a reboot loop.
   */

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
  result = bk7258_wdt_fault_validate();
#else
  result = bk7258_wdt_initialize();
#endif
  if (result < 0)
    {
      _err("bk7258: WDT initialization failed: %d\n", result);
    }
#endif

#ifdef CONFIG_BK7258_IRDA
  result = bk7258_irda_initialize();
  if (result < 0)
    {
      _err("bk7258: IRDA initialization failed: %d\n", result);
    }
#endif

#if defined(CONFIG_BK7258_PM_COORDINATED_STANDBY) && \
    !defined(CONFIG_BK7258_AP_CORE)
  /* Arm the guaranteed AON RTC wake only after AP/RPTUN and the NuttX-owned
   * watchdog are live.  Until this succeeds the PM prepare callback rejects
   * every STANDBY attempt.
   */

  int pmret = bk7258_pm_coord_initialize();

  if (pmret < 0)
    {
      _err("bk7258: coordinated standby init failed: %d\n", pmret);
    }
#endif

#if defined(CONFIG_BK7258_GPIO_LOWERHALF) && !defined(CONFIG_BK7258_AP_CONTROL)
  (void)bk7258_gpio_lowerhalf_initialize();
#endif

#ifdef CONFIG_BK7258_TOUCH
  FAR struct btn_lowerhalf_s *touch_lower;
  int touchret;
  const struct bk7258_touch_config_s touch_config =
  {
    .channel_mask = 1u << CONFIG_BK7258_TOUCH_CHANNEL,
    .poll_interval_ms = CONFIG_BK7258_TOUCH_POLL_INTERVAL_MS,
    .sensitivity_level = CONFIG_BK7258_TOUCH_SENSITIVITY,
    .detect_threshold = CONFIG_BK7258_TOUCH_THRESHOLD,
    .detect_range = CONFIG_BK7258_TOUCH_RANGE,
#ifdef CONFIG_BK7258_TOUCH_CALIBRATE
    .calibrate = true,
#else
    .calibrate = false,
#endif
  };

  touchret = bk7258_touch_initialize(&touch_lower, &touch_config);
  if (touchret >= 0)
    {
      touchret = btn_register("/dev/buttons", touch_lower);
    }

  if (touchret < 0)
    {
      (void)bk7258_touch_deinitialize();
      _err("bk7258: touch buttons init failed: %d\n", touchret);
    }
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
  /* AP release initializes its own SDK SYS/GPIO view after the early CP
   * route.  Recommit the selected board-owned route at the final mandatory
   * platform boundary.
   */

  (void)bk7258_swd_initialize();
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_EXIT);
#endif

#if defined(CONFIG_BK7258_AP_CONTROL) || \
    defined(CONFIG_BK7258_SARADC_SERVER) || \
    defined(CONFIG_BK7258_SDK_IPC_RUNTIME) || \
    defined(CONFIG_BK7258_PM_CLOCK) || \
    defined(CONFIG_BK7258_TEMPERATURE) || \
    (defined(CONFIG_BK7258_BT_IPC) && !defined(CONFIG_BK7258_AP_CORE)) || \
    (defined(CONFIG_BK7258_WIFI_VNET) && !defined(CONFIG_BK7258_AP_CORE))
  g_bk7258_platform_result = apret < 0 ? apret : result;
#else
  g_bk7258_platform_result = result;
#endif
  g_bk7258_platform_initialized = true;
  nxmutex_unlock(&g_bk7258_platform_lock);
  return g_bk7258_platform_result;
}
