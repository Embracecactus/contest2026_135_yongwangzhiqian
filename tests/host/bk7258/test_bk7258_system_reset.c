/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host contract test for typed BK7258 whole-device reset intent.
 ****************************************************************************/

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arch/chip/bk7258_system_reset.h>

#include <driver/aon_wdt.h>

static uint32_t g_expected_source;
static unsigned int g_step;
static bool g_fail_aon;

static void child_fail(unsigned int code) __attribute__((noreturn));

static void child_fail(unsigned int code)
{
  _exit((int)code);
}

irqstate_t up_irq_save(void)
{
  if (g_step++ != 0u)
    {
      child_fail(10u);
    }

  return 0x1234u;
}

void up_irq_restore(irqstate_t flags)
{
  (void)flags;
  child_fail(11u);
}

void bk_misc_set_reset_reason(uint32_t source)
{
  if (g_step++ != 1u || source != g_expected_source)
    {
      child_fail(12u);
    }
}

void aon_pmu_drv_wdt_change_not_rosc_clk(void)
{
  if (g_step++ != 2u)
    {
      child_fail(13u);
    }
}

void aon_pmu_drv_wdt_rst_dev_enable(void)
{
  if (g_step++ != 3u)
    {
      child_fail(14u);
    }
}

bk_err_t bk_aon_wdt_set_period(uint32_t period)
{
  if (g_step++ != 4u || period != 10u)
    {
      child_fail(15u);
    }

  if (g_fail_aon)
    {
      return -1;
    }

  child_fail(0u);
}

void up_systemreset(void)
{
  if (!g_fail_aon || g_step != 5u)
    {
      child_fail(16u);
    }

  child_fail(0u);
}

static void run_case(enum bk7258_reset_source_e requested,
                     enum bk7258_reset_source_e expected,
                     bool fail_aon)
{
  int status;
  pid_t child = fork();

  assert(child >= 0);
  if (child == 0)
    {
      g_expected_source = (uint32_t)expected;
      g_step = 0;
      g_fail_aon = fail_aon;
      bk7258_system_reset(requested);
    }

  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status));
  assert(WEXITSTATUS(status) == 0);
}

int main(void)
{
  run_case(BK7258_RESET_SOURCE_REBOOT,
           BK7258_RESET_SOURCE_REBOOT, false);
  run_case(BK7258_RESET_SOURCE_WATCHDOG,
           BK7258_RESET_SOURCE_WATCHDOG, false);
  run_case(BK7258_RESET_SOURCE_NMI_WDT,
           BK7258_RESET_SOURCE_NMI_WDT, false);

  /* POWERON and unknown values are observations, not valid reset intents. */

  run_case(BK7258_RESET_SOURCE_POWERON,
           BK7258_RESET_SOURCE_REBOOT, false);
  run_case((enum bk7258_reset_source_e)0x7f,
           BK7258_RESET_SOURCE_REBOOT, false);

  /* Failure to arm the AON whole-device watchdog must fall back instead of
   * parking forever.
   */

  run_case(BK7258_RESET_SOURCE_REBOOT,
           BK7258_RESET_SOURCE_REBOOT, true);

  puts("bk7258 typed system-reset contract test: PASS");
  return 0;
}
