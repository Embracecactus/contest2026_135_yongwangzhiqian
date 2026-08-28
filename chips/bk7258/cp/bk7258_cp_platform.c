/****************************************************************************
 * chips/bk7258/cp/bk7258_cp_platform.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned BK7258 platform sequence.  Board-owned policy is reported through
 * explicit checkpoints; this file contains no board callbacks or symbols.
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
#include <arch/chip/bk7258_stage_runner.h>

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
 * Private Types
 ****************************************************************************/

struct bk7258_cp_platform_context_s
{
  FAR const struct bk7258_storage_config_s *storage;
  FAR const struct bk7258_gpio_config_s *gpio;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_cp_stage_execute(
  FAR void *arg, FAR const struct bk7258_stage_desc_s *stage_desc)
{
  FAR const struct bk7258_cp_platform_context_s *context = arg;
  enum bk7258_cp_platform_stage_e stage =
    (enum bk7258_cp_platform_stage_e)stage_desc->id;
  FAR const struct bk7258_storage_config_s *storage = context->storage;
  FAR const struct bk7258_gpio_config_s *gpio = context->gpio;
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

/****************************************************************************
 * Private Data
 ****************************************************************************/

#define BK7258_CP_STAGE_BIT(_id) (UINT32_C(1) << (_id))
#define BK7258_CP_STAGE(_id, _class, _flags, _requires) \
  {(_requires), (_id), (_class), (_flags)}
#define BK7258_CP_STAGE_COUNT_VALUE \
  (sizeof(g_bk7258_cp_stages) / sizeof(g_bk7258_cp_stages[0]))

#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC
#  define BK7258_CP_WDT_REQUIRES \
  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_RESET_MARKER_POLICY)
#else
#  define BK7258_CP_WDT_REQUIRES \
  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)
#endif

#if defined(CONFIG_BK7258_AP_CONTROL) && \
    defined(CONFIG_BK7258_AP_AUTOSTART)
#  define BK7258_CP_PM_COORD_AP_REQUIRES \
  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_AP_START)
#elif defined(CONFIG_BK7258_AP_CONTROL)
#  define BK7258_CP_PM_COORD_AP_REQUIRES \
  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_AP_CONTROL)
#else
#  define BK7258_CP_PM_COORD_AP_REQUIRES 0
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
#  define BK7258_CP_PM_COORD_REQUIRES \
  (BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_PM_SERVER) | \
   BK7258_CP_PM_COORD_AP_REQUIRES)
#else
#  define BK7258_CP_PM_COORD_REQUIRES BK7258_CP_PM_COORD_AP_REQUIRES
#endif

static const struct bk7258_stage_desc_s g_bk7258_cp_stages[] =
{
#ifdef CONFIG_BK7258_SWD_DEBUG
  BK7258_CP_STAGE(BK7258_CP_STAGE_TRACE_ENTRY,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN, 0),
#endif
  BK7258_CP_STAGE(BK7258_CP_STAGE_STORAGE_CONFIG,
                  BK7258_STAGE_MANDATORY, 0, 0),
#ifdef CONFIG_BK7258_OTA
  BK7258_CP_STAGE(BK7258_CP_STAGE_OTA_LAYOUT,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC
  BK7258_CP_STAGE(BK7258_CP_STAGE_RESET_MARKER_POLICY,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
  BK7258_CP_STAGE(BK7258_CP_STAGE_SDK_RUNTIME,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_SWD_DEBUG
  BK7258_CP_STAGE(BK7258_CP_STAGE_DEBUG_ROUTE_AFTER_SDK,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_SARADC_SERVER
  BK7258_CP_STAGE(BK7258_CP_STAGE_SARADC_SERVER,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#ifdef CONFIG_BK7258_PM_CLOCK
  BK7258_CP_STAGE(BK7258_CP_STAGE_PM_SERVER,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#ifdef CONFIG_BK7258_TEMPERATURE
  BK7258_CP_STAGE(BK7258_CP_STAGE_TEMPERATURE_SERVER,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#if defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)
  BK7258_CP_STAGE(BK7258_CP_STAGE_RADIO_STORAGE,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  BK7258_CP_STAGE(BK7258_CP_STAGE_WIFI_CONTROLLER,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_RADIO_STORAGE)),
#endif
#ifdef CONFIG_BK7258_BT_IPC
  BK7258_CP_STAGE(BK7258_CP_STAGE_BT_CONTROLLER,
                  BK7258_STAGE_MANDATORY, 0,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_RADIO_STORAGE)),
#endif
#ifdef CONFIG_BK7258_AP_CONTROL
  BK7258_CP_STAGE(BK7258_CP_STAGE_AP_CONTROL,
                  BK7258_STAGE_MANDATORY, 0, 0),
#  ifdef CONFIG_BK7258_GPIO_LOWERHALF
  BK7258_CP_STAGE(BK7258_CP_STAGE_GPIO_SERVER,
                  BK7258_STAGE_MANDATORY, 0, 0),
#  endif
#endif
#ifdef CONFIG_BK7258_PSRAM
  BK7258_CP_STAGE(BK7258_CP_STAGE_PSRAM,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  BK7258_CP_STAGE(BK7258_CP_STAGE_AP_SUPERVISOR,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#if defined(CONFIG_BK7258_AP_CONTROL) && \
    defined(CONFIG_BK7258_AP_AUTOSTART)
  BK7258_CP_STAGE(BK7258_CP_STAGE_AP_START,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#ifdef CONFIG_BK7258_OTA
  BK7258_CP_STAGE(BK7258_CP_STAGE_OTA_TRIAL,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN |
                  BK7258_STAGE_FLAG_EXTERNAL,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_OTA_LAYOUT)),
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  BK7258_CP_STAGE(BK7258_CP_STAGE_WIFI_CONTROL,
                  BK7258_STAGE_MANDATORY, 0, 0),
#endif
#ifdef CONFIG_BK7258_WDT
  BK7258_CP_STAGE(BK7258_CP_STAGE_WDT,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_WDT_REQUIRES),
#endif
#ifdef CONFIG_BK7258_IRDA
  BK7258_CP_STAGE(BK7258_CP_STAGE_IRDA,
                  BK7258_STAGE_MANDATORY,
                  BK7258_STAGE_FLAG_ALWAYS_RUN, 0),
#endif
#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  BK7258_CP_STAGE(BK7258_CP_STAGE_PM_COORD,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_PM_COORD_REQUIRES),
#endif
#if defined(CONFIG_BK7258_GPIO_LOWERHALF) && \
    !defined(CONFIG_BK7258_AP_CONTROL)
  BK7258_CP_STAGE(BK7258_CP_STAGE_GPIO_FALLBACK,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN, 0),
#endif
#ifdef CONFIG_BK7258_TOUCH
  BK7258_CP_STAGE(BK7258_CP_STAGE_BOARD_DEVICES,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN |
                  BK7258_STAGE_FLAG_EXTERNAL,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
#ifdef CONFIG_BK7258_SWD_DEBUG
  BK7258_CP_STAGE(BK7258_CP_STAGE_DEBUG_ROUTE_FINAL,
                  BK7258_STAGE_BEST_EFFORT,
                  BK7258_STAGE_FLAG_ALWAYS_RUN,
                  BK7258_CP_STAGE_BIT(BK7258_CP_STAGE_STORAGE_CONFIG)),
#endif
};

static mutex_t g_bk7258_cp_binding_lock = NXMUTEX_INITIALIZER;
static struct bk7258_cp_platform_context_s g_bk7258_cp_context;
static bool g_bk7258_cp_bound;

static struct bk7258_stage_runner_s g_bk7258_cp_runner =
{
  .lock = NXMUTEX_INITIALIZER,
  .stages = g_bk7258_cp_stages,
  .execute = bk7258_cp_stage_execute,
  .stage_count = BK7258_CP_STAGE_COUNT_VALUE,
  .state = BK7258_PLATFORM_NEW,
  .first_error_stage = BK7258_STAGE_ID_INVALID,
  .waiting_stage = BK7258_STAGE_ID_INVALID,
};

_Static_assert(BK7258_CP_STAGE_COUNT <= BK7258_STAGE_ID_LIMIT,
               "CP platform stage ID exceeds the status mask");
_Static_assert(BK7258_CP_STAGE_COUNT_VALUE > 0 &&
               BK7258_CP_STAGE_COUNT_VALUE <= BK7258_STAGE_ID_LIMIT,
               "invalid CP platform stage table size");

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_cp_platform_require_binding(void)
{
  int ret = nxmutex_lock(&g_bk7258_cp_binding_lock);

  if (ret < 0)
    {
      return ret;
    }

  ret = g_bk7258_cp_bound ? OK : -EAGAIN;
  nxmutex_unlock(&g_bk7258_cp_binding_lock);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_cp_platform_begin(
  FAR const struct bk7258_storage_config_s *storage,
  FAR const struct bk7258_gpio_config_s *gpio)
{
  int ret = nxmutex_lock(&g_bk7258_cp_binding_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (!g_bk7258_cp_bound)
    {
      g_bk7258_cp_context.storage = storage;
      g_bk7258_cp_context.gpio = gpio;
      g_bk7258_cp_bound = true;
      ret = OK;
    }
  else if (g_bk7258_cp_context.storage != storage ||
           g_bk7258_cp_context.gpio != gpio)
    {
      ret = -EINVAL;
    }
  else
    {
      ret = OK;
    }

  nxmutex_unlock(&g_bk7258_cp_binding_lock);
  return ret;
}

int bk7258_cp_platform_reach_ota_trial(FAR bool *eligible)
{
#ifdef CONFIG_BK7258_OTA
  int ret = bk7258_cp_platform_require_binding();

  if (ret < 0)
    {
      return ret;
    }

  return bk7258_stage_runner_run_until(
    &g_bk7258_cp_runner, &g_bk7258_cp_context,
    BK7258_CP_STAGE_OTA_TRIAL, eligible);
#else
  (void)eligible;
  return -ENOTSUP;
#endif
}

int bk7258_cp_platform_complete_ota_trial(int result)
{
#ifdef CONFIG_BK7258_OTA
  return bk7258_stage_runner_complete_external(
    &g_bk7258_cp_runner, BK7258_CP_STAGE_OTA_TRIAL, result);
#else
  (void)result;
  return -ENOTSUP;
#endif
}

int bk7258_cp_platform_reach_board_devices(FAR bool *eligible)
{
#ifdef CONFIG_BK7258_TOUCH
  int ret = bk7258_cp_platform_require_binding();

  if (ret < 0)
    {
      return ret;
    }

  return bk7258_stage_runner_run_until(
    &g_bk7258_cp_runner, &g_bk7258_cp_context,
    BK7258_CP_STAGE_BOARD_DEVICES, eligible);
#else
  (void)eligible;
  return -ENOTSUP;
#endif
}

int bk7258_cp_platform_complete_board_devices(int result)
{
#ifdef CONFIG_BK7258_TOUCH
  return bk7258_stage_runner_complete_external(
    &g_bk7258_cp_runner, BK7258_CP_STAGE_BOARD_DEVICES, result);
#else
  (void)result;
  return -ENOTSUP;
#endif
}

int bk7258_cp_platform_finish(void)
{
  int ret = bk7258_cp_platform_require_binding();

  if (ret < 0)
    {
      return ret;
    }

  return bk7258_stage_runner_finish(
    &g_bk7258_cp_runner, &g_bk7258_cp_context);
}

int bk7258_cp_platform_result(void)
{
  return bk7258_stage_runner_result(&g_bk7258_cp_runner);
}

int bk7258_cp_platform_get_status(
  FAR struct bk7258_platform_status_s *status)
{
  return bk7258_stage_runner_snapshot(&g_bk7258_cp_runner, status);
}
