/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board bringup for the Beken BK7258 (T5-AI) NuttX port.
 *
 * board_app_initialize() is the NSH application-init hook: when
 * CONFIG_NSH_ARCHINIT=y, nsh_initialize() issues boardctl(BOARDIOC_INIT)
 * and the NSH init task reaches this function during nx_start().  procfs is
 * mounted at /proc here so ps, ls /proc, and cat of /proc entries work;
 * when CONFIG_BK7258_FLASH_MTD + CONFIG_BK7258_FLASH_LITTLEFS are enabled,
 * /dev/mtdblock0 + a LittleFS /data mount are added on the data partition.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>

#include <arch/board/board.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <debug.h>
#include <nuttx/board.h>

#ifdef CONFIG_BK7258_PSRAM
#include <arch/chip/bk7258_psram.h>
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

#ifdef CONFIG_BK7258_OTA_STAGING
#include <arch/chip/bk7258_ota_staging.h>
#include <arch/chip/bk7258_ota_n17_publish.h>
#endif

#ifdef CONFIG_BK7258_OTA_TRIAL
#include <arch/chip/bk7258_ota_trial.h>
#endif

#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
#include <arch/chip/bk7258_ota_fault.h>
#endif

#ifdef CONFIG_BK7258_WDT
#include "bk7258_wdt.h"
#endif

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
#include "bk7258_dvfs.h"
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

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
 * Name: board_app_initialize
 *
 * Description:
 *   Standard NuttX board Application-level initialization hook, reached via
 *   CONFIG_NSH_ARCHINIT from the NSH init task.  Mounts procfs at
 *   CONFIG_NSH_PROC_MOUNTPOINT (default /proc) and returns OK.
 *
 * Input Parameters:
 *   arg - Board-specific argument (unused).
 *
 * Returned Value:
 *   Zero (OK) on success.
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
#if defined(CONFIG_BK7258_AP_CONTROL) || \
    (defined(CONFIG_BK7258_WIFI_VNET) && !defined(CONFIG_BK7258_AP_CORE))
  int apret = OK;
#endif

#if defined(CONFIG_BK7258_WIFI_VNET) && !defined(CONFIG_BK7258_AP_CORE)
  /* CP owns RF/PHY/MAC and must publish the official Wi-Fi controller
   * mailbox endpoints before AP starts its vnet proxy.
   */

  apret = bk7258_wifi_controller_initialize();
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

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
  (void)bk7258_gpio_lowerhalf_initialize();
#endif

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
#ifdef CONFIG_BK7258_OTA_STAGING
      /* Build the two private NuttX MTD image-pair children before exposing
       * the staging API.  They remain internal (no /dev node); this only
       * checks the board wrapper's geometry/allocation path and performs no
       * Flash mutation.
       */

      if (bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_A) == NULL ||
          bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_B) == NULL)
        {
          _err("bk7258: N17 OTA MTD partition init failed\n");
        }
#ifdef CONFIG_MCUBOOT_BOOTLOADER
      /* The upstream NuttX MCUboot flash-map backend opens named MTD
       * devices.  Publish the two already bounds-checked pair partitions
       * only in the BL2 profile; normal firmware retains its private OTA
       * interfaces and does not expose executable flash as /dev nodes.
       */
      else if (register_mtddriver(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                  bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_A),
                                  0600, NULL) < 0 ||
               register_mtddriver(CONFIG_MCUBOOT_SECONDARY_SLOT_PATH,
                                  bk7258_ota_mtd_get(BK7258_OTA_MTD_SLOT_B),
                                  0600, NULL) < 0)
        {
          _err("bk7258: MCUboot MTD node registration failed\n");
        }
#endif
      else if (bk7258_ota_staging_initialize() < 0)
        {
          _err("bk7258: N15-B staging init failed\n");
        }
      else if (bk7258_ota_n17_mtd_get(
                 BK7258_OTA_N17_MTD_JOURNAL_PRIMARY) == NULL ||
               bk7258_ota_n17_mtd_get(
                 BK7258_OTA_N17_MTD_JOURNAL_MIRROR) == NULL ||
               bk7258_ota_n17_mtd_get(
                 BK7258_OTA_N17_MTD_MANIFEST_A) == NULL ||
               bk7258_ota_n17_mtd_get(
                 BK7258_OTA_N17_MTD_MANIFEST_B) == NULL ||
               bk7258_ota_n17_publish_initialize() < 0)
        {
          _err("bk7258: N17 format-3 metadata init failed\n");
        }
#endif
#ifdef CONFIG_BK7258_OTA_FAULT_INJECTION
      if (bk7258_ota_fault_initialize() < 0)
        {
          _err("bk7258: N15-V fault injection init failed\n");
        }
#endif
#ifdef CONFIG_BK7258_OTA_TRIAL
      if (bk7258_ota_trial_initialize() < 0)
        {
          _err("bk7258: N15-D trial init failed\n");
        }
#endif
    }
#endif

  return 0;
}
