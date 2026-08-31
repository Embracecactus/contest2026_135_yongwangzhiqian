/****************************************************************************
 * chips/bk7258/cp/
 * bk7258_sdk_timer_selftest.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <os/os.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SDK_TIMER_TEST_PERIOD_MS   5u
#define BK7258_SDK_TIMER_TEST_TIMEOUT_MS  1000u
#define BK7258_SDK_TIMER_TEST_SETTLE_US   20000u
#define BK7258_SDK_TIMER_TEST_LONG_CALLBACK_US 20000u
#define BK7258_SDK_TIMER_TEST_LONG_SETTLE_US   40000u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_sdk_timer_test_context_s
{
  beken_timer_t timer;
  sem_t done;
  volatile uint32_t callbacks;
  volatile pid_t callback_pid;
  volatile int callback_cpu;
  volatile int deinit_status;
  volatile bool interrupt_context;
  uint32_t callback_delay_us;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_sdk_timer_test_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_sdk_timer_test_callback(void *arg)
{
  struct bk7258_sdk_timer_test_context_s *context = arg;

  context->callback_pid = nxsched_gettid();
  context->callback_cpu = sched_getcpu();
  context->interrupt_context = rtos_is_in_interrupt_context();

  if (context->callback_delay_us != 0)
    {
      /* The periodic watchdog is rearmed before this task-context callback.
       * Sleeping beyond one period forces an expiry to queue while the
       * callback is still running, then self-deinit exercises queue ownership.
       */

      (void)nxsig_usleep(context->callback_delay_us);
    }

  context->deinit_status = rtos_deinit_timer(&context->timer);
  context->callbacks++;
  (void)nxsem_post(&context->done);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_sdk_timer_selftest(uint32_t iterations)
{
  struct bk7258_sdk_timer_test_context_s context;
  pid_t caller_pid = nxsched_gettid();
  pid_t service_pid = -1;
  const char *phase = "lifecycle";
  uint32_t iteration;
  int ret;

  if (iterations == 0 || iterations > 4096u)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_sdk_timer_test_lock);
  if (ret < 0)
    {
      return ret;
    }

  printf("BTMR BEGIN iterations=%lu period_ms=%u caller_pid=%d\n",
         (unsigned long)iterations, BK7258_SDK_TIMER_TEST_PERIOD_MS,
         (int)caller_pid);

  for (iteration = 0; iteration < iterations; iteration++)
    {
      memset(&context, 0, sizeof(context));
      context.callback_pid = -1;
      context.callback_cpu = -1;
      context.deinit_status = BK_FAIL;

      ret = nxsem_init(&context.done, 0, 0);
      if (ret < 0)
        {
          goto failed;
        }

      ret = rtos_init_timer(&context.timer,
                            BK7258_SDK_TIMER_TEST_PERIOD_MS,
                            bk7258_sdk_timer_test_callback, &context);
      if (ret != BK_OK)
        {
          ret = -EIO;
          nxsem_destroy(&context.done);
          goto failed;
        }

      ret = rtos_start_timer(&context.timer);
      if (ret != BK_OK)
        {
          (void)rtos_deinit_timer(&context.timer);
          ret = -EIO;
          nxsem_destroy(&context.done);
          goto failed;
        }

      ret = nxsem_tickwait_uninterruptible(
        &context.done, MSEC2TICK(BK7258_SDK_TIMER_TEST_TIMEOUT_MS));
      if (ret < 0)
        {
          (void)rtos_deinit_timer(&context.timer);
          nxsem_destroy(&context.done);
          goto failed;
        }

      /* Let the timer-service task return from the callback and perform its
       * deferred free.  A broken implementation rearms the freed watchdog
       * here and either invokes the callback again or faults in wd_timer().
       */

      (void)nxsig_usleep(BK7258_SDK_TIMER_TEST_SETTLE_US);

      if (service_pid < 0)
        {
          service_pid = context.callback_pid;
        }

      if (context.callbacks != 1u ||
          context.callback_pid != service_pid ||
          context.callback_pid <= 0 ||
          context.callback_pid == caller_pid || context.callback_cpu != 0 ||
          context.interrupt_context || context.deinit_status != BK_OK ||
          rtos_is_timer_init(&context.timer) ||
          rtos_is_timer_running(&context.timer))
        {
          ret = -EIO;
          nxsem_destroy(&context.done);
          goto failed;
        }

      nxsem_destroy(&context.done);
    }

  /* Exercise the queued self-delete path once.  The callback deliberately
   * outlives several periods, so at least one watchdog expiry is pending when
   * it detaches its SDK handle.  The queued service entry must perform the
   * final free after the callback returns.
   */

  phase = "queued-self-delete";
  memset(&context, 0, sizeof(context));
  context.callback_pid = -1;
  context.callback_cpu = -1;
  context.deinit_status = BK_FAIL;
  context.callback_delay_us = BK7258_SDK_TIMER_TEST_LONG_CALLBACK_US;

  ret = nxsem_init(&context.done, 0, 0);
  if (ret < 0)
    {
      goto failed;
    }

  ret = rtos_init_timer(&context.timer,
                        BK7258_SDK_TIMER_TEST_PERIOD_MS,
                        bk7258_sdk_timer_test_callback, &context);
  if (ret != BK_OK)
    {
      ret = -EIO;
      nxsem_destroy(&context.done);
      goto failed;
    }

  ret = rtos_start_timer(&context.timer);
  if (ret != BK_OK)
    {
      (void)rtos_deinit_timer(&context.timer);
      ret = -EIO;
      nxsem_destroy(&context.done);
      goto failed;
    }

  ret = nxsem_tickwait_uninterruptible(
    &context.done, MSEC2TICK(BK7258_SDK_TIMER_TEST_TIMEOUT_MS));
  if (ret < 0)
    {
      (void)rtos_deinit_timer(&context.timer);
      nxsem_destroy(&context.done);
      goto failed;
    }

  (void)nxsig_usleep(BK7258_SDK_TIMER_TEST_LONG_SETTLE_US);

  if (context.callbacks != 1u ||
      context.callback_pid != service_pid ||
      context.callback_pid <= 0 ||
      context.callback_pid == caller_pid || context.callback_cpu != 0 ||
      context.interrupt_context || context.deinit_status != BK_OK ||
      rtos_is_timer_init(&context.timer) ||
      rtos_is_timer_running(&context.timer))
    {
      ret = -EIO;
      nxsem_destroy(&context.done);
      goto failed;
    }

  nxsem_destroy(&context.done);

  printf("BTMR PASS iterations=%lu callbacks=%lu service_pid=%d cpu=0 isr=0"
         " handle=0 long_callback_us=%u queued_delete=1\n",
         (unsigned long)iterations, (unsigned long)iterations,
         (int)service_pid, BK7258_SDK_TIMER_TEST_LONG_CALLBACK_US);
  nxmutex_unlock(&g_bk7258_sdk_timer_test_lock);
  return OK;

failed:
  printf("BTMR FAIL phase=%s iteration=%lu ret=%d callbacks=%lu callback_pid=%d"
         " cpu=%d isr=%u deinit=%d initialized=%u running=%u\n",
         phase, (unsigned long)iteration, ret,
         (unsigned long)context.callbacks, (int)context.callback_pid,
         context.callback_cpu,
         context.interrupt_context ? 1u : 0u, context.deinit_status,
         rtos_is_timer_init(&context.timer) ? 1u : 0u,
         rtos_is_timer_running(&context.timer) ? 1u : 0u);
  nxmutex_unlock(&g_bk7258_sdk_timer_test_lock);
  return ret;
}
