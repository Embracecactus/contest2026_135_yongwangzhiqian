/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host fault-injection test for the real board-owned AP initial entry.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/compiler.h>

#include <arch/chip/bk7258_amp.h>

int bk7258_ap_main(int argc, char *argv[]);

enum event_e
{
  EVENT_CHIP_STARTUP,
  EVENT_BOARD_DEVICES,
  EVENT_PRODUCT_PREPARE,
  EVENT_CHIP_READY,
  EVENT_PRODUCT_START,
  EVENT_CHIP_SUPERVISE,
  EVENT_CHIP_FAIL,
};

enum scenario_e
{
  SCENARIO_SUCCESS,
  SCENARIO_STARTUP_FAIL,
  SCENARIO_BOARD_FAIL,
  SCENARIO_PRODUCT_PREPARE_FAIL,
  SCENARIO_READY_FAIL,
  SCENARIO_PRODUCT_START_FAIL,
};

static jmp_buf g_exit;
static enum scenario_e g_scenario;
static int g_events[8];
static int g_event_count;
static uint32_t g_failure;

static void event(enum event_e value)
{
  g_events[g_event_count++] = value;
}

int bk7258_ap_lifecycle_startup(uint32_t *failure)
{
  assert(failure != NULL);
  event(EVENT_CHIP_STARTUP);
  if (g_scenario == SCENARIO_STARTUP_FAIL)
    {
      *failure = BK7258_AP_ERROR_PSRAM;
      return -ENOMEM;
    }

  *failure = BK7258_AP_ERROR_NONE;
  return 0;
}

int bk7258_board_ap_initialize(void)
{
  event(EVENT_BOARD_DEVICES);
  return g_scenario == SCENARIO_BOARD_FAIL ? -ENODEV : 0;
}

int bk7258_product_prepare(void)
{
  event(EVENT_PRODUCT_PREPARE);
  return g_scenario == SCENARIO_PRODUCT_PREPARE_FAIL ? -EIO : 0;
}

int bk7258_ap_lifecycle_publish_ready(uint32_t *failure)
{
  assert(failure != NULL);
  event(EVENT_CHIP_READY);
  if (g_scenario == SCENARIO_READY_FAIL)
    {
      *failure = BK7258_AP_ERROR_BAD_BOOT_STATE;
      return -EHOSTDOWN;
    }

  *failure = BK7258_AP_ERROR_NONE;
  return 0;
}

int bk7258_product_start(void)
{
  event(EVENT_PRODUCT_START);
  return g_scenario == SCENARIO_PRODUCT_START_FAIL ? -EAGAIN : 0;
}

void bk7258_ap_lifecycle_supervise(void)
{
  event(EVENT_CHIP_SUPERVISE);
  longjmp(g_exit, 1);
}

void bk7258_ap_lifecycle_fail_and_park(uint32_t failure)
{
  event(EVENT_CHIP_FAIL);
  g_failure = failure;
  longjmp(g_exit, 2);
}

static void run_scenario(enum scenario_e scenario,
                         const int *expected, int expected_count,
                         int expected_exit, uint32_t expected_failure)
{
  int outcome;

  g_scenario = scenario;
  g_event_count = 0;
  g_failure = BK7258_AP_ERROR_NONE;
  outcome = setjmp(g_exit);
  if (outcome == 0)
    {
      (void)bk7258_ap_main(0, NULL);
      assert(false);
    }

  assert(outcome == expected_exit);
  assert(g_event_count == expected_count);
  assert(memcmp(g_events, expected,
                sizeof(*expected) * expected_count) == 0);
  assert(g_failure == expected_failure);
}

int main(void)
{
  static const int success[] =
  {
    EVENT_CHIP_STARTUP, EVENT_BOARD_DEVICES, EVENT_PRODUCT_PREPARE,
    EVENT_CHIP_READY, EVENT_PRODUCT_START, EVENT_CHIP_SUPERVISE
  };
  static const int startup_fail[] =
  {
    EVENT_CHIP_STARTUP, EVENT_CHIP_FAIL
  };
  static const int board_fail[] =
  {
    EVENT_CHIP_STARTUP, EVENT_BOARD_DEVICES, EVENT_CHIP_FAIL
  };
  static const int prepare_fail[] =
  {
    EVENT_CHIP_STARTUP, EVENT_BOARD_DEVICES, EVENT_PRODUCT_PREPARE,
    EVENT_CHIP_READY, EVENT_CHIP_SUPERVISE
  };
  static const int ready_fail[] =
  {
    EVENT_CHIP_STARTUP, EVENT_BOARD_DEVICES, EVENT_PRODUCT_PREPARE,
    EVENT_CHIP_READY, EVENT_CHIP_FAIL
  };

  run_scenario(SCENARIO_SUCCESS, success, (int)nitems(success), 1,
               BK7258_AP_ERROR_NONE);
  run_scenario(SCENARIO_STARTUP_FAIL, startup_fail,
               (int)nitems(startup_fail), 2, BK7258_AP_ERROR_PSRAM);
  run_scenario(SCENARIO_BOARD_FAIL, board_fail, (int)nitems(board_fail), 2,
               BK7258_AP_ERROR_PERIPHERALS);
  run_scenario(SCENARIO_PRODUCT_PREPARE_FAIL, prepare_fail,
               (int)nitems(prepare_fail), 1, BK7258_AP_ERROR_NONE);
  run_scenario(SCENARIO_READY_FAIL, ready_fail, (int)nitems(ready_fail), 2,
               BK7258_AP_ERROR_BAD_BOOT_STATE);
  run_scenario(SCENARIO_PRODUCT_START_FAIL, success,
               (int)nitems(success), 1, BK7258_AP_ERROR_NONE);

  puts("bk7258 AP entry tests: PASS");
  return 0;
}
