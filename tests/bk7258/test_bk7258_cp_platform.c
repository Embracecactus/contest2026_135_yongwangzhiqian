/****************************************************************************
 * Host fault-injection test for the real CP platform orchestration source.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_storage_config.h>
#include <arch/chip/bk7258_cp_platform.h>
#include <arch/chip/bk7258_platform.h>
#include <arch/chip/bk7258_psram.h>

#include "bk7258_internal.h"
#include "bk7258_radio_storage.h"

#ifdef CONFIG_BK7258_SWD_DEBUG
#  include <arch/chip/bk7258_debug.h>
#endif

enum event_e
{
  EVENT_STORAGE_CONFIG,
  EVENT_OTA_LAYOUT,
  EVENT_RESET_MARKER_POLICY,
  EVENT_RADIO_STORAGE,
  EVENT_BT_CONTROLLER,
  EVENT_SDK,
  EVENT_AP_CONTROL,
  EVENT_PSRAM,
  EVENT_PSRAM_HEAP,
  EVENT_AP_START,
  EVENT_OTA,
  EVENT_WDT,
  EVENT_IRDA,
  EVENT_DEBUG_ROUTE,
  EVENT_GPIO_FALLBACK,
  EVENT_BOARD_DEVICES,
};

static int g_events[16];
static int g_event_count;
static int g_sdk_result;
static int g_psram_result;
static int g_ap_start_result;
static int g_ota_layout_result;
#if TEST_CP_SCENARIO != 10
static int g_ota_result;
#endif
static int g_wdt_result;
static int g_reset_marker_policy_result;
static int g_radio_storage_result;

static void event(enum event_e event)
{
  g_events[g_event_count++] = event;
}

int bk7258_sdk_runtime_initialize(void)
{
  event(EVENT_SDK);
  return g_sdk_result;
}

int bk7258_ap_control_initialize(
  const struct bk7258_ap_image_desc_s *image)
{
  assert(image != NULL);
  event(EVENT_AP_CONTROL);
  return 0;
}

int bk7258_psram_initialize(void)
{
  event(EVENT_PSRAM);
  return g_psram_result;
}

int bk7258_psram_add_system_heap(uint32_t size)
{
  assert(size == 4096u);
  event(EVENT_PSRAM_HEAP);
  return 0;
}

int bk7258_psram_get_info(struct bk7258_psram_info_s *info)
{
  memset(info, 0, sizeof(*info));
  return 0;
}

int bk7258_ap_start(uint32_t timeout_ms)
{
  assert(timeout_ms == 42u);
  event(EVENT_AP_START);
  return g_ap_start_result;
}

int bk7258_wdt_initialize(void)
{
  event(EVENT_WDT);
  return g_wdt_result;
}

int bk7258_irda_initialize(void)
{
  event(EVENT_IRDA);
  return 0;
}

#ifdef CONFIG_BK7258_SWD_DEBUG
void bk7258_swd_trace_snapshot(uint32_t stage)
{
  (void)stage;
}

int bk7258_swd_initialize(void)
{
  event(EVENT_DEBUG_ROUTE);
  return 0;
}
#endif

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
int bk7258_gpio_lowerhalf_initialize(
  const struct bk7258_gpio_config_s *config)
{
  assert(config == &g_bk7258_board_gpio_config);
  event(EVENT_GPIO_FALLBACK);
  return 0;
}
#endif

#ifdef CONFIG_BK7258_TOUCH
int bk7258_board_cp_devices_initialize(void)
{
  event(EVENT_BOARD_DEVICES);
  return 0;
}
#endif

#if TEST_CP_SCENARIO != 10
int bk7258_ota_trial_initialize(void)
{
  event(EVENT_OTA);
  return g_ota_result;
}
#endif

const struct bk7258_gpio_config_s g_bk7258_board_gpio_config =
{
  .name = "host-test",
};

static const struct bk7258_radio_storage_config_s g_radio_storage =
{
  .version = BK7258_RADIO_STORAGE_CONFIG_VERSION,
  .size = sizeof(struct bk7258_radio_storage_config_s),
};

const struct bk7258_storage_config_s g_bk7258_board_storage_config =
{
  .version = BK7258_STORAGE_CONFIG_VERSION,
  .size = sizeof(struct bk7258_storage_config_s),
  .radio_storage = &g_radio_storage,
};
static const struct bk7258_ota_layout_s g_ota_layout;

int bk7258_storage_configure(
  const struct bk7258_storage_config_s *config)
{
  assert(config == &g_bk7258_board_storage_config);
  event(EVENT_STORAGE_CONFIG);
  return 0;
}

int bk7258_storage_ota_layout(
  const struct bk7258_ota_layout_s **layout)
{
  assert(layout != NULL);
  event(EVENT_OTA_LAYOUT);
  if (g_ota_layout_result == 0)
    {
      *layout = &g_ota_layout;
    }

  return g_ota_layout_result;
}

int bk7258_storage_marker_address(uint32_t *address)
{
  assert(address != NULL);
  event(EVENT_RESET_MARKER_POLICY);
  if (g_reset_marker_policy_result == 0)
    {
      *address = 0x8000u;
    }

  return g_reset_marker_policy_result;
}

int bk7258_storage_radio_config(
  const struct bk7258_radio_storage_config_s **config)
{
  assert(config != NULL);
  *config = &g_radio_storage;
  return 0;
}

int bk7258_radio_storage_initialize(
  const struct bk7258_radio_storage_config_s *config)
{
  assert(config == &g_radio_storage);
  event(EVENT_RADIO_STORAGE);
  return g_radio_storage_result;
}

int bk7258_bt_controller_ipc_initialize(void)
{
  event(EVENT_BT_CONTROLLER);
  return 0;
}

static void expect_events(const int *expected, int count)
{
  assert(g_event_count == count);
  assert(memcmp(g_events, expected, sizeof(*expected) * count) == 0);
}

int main(void)
{
  struct bk7258_platform_status_s status;

#if TEST_CP_SCENARIO == 1
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT, EVENT_RESET_MARKER_POLICY,
    EVENT_SDK, EVENT_AP_CONTROL, EVENT_PSRAM, EVENT_PSRAM_HEAP,
    EVENT_AP_START, EVENT_OTA, EVENT_WDT, EVENT_IRDA
  };
  assert(bk7258_cp_bringup_initialize() == 0);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_bringup_initialize() == 0);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_bringup_result() == 0);
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.failed_mask == 0);
#elif TEST_CP_SCENARIO == 2
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT, EVENT_RESET_MARKER_POLICY,
    EVENT_SDK, EVENT_PSRAM, EVENT_PSRAM_HEAP,
    EVENT_OTA, EVENT_WDT, EVENT_IRDA
  };
  g_sdk_result = -EIO;
  assert(bk7258_cp_bringup_initialize() == -EIO);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_CP_STAGE_SDK_RUNTIME);
  assert((status.skipped_mask & (UINT32_C(1) << BK7258_CP_STAGE_AP_CONTROL)) != 0);
  assert((status.skipped_mask & (UINT32_C(1) << BK7258_CP_STAGE_AP_START)) != 0);
#elif TEST_CP_SCENARIO == 3
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT, EVENT_RESET_MARKER_POLICY,
    EVENT_SDK, EVENT_AP_CONTROL, EVENT_PSRAM, EVENT_PSRAM_HEAP,
    EVENT_AP_START, EVENT_OTA, EVENT_WDT, EVENT_IRDA
  };
  g_wdt_result = -ETIMEDOUT;
  assert(bk7258_cp_bringup_initialize() == -ETIMEDOUT);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_CP_STAGE_WDT);
  assert((status.succeeded_mask & (UINT32_C(1) << BK7258_CP_STAGE_IRDA)) != 0);
#elif TEST_CP_SCENARIO == 4
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT, EVENT_RESET_MARKER_POLICY,
    EVENT_PSRAM, EVENT_PSRAM_HEAP, EVENT_OTA, EVENT_IRDA
  };
  g_reset_marker_policy_result = -ENODEV;
  assert(bk7258_cp_bringup_initialize() == -ENODEV);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_CP_STAGE_RESET_MARKER_POLICY);
#elif TEST_CP_SCENARIO == 5
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT, EVENT_RESET_MARKER_POLICY,
    EVENT_PSRAM, EVENT_PSRAM_HEAP, EVENT_WDT, EVENT_IRDA
  };
  g_ota_layout_result = -EACCES;
  assert(bk7258_cp_bringup_initialize() == -EACCES);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_CP_STAGE_OTA_LAYOUT);
  assert((status.skipped_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_OTA_TRIAL)) != 0);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_WDT)) != 0);
#elif TEST_CP_SCENARIO == 6
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT, EVENT_RESET_MARKER_POLICY,
    EVENT_SDK, EVENT_AP_CONTROL, EVENT_PSRAM, EVENT_PSRAM_HEAP,
    EVENT_AP_START, EVENT_OTA, EVENT_WDT, EVENT_IRDA
  };
  g_ap_start_result = -EHOSTDOWN;
  assert(bk7258_cp_bringup_initialize() == -EHOSTDOWN);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_CP_STAGE_AP_START);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_OTA_TRIAL)) != 0);
#elif TEST_CP_SCENARIO == 7
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT, EVENT_RESET_MARKER_POLICY,
    EVENT_SDK, EVENT_AP_CONTROL, EVENT_PSRAM, EVENT_OTA, EVENT_WDT,
    EVENT_IRDA
  };
  g_psram_result = -ENOMEM;
  assert(bk7258_cp_bringup_initialize() == -ENOMEM);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_CP_STAGE_PSRAM);
  assert((status.skipped_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_AP_START)) != 0);
#elif TEST_CP_SCENARIO == 8
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT, EVENT_RESET_MARKER_POLICY,
    EVENT_SDK, EVENT_AP_CONTROL, EVENT_PSRAM, EVENT_PSRAM_HEAP,
    EVENT_AP_START, EVENT_OTA, EVENT_WDT, EVENT_IRDA
  };
  g_ota_result = -ETIMEDOUT;
  assert(bk7258_cp_bringup_initialize() == -ETIMEDOUT);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage ==
         BK7258_CP_STAGE_OTA_TRIAL);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_WDT)) != 0);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_IRDA)) != 0);
#elif TEST_CP_SCENARIO == 9
  static const int expected[] =
  {
    EVENT_SDK, EVENT_DEBUG_ROUTE, EVENT_IRDA, EVENT_GPIO_FALLBACK,
    EVENT_BOARD_DEVICES, EVENT_DEBUG_ROUTE
  };
  g_sdk_result = -EIO;
  assert(bk7258_cp_bringup_initialize() == -EIO);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_CP_STAGE_SDK_RUNTIME);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_DEBUG_ROUTE_AFTER_SDK)) != 0);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_IRDA)) != 0);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_GPIO_FALLBACK)) != 0);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_BOARD_DEVICES)) != 0);
#elif TEST_CP_SCENARIO == 10
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_OTA_LAYOUT
  };
  assert(bk7258_cp_bringup_initialize() == 0);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_OTA_TRIAL)) != 0);
#elif TEST_CP_SCENARIO == 11
  static const int expected[] =
  {
    EVENT_STORAGE_CONFIG, EVENT_RADIO_STORAGE
  };
  g_radio_storage_result = -EIO;
  assert(bk7258_cp_bringup_initialize() == -EIO);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_cp_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_CP_STAGE_RADIO_STORAGE);
  assert((status.skipped_mask &
          (UINT32_C(1) << BK7258_CP_STAGE_BT_CONTROLLER)) != 0);
#else
#  error "TEST_CP_SCENARIO must be in the range 1..11"
#endif

  puts("bk7258 CP platform tests: PASS");
  return 0;
}
