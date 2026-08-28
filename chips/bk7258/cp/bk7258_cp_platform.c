/****************************************************************************
 * chips/bk7258/cp/bk7258_cp_platform.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned BK7258 SoC platform stages.  Cross-layer sequencing is performed
 * by the board lifecycle; this file contains no board callbacks or policy.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <arch/chip/bk7258_cp_platform.h>

#if defined(CONFIG_BK7258_AP_CONTROL) || \
    defined(CONFIG_BK7258_AP_SUPERVISOR)
#  include <arch/chip/bk7258_amp.h>
#endif

#ifdef CONFIG_BK7258_AP_CONTROL
#  include <arch/chip/bk7258_image_layout.h>
#endif

#ifdef CONFIG_BK7258_BT_IPC
#  include <arch/chip/bk7258_bt_ipc.h>
#endif

#if defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
#  include <arch/chip/bk7258_storage_config.h>
#  include "bk7258_radio_storage.h"
#endif

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
#  include <arch/chip/bk7258_gpio.h>
#endif

#ifdef CONFIG_BK7258_IRDA
#  include <arch/chip/bk7258_irda.h>
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
#  include <arch/chip/bk7258_pm.h>
#endif

#ifdef CONFIG_BK7258_PSRAM
#  include <arch/chip/bk7258_psram.h>
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

#ifdef CONFIG_BK7258_TEMPERATURE
#  include <arch/chip/bk7258_temperature.h>
#endif

#ifdef CONFIG_BK7258_WIFI_VNET
#  include <arch/chip/bk7258_wifi.h>
#endif

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
#  include "bk7258_pm_coord.h"
#endif

#ifdef CONFIG_BK7258_WDT
#  include "bk7258_wdt.h"
#endif

#if defined(CONFIG_BK7258_OTA) || \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC) || \
    defined(CONFIG_BK7258_FLASH_MTD) || \
    defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
#  include "bk7258_storage_configure.h"
#endif

#if defined(CONFIG_BK7258_OTA) || \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC) || \
    defined(CONFIG_BK7258_FLASH_MTD) || \
    defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
#  include "bk7258_storage_internal.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_cp_platform_run_stage(
  enum bk7258_cp_platform_stage_e stage,
  FAR const struct bk7258_storage_config_s *storage,
  FAR const struct bk7258_gpio_config_s *gpio)
{
  int ret = OK;

  (void)storage;
  (void)gpio;

  switch (stage)
    {
#ifdef CONFIG_BK7258_SWD_DEBUG
      case BK7258_CP_STAGE_TRACE_ENTRY:
        bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_ENTRY);
        break;
#endif

      case BK7258_CP_STAGE_STORAGE_CONFIG:
#if defined(CONFIG_BK7258_OTA) || \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC) || \
    defined(CONFIG_BK7258_FLASH_MTD) || \
    defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
        if (storage == NULL)
          {
            return -ENOSYS;
          }

        ret = bk7258_storage_configure(storage);
        if (ret < 0)
          {
            return ret;
          }
#endif
#ifdef CONFIG_BK7258_GPIO_LOWERHALF
        if (gpio == NULL)
          {
            return -ENOSYS;
          }
#endif
        break;

#ifdef CONFIG_BK7258_OTA
      case BK7258_CP_STAGE_OTA_LAYOUT:
        {
          FAR const struct bk7258_ota_layout_s *layout;

          ret = bk7258_storage_ota_layout(&layout);
        }
        break;
#endif

#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC
      case BK7258_CP_STAGE_RESET_MARKER_POLICY:
        {
          uint32_t address;

          ret = bk7258_storage_marker_address(&address);
        }
        break;
#endif

#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
      case BK7258_CP_STAGE_SDK_RUNTIME:
        ret = bk7258_sdk_runtime_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
      case BK7258_CP_STAGE_DEBUG_ROUTE_AFTER_SDK:
        bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_AFTER_SDK);
        ret = bk7258_swd_initialize();
        bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_AFTER_SWD);
        break;
#endif

#ifdef CONFIG_BK7258_SARADC_SERVER
      case BK7258_CP_STAGE_SARADC_SERVER:
        ret = bk7258_saradc_server_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
      case BK7258_CP_STAGE_PM_SERVER:
        ret = bk7258_pm_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_TEMPERATURE
      case BK7258_CP_STAGE_TEMPERATURE_SERVER:
        ret = bk7258_temperature_initialize();
        break;
#endif

#if defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
      case BK7258_CP_STAGE_RADIO_STORAGE:
        {
          FAR const struct bk7258_radio_storage_config_s *radio;

          ret = bk7258_storage_radio_config(&radio);
          if (ret < 0)
            {
              return ret;
            }

          ret = bk7258_radio_storage_initialize(radio);
        }
        break;
#endif

#ifdef CONFIG_BK7258_WIFI_VNET
      case BK7258_CP_STAGE_WIFI_CONTROLLER:
        ret = bk7258_wifi_controller_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_BT_IPC
      case BK7258_CP_STAGE_BT_CONTROLLER:
        ret = bk7258_bt_controller_ipc_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_AP_CONTROL
      case BK7258_CP_STAGE_AP_CONTROL:
        {
          static const struct bk7258_ap_image_desc_s ap_image =
          {
            .slot_start = BK7258_AP_FLASH_ADDR,
            .slot_end = BK7258_AP_FLASH_ADDR + BK7258_AP_FLASH_SIZE,
            .vector_addr = BK7258_AP_VECTOR_ADDR,
          };

          ret = bk7258_ap_control_initialize(&ap_image);
        }
        break;

#  ifdef CONFIG_BK7258_GPIO_LOWERHALF
      case BK7258_CP_STAGE_GPIO_SERVER:
        ret = bk7258_gpio_lowerhalf_initialize(gpio);
        break;
#  endif
#endif

#ifdef CONFIG_BK7258_PSRAM
      case BK7258_CP_STAGE_PSRAM:
        {
          struct bk7258_psram_info_s psram;

          memset(&psram, 0, sizeof(psram));
          ret = bk7258_psram_initialize();

#  ifdef CONFIG_BK7258_PSRAM_SYSTEM_HEAP
          if (ret >= 0)
            {
              int heapret = bk7258_psram_add_system_heap(
                CONFIG_BK7258_PSRAM_SYSTEM_HEAP_SIZE);

              if (heapret < 0)
                {
                  syslog(LOG_ERR,
                         "BPSR SYSTEM HEAP FAIL status=%d size=%lu\n",
                         heapret,
                         (unsigned long)
                           CONFIG_BK7258_PSRAM_SYSTEM_HEAP_SIZE);
                  ret = heapret;
                }
              else
                {
                  syslog(LOG_INFO, "BPSR SYSTEM HEAP PASS size=%lu\n",
                         (unsigned long)
                           CONFIG_BK7258_PSRAM_SYSTEM_HEAP_SIZE);
                }
            }
#  endif

          (void)bk7258_psram_get_info(&psram);
          if (ret < 0)
            {
              syslog(LOG_ERR,
                     "BPSR BOOT FAIL status=%d id=%04lx config=%04lx "
                     "fail=%08lx expected=%08lx actual=%08lx\n",
                     ret, (unsigned long)psram.chip_id,
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
        }
        break;
#endif

#ifdef CONFIG_BK7258_AP_SUPERVISOR
      case BK7258_CP_STAGE_AP_SUPERVISOR:
        ret = bk7258_ap_supervisor_initialize();
        break;
#endif

#if defined(CONFIG_BK7258_AP_CONTROL) && \
    defined(CONFIG_BK7258_AP_AUTOSTART)
      case BK7258_CP_STAGE_AP_START:
        ret = bk7258_ap_start(CONFIG_BK7258_AP_AUTOSTART_TIMEOUT_MS);
        break;
#endif

#ifdef CONFIG_BK7258_WIFI_VNET
      case BK7258_CP_STAGE_WIFI_CONTROL:
        ret = bk7258_wifi_control_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_WDT
      case BK7258_CP_STAGE_WDT:
#  ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
        ret = bk7258_wdt_fault_validate();
#  else
        ret = bk7258_wdt_initialize();
#  endif
        break;
#endif

#ifdef CONFIG_BK7258_IRDA
      case BK7258_CP_STAGE_IRDA:
        ret = bk7258_irda_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
      case BK7258_CP_STAGE_PM_COORD:
        ret = bk7258_pm_coord_initialize();
        break;
#endif

#if defined(CONFIG_BK7258_GPIO_LOWERHALF) && \
    !defined(CONFIG_BK7258_AP_CONTROL)
      case BK7258_CP_STAGE_GPIO_FALLBACK:
        ret = bk7258_gpio_lowerhalf_initialize(gpio);
        break;
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
      case BK7258_CP_STAGE_DEBUG_ROUTE_FINAL:
        ret = bk7258_swd_initialize();
        bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_BOARD_LATE_EXIT);
        break;
#endif

      default:
        ret = -ENOTSUP;
        break;
    }

  return ret;
}
