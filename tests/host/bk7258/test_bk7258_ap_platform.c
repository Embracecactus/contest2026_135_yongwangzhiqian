/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host fault-injection test for the real AP platform preparation source.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/chip/bk7258_ap_platform.h>

void board_late_initialize(void);

enum event_e
{
  EVENT_SDK,
  EVENT_PSRAM,
  EVENT_PM,
  EVENT_TEMPERATURE,
};

static int g_events[8];
static int g_event_count;
static int g_sdk_result;
static int g_psram_result;
static int g_pm_result;

static void event(enum event_e event)
{
  g_events[g_event_count++] = event;
}

int bk7258_sdk_runtime_initialize(void)
{
  event(EVENT_SDK);
  return g_sdk_result;
}

int bk7258_psram_initialize(void)
{
  event(EVENT_PSRAM);
  return g_psram_result;
}

int bk7258_pm_initialize(void)
{
  event(EVENT_PM);
  return g_pm_result;
}

int bk7258_temperature_initialize(void)
{
  event(EVENT_TEMPERATURE);
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

#if TEST_AP_SCENARIO == 1
  static const int expected[] =
  {
    EVENT_SDK, EVENT_PM, EVENT_TEMPERATURE, EVENT_PSRAM
  };
  board_late_initialize();
  assert(bk7258_ap_platform_result() == 0);
  expect_events(expected, (int)nitems(expected));
  (void)status;
  assert(bk7258_ap_platform_prepare() == 0);
  expect_events(expected, (int)nitems(expected));
#elif TEST_AP_SCENARIO == 2
  static const int expected[] = {EVENT_SDK, EVENT_PSRAM};
  g_sdk_result = -EIO;
  board_late_initialize();
  assert(bk7258_ap_platform_result() == -EIO);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_ap_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_AP_STAGE_SDK_RUNTIME);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_AP_STAGE_PSRAM)) != 0);
  assert((status.skipped_mask &
          (UINT32_C(1) << BK7258_AP_STAGE_PM_CLIENT)) != 0);
  assert((status.skipped_mask &
          (UINT32_C(1) << BK7258_AP_STAGE_TEMPERATURE_CLIENT)) != 0);
#elif TEST_AP_SCENARIO == 3
  static const int expected[] =
  {
    EVENT_SDK, EVENT_PM, EVENT_TEMPERATURE, EVENT_PSRAM
  };
  g_psram_result = -ENOMEM;
  board_late_initialize();
  assert(bk7258_ap_platform_result() == -ENOMEM);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_ap_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_AP_STAGE_PSRAM);
  assert((status.failed_mask &
          (UINT32_C(1) << BK7258_AP_STAGE_PSRAM)) != 0);
#elif TEST_AP_SCENARIO == 4
  static const int expected[] =
  {
    EVENT_SDK, EVENT_PM, EVENT_PSRAM
  };
  g_pm_result = -EHOSTDOWN;
  board_late_initialize();
  assert(bk7258_ap_platform_result() == -EHOSTDOWN);
  expect_events(expected, (int)nitems(expected));
  assert(bk7258_ap_platform_get_status(&status) == 0);
  assert(status.first_error_stage == BK7258_AP_STAGE_PM_CLIENT);
  assert((status.skipped_mask &
          (UINT32_C(1) << BK7258_AP_STAGE_TEMPERATURE_CLIENT)) != 0);
  assert((status.succeeded_mask &
          (UINT32_C(1) << BK7258_AP_STAGE_PSRAM)) != 0);
#else
#  error "TEST_AP_SCENARIO must be 1, 2, 3, or 4"
#endif

  puts("bk7258 AP platform tests: PASS");
  return 0;
}
