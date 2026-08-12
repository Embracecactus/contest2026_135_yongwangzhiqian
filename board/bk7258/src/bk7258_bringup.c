/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board bringup for the Beken BK7258 (T5-AI) NuttX port.
 *
 * board_late_initialize() owns mandatory platform services independently of
 * NSH.  board_app_initialize() is the application-facing hook for procfs,
 * MTD device nodes and filesystems.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>

#include <arch/board/board.h>

#ifdef CONFIG_BK7258_SARADC_SERVER
#  include <arch/chip/bk7258_saradc_server.h>
#endif
#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
#  include <arch/chip/bk7258_sdk_runtime.h>
#endif
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <debug.h>
#include <nuttx/board.h>
#include <nuttx/mutex.h>

#ifdef CONFIG_BK7258_SWD_DEBUG
#include <arch/chip/bk7258_debug.h>
#endif

#ifdef CONFIG_BK7258_PSRAM
#include <arch/chip/bk7258_psram.h>
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
#include <arch/chip/bk7258_pm.h>
#endif

#ifdef CONFIG_BK7258_AP_CONTROL
#include <arch/chip/bk7258_amp.h>
#endif

#ifdef CONFIG_BK7258_BT_IPC
#include <arch/chip/bk7258_bt_ipc.h>
#endif

#ifdef CONFIG_BK7258_WIFI_VNET
#include <arch/chip/bk7258_wifi.h>
#endif

#ifdef CONFIG_BK7258_FLASH_MTD
#include <nuttx/fs/fs.h>
#include <nuttx/mtd/mtd.h>
#include "bk7258_flash_mtd.h"
#endif

#ifdef CONFIG_BK7258_FLASH_LITTLEFS
#include <nuttx/fs/fs.h>
#endif

#ifdef CONFIG_BK7258_WDT
#include "bk7258_wdt.h"
#endif

#ifdef CONFIG_BK7258_TOUCH
#include <arch/chip/bk7258_touch.h>
#include <nuttx/input/buttons.h>
#endif

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
#include "bk7258_dvfs.h"
#endif

/****************************************************************************
 * Private Data and Functions
 ****************************************************************************/

static mutex_t g_bk7258_platform_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_platform_initialized;
static int g_bk7258_platform_result;

#ifdef CONFIG_BK7258_FLASH_LITTLEFS
/* LittleFS bring-up: register /dev/mtdblock0 (ftl), mount at /data with the
 * "autoformat" option (formats only on first boot), then run a probe-file
 * persistence check.
 *
 * The probe file is created on the first boot after format and read back on
 * every later boot, so a reboot observing the expected bytes proves write
 * persistence.
 */

#define BK7258_FS_MOUNTPOINT  "/data"
#define BK7258_FS_BLOCKDEV    "/dev/mtdblock0"
#define BK7258_FS_PROBE       "/data/probe.txt"
#define BK7258_FS_PROBE_LEN   12
static const char g_fs_probe[BK7258_FS_PROBE_LEN] = "BK7258LFS-OK";

static void bk7258_fs_probe(struct mtd_dev_s *mtd)
{
  char buf[BK7258_FS_PROBE_LEN];
  int fd;
  ssize_t n;

  if (ftl_initialize(0, mtd) < 0)
    {
      return;
    }

  mkdir(BK7258_FS_MOUNTPOINT, 0777);

  if (mount(BK7258_FS_BLOCKDEV, BK7258_FS_MOUNTPOINT, "littlefs", 0,
            "autoformat") < 0)
    {
      return;
    }

  /* If the probe file exists, read it back and compare (persistence). */

  fd = open(BK7258_FS_PROBE, O_RDONLY);
  if (fd < 0)
    {
      /* First boot: create the probe file. */

      fd = open(BK7258_FS_PROBE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd < 0)
        {
          return;
        }

      if (write(fd, g_fs_probe, BK7258_FS_PROBE_LEN) != BK7258_FS_PROBE_LEN)
        {
          close(fd);
          return;
        }

      close(fd);
      sync();
      return;
    }

  n = read(fd, buf, BK7258_FS_PROBE_LEN);
  close(fd);

  /* Persistence verification: the file was created on a previous boot, so a
   * successful read-back of the expected marker proves writes survive reset.
   * Stay silent when persistence is confirmed; log a runtime error otherwise.
   */

  if (n != (ssize_t)BK7258_FS_PROBE_LEN ||
      memcmp(buf, g_fs_probe, BK7258_FS_PROBE_LEN) != 0)
    {
      _err("bk7258: LittleFS probe persistence check failed (n=%d)\n",
           (int)n);
    }
}
#endif /* CONFIG_BK7258_FLASH_LITTLEFS */

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

static int bk7258_platform_initialize(void)
{
  int lockret;

  lockret = nxmutex_lock(&g_bk7258_platform_lock);
  if (lockret < 0)
    {
      return lockret;
    }

  if (g_bk7258_platform_initialized)
    {
      int result = g_bk7258_platform_result;

      nxmutex_unlock(&g_bk7258_platform_lock);
      return result;
    }

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_ENTRY);
#endif

#if defined(CONFIG_BK7258_AP_CONTROL) || \
    defined(CONFIG_BK7258_SARADC_SERVER) || \
    defined(CONFIG_BK7258_SDK_IPC_RUNTIME) || \
    defined(CONFIG_BK7258_PM_CLOCK) || \
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

  {
    int swdret = bk7258_swd_initialize();

    if (swdret < 0)
      {
        _err("bk7258: SWD pinmux failed: %d\n", swdret);
      }

    bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_AFTER_SWD);
  }
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

#ifdef CONFIG_BK7258_AP_CONTROL
#ifdef CONFIG_BK7258_BT_IPC
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

  if (apret >= 0)
    {
      apret = bk7258_ap_control_initialize();
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
             "BPSR BOOT FAIL status=%d id=%04lx config=%04lx fail=%08lx expected=%08lx actual=%08lx\n",
             psramret, (unsigned long)psram.chip_id,
             (unsigned long)psram.config_value,
             (unsigned long)psram.boot_test_fail_address,
             (unsigned long)psram.boot_test_expected,
             (unsigned long)psram.boot_test_actual);
    }
  else
    {
      syslog(LOG_INFO,
             "BPSR BOOT PASS id=%04lx config=%04lx capacity=%lu heap=%08lx+%lu raw=%lu/%lu mpu=%lu\n",
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
  /* The CP reset entry already closed the bootloader's AON and APB watchdogs.
   * Register and arm the NuttX automonitor only after the bounded AP startup
   * has returned.  Advanced AP profiles may legitimately spend longer than
   * the normal eight-second watchdog period in their aggregate SMP gates;
   * arming earlier turns a slow-but-bounded self-test into a reboot loop.
   */

  (void)bk7258_wdt_initialize();
#endif

#if defined(CONFIG_BK7258_GPIO_LOWERHALF) && !defined(CONFIG_BK7258_AP_CONTROL)
  (void)bk7258_gpio_lowerhalf_initialize();
#endif

#ifdef CONFIG_BK7258_TOUCH
  {
    FAR struct btn_lowerhalf_s *touch_lower;
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
    int touchret;

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
    (defined(CONFIG_BK7258_WIFI_VNET) && !defined(CONFIG_BK7258_AP_CORE))
  g_bk7258_platform_result = apret;
#else
  g_bk7258_platform_result = OK;
#endif
  g_bk7258_platform_initialized = true;
  nxmutex_unlock(&g_bk7258_platform_lock);
  return g_bk7258_platform_result;
}

void board_late_initialize(void)
{
  int ret = bk7258_platform_initialize();

  if (ret < 0)
    {
      _err("bk7258: mandatory platform initialization failed: %d\n", ret);
    }
}

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Register application-facing procfs/MTD/filesystem services.  Mandatory
 *   SDK, IPC, PM and AP lifecycle initialization is owned by
 *   board_late_initialize() and does not depend on NSH.
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  int ret;

  (void)arg;
  ret = bk7258_platform_initialize();
  if (ret < 0)
    {
      return ret;
    }

  /* Register the BK7258 DVFS /proc/dvfs entry *before* mounting procfs: the
   * fs_procfs NOTE requires the procfs entry table to be stable at mount
   * time (procfs_register reallocs the table; doing it after the mount would
   * race with concurrent procfs access).
   */

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
  (void)bk7258_dvfs_procfs_register();
#endif

  /* Mount procfs at the NSH proc mountpoint so ps, ls /proc, and cat of
   * /proc entries work.  CONFIG_NSH_ARCHINIT activates this hook;
   * CONFIG_FS_PROCFS provides the filesystem.
   */

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_NSH_PROC_MOUNTPOINT)
  (void)mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL);
#endif

#ifdef CONFIG_BK7258_FLASH_MTD
  /* Create the MTD instance for the 1 MiB data partition.  When LittleFS is
   * also enabled, register /dev/mtdblock0 + mount /data and run the probe
   * persistence check on the same instance.
   */

  FAR struct mtd_dev_s *mtd = bk7258_flash_mtd_initialize();
  if (mtd != NULL)
    {
#ifdef CONFIG_BK7258_FLASH_LITTLEFS
      bk7258_fs_probe(mtd);
#endif
#ifdef CONFIG_MCUBOOT_BOOTLOADER
      /* Publish read-only, bounds-checked image-pair partitions only to the
       * NuttX MCUboot BL2 profile. */
      if (register_mtddriver(
            CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
            bk7258_mcuboot_mtd_get(BK7258_MCUBOOT_MTD_SLOT_PRIMARY),
            0600, NULL) < 0 ||
          register_mtddriver(
            CONFIG_MCUBOOT_SECONDARY_SLOT_PATH,
            bk7258_mcuboot_mtd_get(BK7258_MCUBOOT_MTD_SLOT_SECONDARY),
            0600, NULL) < 0)
        {
          _err("bk7258: MCUboot MTD node registration failed\n");
        }
#endif
    }
#endif

  return 0;
}
