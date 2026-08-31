/****************************************************************************
 * chips/bk7258/
 * bk7258_sdk_irq_timer_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Manual non-WDT hardware test for the CPU0 Beken SDK IRQ bridge.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <nuttx/spinlock.h>

#include <driver/int.h>
#include <driver/timer.h>

#include "bk7258_sdk_irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_IRQTEST_TIMER_ID          TIMER_ID1
#define BK7258_IRQTEST_SOURCE            INT_SRC_TIMER
#define BK7258_IRQTEST_PERIOD_MS         50u
#define BK7258_IRQTEST_WAIT_STEP_US      10000u
#define BK7258_IRQTEST_WAIT_STEPS        50u
#define BK7258_IRQTEST_SILENCE_US        \
  (BK7258_IRQTEST_PERIOD_MS * 3u * 1000u)
#define BK7258_IRQTEST_STATUS_MASK       (1u << TIMER_ID1)
#define BK7258_IRQTEST_ALL_TIMERS_MASK   ((1u << TIMER_ID_MAX) - 1u)

/****************************************************************************
 * Compile-time Invariants
 ****************************************************************************/

_Static_assert(TIMER_ID1 == 1,
               "Stage B timer test requires TIMER_ID1 to remain channel 1");
_Static_assert(TIMER_ID_MAX == 6,
               "Stage B timer test requires six SDK timer channels");
_Static_assert(INT_SRC_TIMER == 3,
               "Stage B timer test requires INT_SRC_TIMER to remain source 3");
_Static_assert((CONFIG_TIMER_SUPPORT_ID_BITS &
                BK7258_IRQTEST_STATUS_MASK) != 0,
               "Stage B timer test requires SDK support for TIMER_ID1");

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_bk7258_irqtest_running;
static volatile uint32_t g_bk7258_irqtest_a_count;
static volatile uint32_t g_bk7258_irqtest_a_status;
static volatile uint32_t g_bk7258_irqtest_b_count;
static volatile uint32_t g_bk7258_irqtest_b_status;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* The pinned SDK exports this helper from timer_driver.c but does not declare
 * it in the public timer header.  It returns and clears the six-channel raw
 * interrupt-status bitmap.
 */

extern uint32_t timer_clear_isr_status(void);

static bool bk7258_irqtest_claim(void)
{
  irqstate_t flags;
  bool claimed;

  flags = enter_critical_section();
  claimed = !g_bk7258_irqtest_running;
  if (claimed)
    {
      g_bk7258_irqtest_running = true;
    }

  leave_critical_section(flags);
  return claimed;
}

static void bk7258_irqtest_release(void)
{
  irqstate_t flags;

  flags = enter_critical_section();
  g_bk7258_irqtest_running = false;
  leave_critical_section(flags);
}

static bool
bk7258_irqtest_wait(volatile uint32_t *counter)
{
  unsigned int step;

  for (step = 0; step < BK7258_IRQTEST_WAIT_STEPS; step++)
    {
      if (*counter != 0)
        {
          return true;
        }

      usleep(BK7258_IRQTEST_WAIT_STEP_US);
    }

  return false;
}

static void bk7258_irqtest_callback_a(void)
{
  uint32_t status = timer_clear_isr_status();

  g_bk7258_irqtest_a_status |= status;
  if ((status & BK7258_IRQTEST_STATUS_MASK) != 0)
    {
      g_bk7258_irqtest_a_count++;
    }
}

static void bk7258_irqtest_callback_b(void)
{
  uint32_t status = timer_clear_isr_status();

  g_bk7258_irqtest_b_status |= status;
  if ((status & BK7258_IRQTEST_STATUS_MASK) != 0)
    {
      g_bk7258_irqtest_b_count++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_sdk_irq_timer_test(void)
{
  int_group_isr_t saved_handler = NULL;
  const char *phase = "claim";
  uint32_t silence_a;
  uint32_t silence_b;
  uint32_t silence_status;
  uint32_t enabled;
  bk_err_t restore_ret;
  bk_err_t ret;
  bool restore_needed = false;
  bool timer_touched = false;
  int result = -1;

  if (!bk7258_irqtest_claim())
    {
      printf("bkirqtest: FAIL busy\n");
      return -1;
    }

  printf("bkirqtest: BEGIN timer=%u source=%u irq=%u period=%ums\n",
         (unsigned int)BK7258_IRQTEST_TIMER_ID,
         (unsigned int)BK7258_IRQTEST_SOURCE,
         (unsigned int)(BK7258_IRQ_FIRST + BK7258_IRQTEST_SOURCE),
         (unsigned int)BK7258_IRQTEST_PERIOD_MS);

  phase = "snapshot";
  ret = bk7258_sdk_irq_test_snapshot_handler(BK7258_IRQTEST_SOURCE,
                                              &saved_handler);
  if (ret != BK_OK || saved_handler == NULL)
    {
      printf("bkirqtest: snapshot ret=%d handler=0x%08lx\n",
             (int)ret, (unsigned long)(uintptr_t)saved_handler);
      goto out;
    }

  phase = "idle-guard";
  enabled = bk_timer_get_enable_status();
  if (enabled == (uint32_t)BK_ERR_TIMER_NOT_INIT)
    {
      printf("bkirqtest: timer driver is not initialized\n");
      goto out;
    }

  if ((enabled & BK7258_IRQTEST_ALL_TIMERS_MASK) != 0)
    {
      printf("bkirqtest: timers busy enable=0x%08lx\n",
             (unsigned long)enabled);
      goto out;
    }

  timer_touched = true;
  ret = bk_timer_stop(BK7258_IRQTEST_TIMER_ID);
  if (ret != BK_OK)
    {
      phase = "initial-stop";
      goto out;
    }

  timer_clear_isr_status();
  g_bk7258_irqtest_a_count = 0;
  g_bk7258_irqtest_a_status = 0;
  g_bk7258_irqtest_b_count = 0;
  g_bk7258_irqtest_b_status = 0;

  phase = "register-a";
  restore_needed = true;
  ret = bk_int_isr_register(BK7258_IRQTEST_SOURCE,
                            bk7258_irqtest_callback_a, NULL);
  if (ret != BK_OK)
    {
      goto out;
    }

  phase = "trigger-a";
  ret = bk_timer_start_without_callback(BK7258_IRQTEST_TIMER_ID,
                                         BK7258_IRQTEST_PERIOD_MS);
  if (ret != BK_OK ||
      !bk7258_irqtest_wait(&g_bk7258_irqtest_a_count))
    {
      goto out;
    }

  ret = bk_timer_stop(BK7258_IRQTEST_TIMER_ID);
  timer_clear_isr_status();
  if (ret != BK_OK || g_bk7258_irqtest_a_count == 0 ||
      (g_bk7258_irqtest_a_status & BK7258_IRQTEST_STATUS_MASK) == 0)
    {
      goto out;
    }

  printf("bkirqtest: A count=%lu status=0x%08lx\n",
         (unsigned long)g_bk7258_irqtest_a_count,
         (unsigned long)g_bk7258_irqtest_a_status);

  silence_a = g_bk7258_irqtest_a_count;
  silence_b = g_bk7258_irqtest_b_count;

  phase = "unregister";
  ret = bk_int_isr_unregister(BK7258_IRQTEST_SOURCE);
  if (ret != BK_OK)
    {
      goto out;
    }

  phase = "silence-trigger";
  ret = bk_timer_start_without_callback(BK7258_IRQTEST_TIMER_ID,
                                         BK7258_IRQTEST_PERIOD_MS);
  if (ret != BK_OK)
    {
      goto out;
    }

  usleep(BK7258_IRQTEST_SILENCE_US);
  silence_status = timer_clear_isr_status();
  ret = bk_timer_stop(BK7258_IRQTEST_TIMER_ID);
  timer_clear_isr_status();
  if (ret != BK_OK ||
      (silence_status & BK7258_IRQTEST_STATUS_MASK) == 0 ||
      g_bk7258_irqtest_a_count != silence_a ||
      g_bk7258_irqtest_b_count != silence_b)
    {
      goto out;
    }

  printf("bkirqtest: SILENT a=%lu b=%lu status=0x%08lx\n",
         (unsigned long)g_bk7258_irqtest_a_count,
         (unsigned long)g_bk7258_irqtest_b_count,
         (unsigned long)silence_status);

  phase = "register-b";
  ret = bk_int_isr_register(BK7258_IRQTEST_SOURCE,
                            bk7258_irqtest_callback_b, NULL);
  if (ret != BK_OK)
    {
      goto out;
    }

  phase = "trigger-b";
  ret = bk_timer_start_without_callback(BK7258_IRQTEST_TIMER_ID,
                                         BK7258_IRQTEST_PERIOD_MS);
  if (ret != BK_OK ||
      !bk7258_irqtest_wait(&g_bk7258_irqtest_b_count))
    {
      goto out;
    }

  ret = bk_timer_stop(BK7258_IRQTEST_TIMER_ID);
  timer_clear_isr_status();
  if (ret != BK_OK || g_bk7258_irqtest_b_count == 0 ||
      (g_bk7258_irqtest_b_status & BK7258_IRQTEST_STATUS_MASK) == 0 ||
      g_bk7258_irqtest_a_count != silence_a)
    {
      goto out;
    }

  printf("bkirqtest: B count=%lu status=0x%08lx\n",
         (unsigned long)g_bk7258_irqtest_b_count,
         (unsigned long)g_bk7258_irqtest_b_status);
  result = 0;

out:
  if (timer_touched)
    {
      bk_timer_stop(BK7258_IRQTEST_TIMER_ID);
      timer_clear_isr_status();
    }

  if (restore_needed)
    {
      restore_ret = bk_int_isr_register(BK7258_IRQTEST_SOURCE,
                                        saved_handler, NULL);
      if (restore_ret != BK_OK)
        {
          printf("bkirqtest: restore FAIL ret=%d\n", (int)restore_ret);
          phase = "restore";
          result = -1;
        }
      else
        {
          printf("bkirqtest: restore OK\n");
        }
    }

  if (result == 0)
    {
      printf("bkirqtest: PASS\n");
    }
  else
    {
      printf("bkirqtest: FAIL phase=%s\n", phase);
    }

  bk7258_irqtest_release();
  return result;
}
