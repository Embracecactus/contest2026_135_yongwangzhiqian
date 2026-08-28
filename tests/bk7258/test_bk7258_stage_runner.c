/****************************************************************************
 * tests/bk7258/test_bk7258_stage_runner.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bk7258_stage_runner.h"

#define TEST_BIT(id) (UINT32_C(1) << (id))

struct test_context_s
{
  int calls[BK7258_STAGE_ID_LIMIT];
  int order[BK7258_STAGE_ID_LIMIT];
  int order_count;
  int results[BK7258_STAGE_ID_LIMIT];
  long delay_ns;
};

struct test_thread_arg_s
{
  struct bk7258_stage_runner_s *runner;
  struct test_context_s *context;
  int result;
};

static pthread_mutex_t g_mutex_control = PTHREAD_MUTEX_INITIALIZER;
static int g_mutex_failures;

int nxmutex_lock(mutex_t *mutex)
{
  int ret;

  ret = pthread_mutex_lock(&g_mutex_control);
  assert(ret == 0);
  if (g_mutex_failures > 0)
    {
      g_mutex_failures--;
      ret = pthread_mutex_unlock(&g_mutex_control);
      assert(ret == 0);
      return -EAGAIN;
    }

  ret = pthread_mutex_unlock(&g_mutex_control);
  assert(ret == 0);
  ret = pthread_mutex_lock(mutex);
  return ret == 0 ? 0 : -ret;
}

int nxmutex_unlock(mutex_t *mutex)
{
  int ret = pthread_mutex_unlock(mutex);
  return ret == 0 ? 0 : -ret;
}

static void test_mutex_fail_next(int failures)
{
  int ret = pthread_mutex_lock(&g_mutex_control);
  assert(ret == 0);
  g_mutex_failures = failures;
  ret = pthread_mutex_unlock(&g_mutex_control);
  assert(ret == 0);
}

static int test_stage(
  void *runner_context, const struct bk7258_stage_desc_s *stage)
{
  struct test_context_s *context = runner_context;

  assert(stage != NULL && stage->id < BK7258_STAGE_ID_LIMIT);

  context->calls[stage->id]++;
  context->order[context->order_count++] = stage->id;
  if (context->delay_ns > 0 && stage->id == 0)
    {
      struct timespec delay =
      {
        .tv_sec = 0,
        .tv_nsec = context->delay_ns,
      };

      assert(nanosleep(&delay, NULL) == 0);
    }

  return context->results[stage->id];
}

static void test_runner_prepare(struct bk7258_stage_runner_s *runner,
                                const struct bk7258_stage_desc_s *stages,
                                uint8_t count)
{
  memset(runner, 0, sizeof(*runner));
  assert(pthread_mutex_init(&runner->lock, NULL) == 0);
  runner->stages = stages;
  runner->execute = test_stage;
  runner->stage_count = count;
  runner->state = BK7258_PLATFORM_NEW;
  runner->first_error_stage = BK7258_STAGE_ID_INVALID;
}

static void test_runner_destroy(struct bk7258_stage_runner_s *runner)
{
  assert(pthread_mutex_destroy(&runner->lock) == 0);
}

static void test_all_success(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stages[] =
  {
    {0, 1, BK7258_STAGE_MANDATORY, 0},
    {TEST_BIT(1), 5, BK7258_STAGE_MANDATORY, 0},
    {TEST_BIT(5), 9, BK7258_STAGE_BEST_EFFORT, 0},
  };
  struct bk7258_stage_runner_s runner;
  struct bk7258_platform_status_s status;

  test_runner_prepare(&runner, stages, 3);
  assert(bk7258_stage_runner_run(&runner, &context) == 0);
  assert(context.order_count == 3);
  assert(context.order[0] == 1 && context.order[1] == 5 &&
         context.order[2] == 9);
  assert(runner.succeeded_mask ==
         (TEST_BIT(1) | TEST_BIT(5) | TEST_BIT(9)));
  assert(runner.failed_mask == 0);
  assert(runner.first_error_stage == BK7258_STAGE_ID_INVALID);
  assert(bk7258_stage_runner_snapshot(&runner, &status) == 0);
  assert(status.state == BK7258_PLATFORM_DONE);
  assert(status.attempted_mask == runner.succeeded_mask);
  assert(status.skipped_mask == 0);
  assert(status.first_error_stage == BK7258_STAGE_ID_INVALID);
  test_runner_destroy(&runner);
}

static void test_first_error_and_always_run(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stages[] =
  {
    {0, 0, BK7258_STAGE_MANDATORY, 0},
    {0, 2, BK7258_STAGE_MANDATORY, 0},
    {0, 4, BK7258_STAGE_BEST_EFFORT,
     BK7258_STAGE_FLAG_ALWAYS_RUN},
    {0, 7, BK7258_STAGE_MANDATORY,
     BK7258_STAGE_FLAG_ALWAYS_RUN},
  };
  struct bk7258_stage_runner_s runner;
  struct bk7258_platform_status_s status;

  context.results[0] = -EIO;
  context.results[4] = -ENODEV;
  context.results[7] = -ETIMEDOUT;
  test_runner_prepare(&runner, stages, 4);
  assert(bk7258_stage_runner_run(&runner, &context) == -EIO);
  assert(context.calls[0] == 1 && context.calls[2] == 0);
  assert(context.calls[4] == 1 && context.calls[7] == 1);
  assert(runner.first_error_stage == 0);
  assert(runner.failed_mask == (TEST_BIT(0) | TEST_BIT(4) | TEST_BIT(7)));
  assert(bk7258_stage_runner_snapshot(&runner, &status) == 0);
  assert(status.skipped_mask == TEST_BIT(2));
  test_runner_destroy(&runner);
}

static void test_always_run_can_supply_first_error(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stages[] =
  {
    {0, 3, BK7258_STAGE_MANDATORY, 0},
    {0, 11, BK7258_STAGE_MANDATORY,
     BK7258_STAGE_FLAG_ALWAYS_RUN},
  };
  struct bk7258_stage_runner_s runner;

  context.results[11] = -ETIMEDOUT;
  test_runner_prepare(&runner, stages, 2);
  assert(bk7258_stage_runner_run(&runner, &context) == -ETIMEDOUT);
  assert(runner.first_error_stage == 11);
  assert((runner.succeeded_mask & TEST_BIT(3)) != 0);
  assert((runner.failed_mask & TEST_BIT(11)) != 0);
  test_runner_destroy(&runner);
}

static void test_always_run_honors_prerequisites(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stages[] =
  {
    {0, 0, BK7258_STAGE_MANDATORY, 0},
    {TEST_BIT(0), 4,
     BK7258_STAGE_MANDATORY, BK7258_STAGE_FLAG_ALWAYS_RUN},
    {0, 7,
     BK7258_STAGE_MANDATORY, BK7258_STAGE_FLAG_ALWAYS_RUN},
  };
  struct bk7258_stage_runner_s runner;
  struct bk7258_platform_status_s status;

  context.results[0] = -ENODEV;
  test_runner_prepare(&runner, stages, 3);
  assert(bk7258_stage_runner_run(&runner, &context) == -ENODEV);
  assert(context.calls[0] == 1 && context.calls[4] == 0 &&
         context.calls[7] == 1);
  assert(bk7258_stage_runner_snapshot(&runner, &status) == 0);
  assert(status.skipped_mask == TEST_BIT(4));
  assert((runner.succeeded_mask & TEST_BIT(7)) != 0);
  test_runner_destroy(&runner);
}

static void test_best_effort_does_not_gate(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stages[] =
  {
    {0, 1, BK7258_STAGE_MANDATORY, 0},
    {0, 2, BK7258_STAGE_BEST_EFFORT, 0},
    {0, 3, BK7258_STAGE_MANDATORY, 0},
  };
  struct bk7258_stage_runner_s runner;

  context.results[2] = -ENODEV;
  test_runner_prepare(&runner, stages, 3);
  assert(bk7258_stage_runner_run(&runner, &context) == 0);
  assert(context.calls[3] == 1);
  assert(runner.failed_mask == TEST_BIT(2));
  assert((runner.succeeded_mask & TEST_BIT(3)) != 0);
  test_runner_destroy(&runner);
}

static void test_lock_failure_and_retry(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stage =
    {0, 1, BK7258_STAGE_MANDATORY, 0};
  struct bk7258_stage_runner_s runner;

  test_runner_prepare(&runner, &stage, 1);
  test_mutex_fail_next(1);
  assert(bk7258_stage_runner_run(&runner, &context) == -EAGAIN);
  assert(runner.state == BK7258_PLATFORM_NEW);
  assert(context.calls[1] == 0);
  assert(bk7258_stage_runner_run(&runner, &context) == 0);
  assert(context.calls[1] == 1);
  test_runner_destroy(&runner);
}

static void test_highest_stage_id(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stage =
    {0, BK7258_STAGE_ID_LIMIT - 1,
     BK7258_STAGE_MANDATORY, 0};
  struct bk7258_stage_runner_s runner;
  struct bk7258_platform_status_s status;

  test_runner_prepare(&runner, &stage, 1);
  assert(bk7258_stage_runner_run(&runner, &context) == 0);
  assert(context.calls[BK7258_STAGE_ID_LIMIT - 1] == 1);
  assert(bk7258_stage_runner_snapshot(&runner, &status) == 0);
  assert(status.attempted_mask == UINT32_C(0x80000000));
  assert(status.succeeded_mask == UINT32_C(0x80000000));
  test_runner_destroy(&runner);
}

static void test_invalid_table_is_not_executed(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stages[] =
  {
    {0, 6, BK7258_STAGE_MANDATORY, 0},
    {0, 6, BK7258_STAGE_MANDATORY, 0},
  };
  struct bk7258_stage_runner_s runner;

  test_runner_prepare(&runner, stages, 2);
  assert(bk7258_stage_runner_run(&runner, &context) == -EINVAL);
  assert(context.order_count == 0);
  assert(runner.state == BK7258_PLATFORM_NEW);
  test_runner_destroy(&runner);
}

static void test_missing_executor_is_not_executed(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stage =
    {0, 1, BK7258_STAGE_MANDATORY, 0};
  struct bk7258_stage_runner_s runner;

  test_runner_prepare(&runner, &stage, 1);
  runner.execute = NULL;
  assert(bk7258_stage_runner_run(&runner, &context) == -EINVAL);
  assert(context.order_count == 0);
  assert(runner.state == BK7258_PLATFORM_NEW);
  test_runner_destroy(&runner);
}

static void test_invalid_forward_prerequisite_is_not_executed(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stages[] =
  {
    {TEST_BIT(6), 2, BK7258_STAGE_MANDATORY, 0},
    {0, 6, BK7258_STAGE_MANDATORY, 0},
  };
  struct bk7258_stage_runner_s runner;

  test_runner_prepare(&runner, stages, 2);
  assert(bk7258_stage_runner_run(&runner, &context) == -EINVAL);
  assert(context.order_count == 0);
  assert(runner.state == BK7258_PLATFORM_NEW);
  test_runner_destroy(&runner);
}

static void test_result_and_failed_reentry(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stage =
    {0, 8, BK7258_STAGE_MANDATORY, 0};
  struct bk7258_stage_runner_s runner;

  context.results[8] = -EIO;
  test_runner_prepare(&runner, &stage, 1);
  assert(bk7258_stage_runner_result(&runner) == -EAGAIN);
  assert(bk7258_stage_runner_run(&runner, &context) == -EIO);
  assert(bk7258_stage_runner_run(&runner, NULL) == -EIO);
  assert(bk7258_stage_runner_result(&runner) == -EIO);
  assert(context.calls[8] == 1);
  test_runner_destroy(&runner);
}

static void *test_thread_run(void *arg)
{
  struct test_thread_arg_s *thread = arg;

  thread->result = bk7258_stage_runner_run(thread->runner, thread->context);
  return NULL;
}

static void test_concurrent_callers_execute_once(void)
{
  struct test_context_s context = {0};
  const struct bk7258_stage_desc_s stages[] =
  {
    {0, 0, BK7258_STAGE_MANDATORY, 0},
    {TEST_BIT(0), 1, BK7258_STAGE_MANDATORY, 0},
  };
  struct bk7258_stage_runner_s runner;
  struct test_thread_arg_s thread_args[] =
  {
    {&runner, &context, -1}, {&runner, &context, -1}
  };
  pthread_t threads[2];

  context.delay_ns = 20000000L;
  test_runner_prepare(&runner, stages, 2);
  assert(pthread_create(&threads[0], NULL, test_thread_run,
                        &thread_args[0]) == 0);
  assert(pthread_create(&threads[1], NULL, test_thread_run,
                        &thread_args[1]) == 0);
  assert(pthread_join(threads[0], NULL) == 0);
  assert(pthread_join(threads[1], NULL) == 0);
  assert(thread_args[0].result == 0 && thread_args[1].result == 0);
  assert(context.calls[0] == 1 && context.calls[1] == 1);
  test_runner_destroy(&runner);
}

int main(void)
{
  test_all_success();
  test_first_error_and_always_run();
  test_always_run_can_supply_first_error();
  test_always_run_honors_prerequisites();
  test_best_effort_does_not_gate();
  test_lock_failure_and_retry();
  test_highest_stage_id();
  test_invalid_table_is_not_executed();
  test_missing_executor_is_not_executed();
  test_invalid_forward_prerequisite_is_not_executed();
  test_result_and_failed_reentry();
  test_concurrent_callers_execute_once();
  puts("bk7258 stage runner tests: PASS");
  return 0;
}
