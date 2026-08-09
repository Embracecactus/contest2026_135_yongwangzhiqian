/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/
 * bk7258_rpmsg_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-local N9 AP-SMP RPMsg concurrency and latency test service.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_RPMSG_TEST

#include <errno.h>
#ifdef CONFIG_BK7258_AP_CORE
#  include <malloc.h>
#endif
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <driver/aon_rtc.h>
#include <soc/reg_base.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_rptun.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_RPMSG_TEST_EPT_NAME       "bk7258-smp-test"
#define BK7258_RPMSG_TEST_WIRE_MAGIC     0x54505242u /* "BRPT" */
#define BK7258_RPMSG_TEST_WIRE_VERSION   1u
#define BK7258_RPMSG_TEST_SEND_TIMEOUT   100u
#define BK7258_RPMSG_TEST_REPLY_TIMEOUT  1000u
#define BK7258_RPMSG_TEST_REPORT_MARGIN  2000u
#define BK7258_RPMSG_TEST_CLEANUP_TIMEOUT 1000u
#define BK7258_RPMSG_TEST_MIN_TIMEOUT    1000u
#define BK7258_RPMSG_TEST_MAX_TIMEOUT    60000u
#define BK7258_RPMSG_TEST_CONTROLLER_PRIO 110
#define BK7258_RPMSG_TEST_WORKER_PRIO    120
#define BK7258_RPMSG_TEST_LOAD_PRIO      80
#define BK7258_RPMSG_TEST_TX_PRIO        190
#define BK7258_RPMSG_TEST_TX_POLL_MS     1u
#define BK7258_RPMSG_TEST_DONE_POLL_MS   1u
#define BK7258_RPMSG_TEST_THREAD_STACK   4096
#define BK7258_RPMSG_TEST_AON_COUNT_LOW  (SOC_AON_RTC_REG_BASE + 3u * 4u)

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_RPMSG_TEST_REMOTE_NAME  "cp"
#else
#  define BK7258_RPMSG_TEST_REMOTE_NAME  "ap"
#endif

enum bk7258_rpmsg_test_command_e
{
  BK7258_RPMSG_TEST_COMMAND_START = 1,
  BK7258_RPMSG_TEST_COMMAND_ECHO,
  BK7258_RPMSG_TEST_COMMAND_ECHO_RESPONSE,
  BK7258_RPMSG_TEST_COMMAND_REPORT
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_rpmsg_test_wire_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t generation;
  uint32_t run_id;
  uint32_t sequence;
  uint32_t slot;
  uint32_t payload_size;
  uint32_t value;
  uint8_t data[BK7258_RPMSG_TEST_MAX_PAYLOAD];
};

#ifdef CONFIG_BK7258_AP_CORE
struct bk7258_rpmsg_test_tx_request_s
{
  uint8_t data[BK7258_RPMSG_TEST_FRAME_SIZE];
  size_t len;
  uint32_t timeout_ms;
  volatile int status;
};
#endif

struct bk7258_rpmsg_test_dev_s
{
  struct rpmsg_endpoint ept;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile int connection_error;

#ifdef CONFIG_BK7258_AP_CORE
  sem_t start_sem;
  sem_t done_sem;
  sem_t worker_start_sem[2];
  sem_t reply_sem[2];
  sem_t load_start_sem;
  sem_t load_done_sem;
  sem_t tx_sem;
  sem_t tx_done_sem[2];
  volatile uint32_t tx_pending;
  volatile uint32_t worker_dispatch[2];
  struct bk7258_rpmsg_test_tx_request_s tx_request[2];
  volatile bool busy;
  volatile bool abort;
  volatile bool load_stop;
  volatile uint32_t generation;
  volatile uint32_t run_id;
  volatile uint32_t count;
  volatile uint32_t payload_size;
  volatile uint32_t flags;
  volatile uint32_t timeout_ms;
  volatile uint32_t awaiting[2];
  volatile int reply_status[2];
  volatile int worker_status[2];
  volatile uint32_t workers_done;
  uint32_t latency[2][BK7258_RPMSG_TEST_MAX_COUNT];
  struct bk7258_rpmsg_test_result_s result;
#else
  sem_t report_sem;
  volatile uint32_t waiting_generation;
  volatile uint32_t waiting_run_id;
  volatile bool report_valid;
  struct bk7258_rpmsg_test_result_s report;
#endif
};

/****************************************************************************
 * Compile-time ABI Gates
 ****************************************************************************/

static_assert(offsetof(struct bk7258_rpmsg_test_wire_s, data) ==
              BK7258_RPMSG_TEST_WIRE_HEADER_SIZE,
              "BK7258 RPMsg test wire header changed");
static_assert(sizeof(struct bk7258_rpmsg_test_wire_s) ==
              BK7258_RPMSG_TEST_FRAME_SIZE,
              "BK7258 RPMsg test wire frame changed");

#if defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_BK7258_AP_SUPERVISOR)
/* The test gateway must not suppress the N10 primary management heartbeat.
 * Transport workers still own priorities 224/225, followed by the N10
 * heartbeat at its default 200, then this board-only traffic generator.
 */

static_assert(BK7258_RPMSG_TEST_TX_PRIO <
              CONFIG_BK7258_AP_SUPERVISOR_PRIORITY,
              "RPMsg test TX must remain below the AP heartbeat");
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_rpmsg_test_dev_s g_bk7258_rpmsg_test;

#ifndef CONFIG_BK7258_AP_CORE
static mutex_t g_bk7258_rpmsg_test_lock = NXMUTEX_INITIALIZER;
static uint32_t g_bk7258_rpmsg_test_next_run;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_rpmsg_test_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static int bk7258_rpmsg_test_sem_init(sem_t *sem)
{
  int ret;

  ret = nxsem_init(sem, 0, 0);
#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(sem, SEM_PRIO_NONE);
    }
#endif

  return ret;
}

static bool bk7258_rpmsg_test_endpoint_ready(void)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  return __atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) &&
         control->magic == BK7258_RPTUN_CONTROL_MAGIC &&
         control->version == BK7258_RPTUN_CONTROL_VERSION &&
         control->size == sizeof(*control) &&
         control->generation != 0 &&
         control->state == BK7258_RPTUN_STATE_CONNECTED &&
         is_rpmsg_ept_ready(&priv->ept);
}

static uint32_t bk7258_rpmsg_test_aon_tick(void)
{
  /* The SDK's 64-bit getter loops until two low/high register pairs match.
   * CPU2 can observe a new 32 kHz low word between every pair and remain in
   * that loop.  A single low-word sample is sufficient for the bounded N9
   * test window (at most 60 seconds, versus roughly 37 hours to wrap).
   */

  return *(volatile uint32_t *)(uintptr_t)
           BK7258_RPMSG_TEST_AON_COUNT_LOW;
}

static int bk7258_rpmsg_test_send_bounded(const void *data, size_t len,
                                           uint32_t timeout_ms)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  uint32_t frequency = bk_rtc_get_clock_freq();
  uint32_t start = bk7258_rpmsg_test_aon_tick();
  uint32_t timeout_ticks =
    (uint32_t)(((uint64_t)timeout_ms * frequency + 999u) / 1000u);
  int ret = -ENOTCONN;

  do
    {
      if (!bk7258_rpmsg_test_endpoint_ready())
        {
          return -ENOTCONN;
        }

      /* OpenAMP serializes TX buffer allocation and virtqueue enqueue with
       * the rpmsg_device lock.  AP producers reach this call only through
       * the CPU0 TX gateway; direct AP calls are likewise CPU0-only.  Do not
       * add another board lock here because it would nest around OpenAMP's
       * scheduler-aware lock.
       */

      ret = rpmsg_trysend(&priv->ept, data, len);
      if (ret >= 0)
        {
          return OK;
        }

      /* A transient lack of a TX buffer is normal with two concurrent
       * maximum-size senders.  CPU2 has no local SysTick, so a signal sleep
       * here can become unbounded.  Keep the retry AON-bounded and use a
       * short architecture delay; the higher-priority RPMsg/mailbox workers
       * can still preempt this test task and return buffers.
       */

      up_udelay(50);
    }
  while ((uint32_t)(bk7258_rpmsg_test_aon_tick() - start) < timeout_ticks);

  return ret < 0 ? ret : -ETIMEDOUT;
}

#ifdef CONFIG_BK7258_AP_CORE
static int bk7258_rpmsg_test_submit(uint32_t slot, FAR const void *data,
                                     size_t len, uint32_t timeout_ms)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  struct bk7258_rpmsg_test_tx_request_s *request;
  uint32_t frequency;
  uint32_t start;
  uint32_t timeout_ticks;
  int status;
  int ret;

  if (slot >= 2 || data == NULL || len > BK7258_RPMSG_TEST_FRAME_SIZE)
    {
      return -EINVAL;
    }

  request = &priv->tx_request[slot];
  bk7258_rpmsg_test_flush_sem(&priv->tx_done_sem[slot]);
  memcpy(request->data, data, len);
  request->len = len;
  request->timeout_ms = timeout_ms;
  __atomic_store_n(&request->status, -EINPROGRESS, __ATOMIC_RELEASE);
  __asm volatile ("dmb sy" ::: "memory");
  __atomic_fetch_or(&priv->tx_pending, 1u << slot, __ATOMIC_RELEASE);

  /* The current AP SMP bootstrap does not give CPU2 an independent system
   * tick.  More importantly, a CPU2 -> CPU0 semaphore wake can remain
   * pending until some other CPU0 scheduling event.  Keep the fast event
   * wake for CPU0 producers and let the gateway's CPU0 timed level poll
   * discover CPU2 requests.  The request bit is the source of truth; the
   * semaphore is only an acceleration hint.
   */

  if (up_cpu_index() == 0)
    {
      (void)nxsem_post(&priv->tx_sem);
      ret = nxsem_wait_uninterruptible(&priv->tx_done_sem[slot]);
      if (ret < 0)
        {
          return ret;
        }

      __asm volatile ("dmb sy" ::: "memory");
      return __atomic_load_n(&request->status, __ATOMIC_ACQUIRE);
    }

  /* CPU2 has no local SysTick and its current board bootstrap cannot make
   * repeated cross-core semaphore wakeups a reliable transport primitive.
   * The request payload lives in the fixed gateway slot above, so CPU2 can
   * wait on the release/acquire status without exposing a stack pointer to a
   * late gateway.  The shared AON RTC supplies the timeout boundary without
   * entering the SMP scheduler on every RPMsg frame.
   */

  frequency = bk_rtc_get_clock_freq();
  start = bk7258_rpmsg_test_aon_tick();
  timeout_ticks =
    (uint32_t)(((uint64_t)timeout_ms * frequency + 999u) / 1000u);

  do
    {
      status = __atomic_load_n(&request->status, __ATOMIC_ACQUIRE);
      if (status != -EINPROGRESS)
        {
          return status;
        }

      if (__atomic_load_n(&priv->abort, __ATOMIC_ACQUIRE))
        {
          return -ECANCELED;
        }

      up_udelay(50);
    }
  while ((uint32_t)(bk7258_rpmsg_test_aon_tick() - start) < timeout_ticks);

  status = -EINPROGRESS;
  (void)__atomic_compare_exchange_n(&request->status, &status,
                                    -ETIMEDOUT, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  return __atomic_load_n(&request->status, __ATOMIC_ACQUIRE);
}

static FAR void *bk7258_rpmsg_test_tx_gateway(FAR void *arg)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  uint32_t pending;
  uint32_t slot;
  int ret;

  (void)arg;
  for (; ; )
    {
      pending = __atomic_exchange_n(&priv->tx_pending, 0,
                                    __ATOMIC_ACQ_REL);
      if (pending == 0)
        {
          /* This thread is pinned to logical CPU0, whose system tick is the
           * supported timeout owner.  Polling the level bit also closes the
           * CPU2 -> CPU0 missed-edge window without entering OpenAMP from
           * CPU2 or modifying NuttX semaphore/IPI code.
           */

          (void)nxsem_tickwait_uninterruptible(
                  &priv->tx_sem,
                  MSEC2TICK(BK7258_RPMSG_TEST_TX_POLL_MS));
          continue;
        }

      for (slot = 0; slot < 2; slot++)
        {
          struct bk7258_rpmsg_test_tx_request_s *request;

          if ((pending & (1u << slot)) == 0)
            {
              continue;
            }

          request = &priv->tx_request[slot];
          if (__atomic_load_n(&request->status, __ATOMIC_ACQUIRE) ==
              -EINPROGRESS)
            {
              ret = bk7258_rpmsg_test_send_bounded(
                      request->data, request->len,
                      request->timeout_ms);
              __atomic_store_n(&request->status, ret, __ATOMIC_RELEASE);
            }

          __asm volatile ("dmb sy" ::: "memory");
          if (slot == 0)
            {
              (void)nxsem_post(&priv->tx_done_sem[slot]);
            }
          else
            {
              __asm volatile ("sev" ::: "memory");
            }
        }
    }

  return NULL;
}
#endif

static bool bk7258_rpmsg_test_wire_valid(
  const struct bk7258_rpmsg_test_wire_s *msg, size_t len)
{
  return msg != NULL && len >= BK7258_RPMSG_TEST_WIRE_HEADER_SIZE &&
         len <= BK7258_RPMSG_TEST_FRAME_SIZE &&
         msg->magic == BK7258_RPMSG_TEST_WIRE_MAGIC &&
         msg->version == BK7258_RPMSG_TEST_WIRE_VERSION;
}

static bool bk7258_rpmsg_test_payload_valid(
  const struct bk7258_rpmsg_test_wire_s *msg, size_t len)
{
  uint32_t i;

  if (msg->payload_size == 0 ||
      msg->payload_size > BK7258_RPMSG_TEST_MAX_PAYLOAD ||
      len != BK7258_RPMSG_TEST_WIRE_HEADER_SIZE + msg->payload_size)
    {
      return false;
    }

  for (i = 0; i < msg->payload_size; i++)
    {
      if (msg->data[i] !=
          (uint8_t)(msg->sequence + msg->slot + i))
        {
          return false;
        }
    }

  return true;
}

#ifdef CONFIG_BK7258_AP_CORE

static int bk7258_rpmsg_test_u32_compare(const void *lhs, const void *rhs)
{
  uint32_t a = *(const uint32_t *)lhs;
  uint32_t b = *(const uint32_t *)rhs;

  return a < b ? -1 : a > b;
}

static uint32_t bk7258_rpmsg_test_ticks_to_cycles(uint32_t elapsed_ticks,
                                                  uint32_t frequency)
{
  uint64_t cycles = (uint64_t)elapsed_ticks * frequency /
                    bk_rtc_get_clock_freq();

  return cycles > UINT32_MAX ? UINT32_MAX : (uint32_t)cycles;
}

static int bk7258_rpmsg_test_wait_reply(uint32_t slot,
                                        uint32_t timeout_ms)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  uint32_t frequency;
  uint32_t start;
  uint32_t timeout_ticks;

  if (up_cpu_index() == 0)
    {
      return nxsem_tickwait_uninterruptible(&priv->reply_sem[slot],
                                             MSEC2TICK(timeout_ms));
    }

  /* Avoid a scheduler-mediated CPU0 -> CPU2 semaphore wake for every echo.
   * The callback publishes reply_status with release ordering and the AON
   * RTC gives CPU2 a real timeout even though it has no local SysTick.
   */

  frequency = bk_rtc_get_clock_freq();
  start = bk7258_rpmsg_test_aon_tick();
  timeout_ticks =
    (uint32_t)(((uint64_t)timeout_ms * frequency + 999u) / 1000u);

  do
    {
      if (__atomic_load_n(&priv->reply_status[slot], __ATOMIC_ACQUIRE) !=
          -EINPROGRESS)
        {
          return OK;
        }

      if (__atomic_load_n(&priv->abort, __ATOMIC_ACQUIRE))
        {
          return -ECANCELED;
        }

      if (!bk7258_rpmsg_test_endpoint_ready())
        {
          return -ENOTCONN;
        }

      up_udelay(50);
    }
  while ((uint32_t)(bk7258_rpmsg_test_aon_tick() - start) < timeout_ticks);

  return -ETIMEDOUT;
}

static void bk7258_rpmsg_test_finalize_cpu(uint32_t slot)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  struct bk7258_rpmsg_test_cpu_result_s *cpu = &priv->result.cpu[slot];
  uint32_t count = cpu->received;

  if (count == 0)
    {
      return;
    }

  qsort(priv->latency[slot], count, sizeof(uint32_t),
        bk7258_rpmsg_test_u32_compare);
  cpu->min_cycles = priv->latency[slot][0];
  cpu->p50_cycles = priv->latency[slot][(count * 50u + 99u) / 100u - 1u];
  cpu->p95_cycles = priv->latency[slot][(count * 95u + 99u) / 100u - 1u];
  cpu->p99_cycles = priv->latency[slot][(count * 99u + 99u) / 100u - 1u];
  cpu->max_cycles = priv->latency[slot][count - 1u];
}

static int bk7258_rpmsg_test_attr_initialize(pthread_attr_t *attr,
                                              uint32_t cpu,
                                              int priority)
{
  struct sched_param param;
  cpu_set_t cpuset = (cpu_set_t)(1u << cpu);
  int initialized = 0;
  int ret;

  ret = pthread_attr_init(attr);
  if (ret == 0)
    {
      initialized = 1;
      ret = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(attr, BK7258_RPMSG_TEST_THREAD_STACK);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(attr, sizeof(cpuset), &cpuset);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = priority;
      ret = pthread_attr_setschedparam(attr, &param);
    }

  if (ret != 0 && initialized)
    {
      (void)pthread_attr_destroy(attr);
    }

  return ret;
}

static void bk7258_rpmsg_test_run_worker(uint32_t slot)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  struct bk7258_rpmsg_test_cpu_result_s *cpu;
  struct bk7258_rpmsg_test_wire_s msg;
  uint32_t reply_timeout;
  uint32_t sequence;
  uint32_t elapsed_cycles;
  uint32_t start;
  uint32_t end;
  uint32_t i;
  int ret = OK;

  cpu = &priv->result.cpu[slot];
  cpu->sender_cpu = (uint32_t)up_cpu_index();
  if (cpu->sender_cpu != slot)
    {
      ret = -EXDEV;
      cpu->errors++;
      goto done;
    }

  reply_timeout = priv->timeout_ms < BK7258_RPMSG_TEST_REPLY_TIMEOUT ?
                  priv->timeout_ms : BK7258_RPMSG_TEST_REPLY_TIMEOUT;

  for (sequence = 1; sequence <= priv->count; sequence++)
    {
      if (__atomic_load_n(&priv->abort, __ATOMIC_ACQUIRE))
        {
          ret = -ECANCELED;
          break;
        }

      memset(&msg, 0, BK7258_RPMSG_TEST_WIRE_HEADER_SIZE);
      msg.magic = BK7258_RPMSG_TEST_WIRE_MAGIC;
      msg.version = BK7258_RPMSG_TEST_WIRE_VERSION;
      msg.command = BK7258_RPMSG_TEST_COMMAND_ECHO;
      msg.generation = priv->generation;
      msg.run_id = priv->run_id;
      msg.sequence = sequence;
      msg.slot = slot;
      msg.payload_size = priv->payload_size;
      for (i = 0; i < msg.payload_size; i++)
        {
          msg.data[i] = (uint8_t)(sequence + slot + i);
        }

      __atomic_store_n(&priv->reply_status[slot], -EINPROGRESS,
                       __ATOMIC_RELEASE);
      priv->awaiting[slot] = sequence;
      __asm volatile ("dmb sy" ::: "memory");
      /* DWT_CYCCNT is banked per physical Cortex-M33 and CPU2 does not run
       * the CPU0 timer initialization path.  NuttX's system-tick seqlock also
       * cannot be used safely from this board's CPU2 bootstrap path.  Follow
       * the official BK7258 SMP SDK interrupt recorder and use the shared AON
       * RTC, then express the measured microseconds in AP clock cycles.
       */

      start = bk7258_rpmsg_test_aon_tick();
      ret = bk7258_rpmsg_test_submit(
              slot, &msg,
              BK7258_RPMSG_TEST_WIRE_HEADER_SIZE + msg.payload_size,
              BK7258_RPMSG_TEST_SEND_TIMEOUT);
      if (ret < 0)
        {
          cpu->errors++;
          __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
          break;
        }

      cpu->sent++;
      ret = bk7258_rpmsg_test_wait_reply(slot, reply_timeout);
      end = bk7258_rpmsg_test_aon_tick();
      if (ret < 0)
        {
          cpu->errors++;
          __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
          break;
        }

      __asm volatile ("dmb sy" ::: "memory");
      ret = priv->reply_status[slot];
      if (ret < 0 || !bk7258_rpmsg_test_endpoint_ready())
        {
          if (ret >= 0)
            {
              ret = -ENOTCONN;
            }

          cpu->errors++;
          __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
          break;
        }

      elapsed_cycles = bk7258_rpmsg_test_ticks_to_cycles(
                         end - start, priv->result.frequency);
      priv->latency[slot][cpu->received] = elapsed_cycles;
      cpu->total_cycles += elapsed_cycles;
      cpu->received++;
    }

done:
  priv->worker_status[slot] = ret;
  __atomic_fetch_add(&priv->workers_done, 1u, __ATOMIC_RELEASE);

  /* As with the TX request path, use the atomic count as the cross-core
   * level state.  A CPU2 -> CPU0 semaphore post can lose its scheduling edge
   * in the current AP bootstrap, so only the CPU0 worker supplies the fast
   * hint.  The CPU0 controller polls workers_done with a bounded tick wait.
   */

  if (up_cpu_index() == 0)
    {
      (void)nxsem_post(&priv->done_sem);
    }
}

static FAR void *bk7258_rpmsg_test_worker(FAR void *arg)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  uint32_t slot = (uint32_t)(uintptr_t)arg;
  uint32_t dispatch = 0;
  int ret;

  /* A detached pthread that exits on AP logical CPU1 leaves its stack/TCB
   * allocation pending in this board's current SMP lifecycle path.  The
   * stress test used to create two fresh workers per request and therefore
   * consumed one 4360-byte allocation on every run until pthread_create()
   * returned ENOMEM.  Keep one pinned worker per logical CPU for the AP
   * lifetime and dispatch requests with counting semaphores instead.  This
   * stays entirely in the board wrapper and does not alter NuttX or the SDK.
   */

  for (; ; )
    {
      if (slot == 0)
        {
          ret = nxsem_wait_uninterruptible(&priv->worker_start_sem[slot]);
          if (ret < 0)
            {
              continue;
            }
        }
      else
        {
          /* Keep CPU2 out of the fragile cross-core semaphore wake path.
           * The controller publishes all request fields before incrementing
           * this sequence and executes SEV.  WFE is only an acceleration;
           * the AP heartbeat also emits SEV periodically, so a missed event
           * cannot hide a changed level value.
           */

          while (__atomic_load_n(&priv->worker_dispatch[slot],
                                  __ATOMIC_ACQUIRE) == dispatch)
            {
              __asm volatile ("wfe" ::: "memory");
            }

          dispatch = __atomic_load_n(&priv->worker_dispatch[slot],
                                     __ATOMIC_ACQUIRE);
        }

      __asm volatile ("dmb sy" ::: "memory");
      bk7258_rpmsg_test_run_worker(slot);
    }

  return NULL;
}

static FAR void *bk7258_rpmsg_test_load_worker(FAR void *arg)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  volatile uint32_t value = 0;
  uint32_t i;
  int ret;

  (void)arg;
  for (; ; )
    {
      ret = nxsem_wait_uninterruptible(&priv->load_start_sem);
      if (ret < 0)
        {
          continue;
        }

      while (!__atomic_load_n(&priv->load_stop, __ATOMIC_ACQUIRE))
        {
          for (i = 0; i < 4096; i++)
            {
              value = value * 1664525u + 1013904223u;
            }

          /* This task is pinned to CPU0 below every transport, health and
           * test-control priority.  Preemption therefore gives those tasks
           * bounded progress while this loop consumes otherwise idle CPU0
           * cycles.  sched_yield() would only rotate equal-priority tasks;
           * with no such peer it repeatedly enters NuttX's global SMP
           * scheduler without yielding useful work and can turn this load
           * generator into a scheduler/IPI storm.
           */
        }

      (void)nxsem_post(&priv->load_done_sem);
    }

  (void)value;
  return NULL;
}

static void bk7258_rpmsg_test_heap_snapshot(
  struct bk7258_rpmsg_test_heap_result_s *snapshot)
{
  struct mallinfo info = mallinfo();

  snapshot->arena = info.arena;
  snapshot->allocated_blocks = info.aordblks;
  snapshot->free_blocks = info.ordblks;
  snapshot->largest_free = info.mxordblk;
  snapshot->allocated_bytes = info.uordblks;
  snapshot->free_bytes = info.fordblks;
}

static int bk7258_rpmsg_test_spawn(pthread_t *thread, uint32_t cpu,
                                    int priority,
                                    pthread_startroutine_t entry,
                                    FAR void *arg,
                                    uint32_t *failure_stage)
{
  pthread_attr_t attr;
  int ret;

  *failure_stage = BK7258_RPMSG_TEST_SPAWN_STAGE_ATTR;
  ret = bk7258_rpmsg_test_attr_initialize(&attr, cpu, priority);
  if (ret != 0)
    {
      return -ret;
    }

  *failure_stage = BK7258_RPMSG_TEST_SPAWN_STAGE_CREATE;
  ret = pthread_create(thread, &attr, entry, arg);
  (void)pthread_attr_destroy(&attr);
  if (ret == 0)
    {
      *failure_stage = BK7258_RPMSG_TEST_SPAWN_STAGE_NONE;
    }

  return ret == 0 ? OK : -ret;
}

static void bk7258_rpmsg_test_prepare_result(void)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;

  memset(&priv->result, 0, sizeof(priv->result));
  priv->result.magic = BK7258_RPMSG_TEST_RESULT_MAGIC;
  priv->result.version = BK7258_RPMSG_TEST_RESULT_VERSION;
  priv->result.size = sizeof(priv->result);
  priv->result.generation = priv->generation;
  priv->result.run_id = priv->run_id;
  priv->result.count = priv->count;
  priv->result.payload_size = priv->payload_size;
  priv->result.frame_size = BK7258_RPMSG_TEST_WIRE_HEADER_SIZE +
                            priv->payload_size;
  priv->result.flags = priv->flags;
  priv->result.frequency = up_perf_getfreq();
  priv->result.controller_cpu = (uint32_t)up_cpu_index();
  bk7258_rpmsg_test_heap_snapshot(&priv->result.heap_start);
}

static int bk7258_rpmsg_test_send_report(int status, bool release)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  struct bk7258_rpmsg_test_wire_s msg;

  priv->result.status = status;
  memset(&msg, 0, BK7258_RPMSG_TEST_WIRE_HEADER_SIZE);
  msg.magic = BK7258_RPMSG_TEST_WIRE_MAGIC;
  msg.version = BK7258_RPMSG_TEST_WIRE_VERSION;
  msg.command = BK7258_RPMSG_TEST_COMMAND_REPORT;
  msg.generation = priv->generation;
  msg.run_id = priv->run_id;
  msg.value = (uint32_t)status;
  memcpy(msg.data, &priv->result, sizeof(priv->result));

  /* The CP can submit the next run as soon as it receives this immutable
   * local report.  Release busy before publishing the report so that the
   * next START cannot race with a trailing busy clear.  The controller will
   * consume the already-posted start semaphore after this send returns.
   */

  if (release)
    {
      __atomic_store_n(&priv->busy, false, __ATOMIC_RELEASE);
    }

  return bk7258_rpmsg_test_send_bounded(
           &msg, BK7258_RPMSG_TEST_WIRE_HEADER_SIZE + sizeof(priv->result),
           BK7258_RPMSG_TEST_SEND_TIMEOUT);
}

static int bk7258_rpmsg_test_controller(int argc, FAR char *argv[])
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  uint32_t expected;
  uint32_t i;
  clock_t wait_start;
  clock_t wait_limit;
  clock_t wait_slice;
  clock_t elapsed;
  int load_started;
  int status;
  int ret;

  (void)argc;
  (void)argv;

  for (; ; )
    {
      ret = nxsem_wait_uninterruptible(&priv->start_sem);
      if (ret < 0 || !__atomic_load_n(&priv->busy, __ATOMIC_ACQUIRE))
        {
          continue;
        }

      bk7258_rpmsg_test_flush_sem(&priv->done_sem);
      bk7258_rpmsg_test_flush_sem(&priv->load_done_sem);
      for (i = 0; i < 2; i++)
        {
          bk7258_rpmsg_test_flush_sem(&priv->reply_sem[i]);
          priv->awaiting[i] = 0;
          priv->reply_status[i] = -EINPROGRESS;
          priv->worker_status[i] = -EINPROGRESS;
          memset(priv->latency[i], 0, sizeof(priv->latency[i]));
        }

      priv->workers_done = 0;
      __atomic_store_n(&priv->abort, false, __ATOMIC_RELEASE);
      __atomic_store_n(&priv->load_stop, false, __ATOMIC_RELEASE);
      bk7258_rpmsg_test_prepare_result();
      __asm volatile ("dmb sy" ::: "memory");

      load_started = 0;
      status = OK;
      if ((priv->flags & BK7258_RPMSG_TEST_FLAG_CPU0_LOAD) != 0)
        {
          priv->result.spawn_target =
            BK7258_RPMSG_TEST_SPAWN_TARGET_LOAD;
          priv->result.spawn_stage =
            BK7258_RPMSG_TEST_SPAWN_STAGE_DISPATCH;
          ret = nxsem_post(&priv->load_start_sem);
          if (ret < 0)
            {
              status = ret;
              priv->result.spawn_status = ret;
            }
          else
            {
              load_started = 1;
              priv->result.spawn_target =
                BK7258_RPMSG_TEST_SPAWN_TARGET_NONE;
              priv->result.spawn_stage =
                BK7258_RPMSG_TEST_SPAWN_STAGE_NONE;
            }
        }

      expected = 0;
      for (i = 0; i < 2 && status >= 0; i++)
        {
          priv->result.spawn_target =
            i == 0 ? BK7258_RPMSG_TEST_SPAWN_TARGET_CPU0 :
                     BK7258_RPMSG_TEST_SPAWN_TARGET_CPU1;
          priv->result.spawn_stage =
            BK7258_RPMSG_TEST_SPAWN_STAGE_DISPATCH;
          if (i == 1)
            {
              __atomic_fetch_add(&priv->worker_dispatch[i], 1u,
                                 __ATOMIC_RELEASE);
              __asm volatile ("dmb sy; sev" ::: "memory");
              ret = OK;
            }
          else
            {
              ret = nxsem_post(&priv->worker_start_sem[i]);
            }

          if (ret < 0)
            {
              status = ret;
              priv->result.spawn_status = ret;
              __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
              break;
            }

          expected++;
          priv->result.spawn_target =
            BK7258_RPMSG_TEST_SPAWN_TARGET_NONE;
          priv->result.spawn_stage =
            BK7258_RPMSG_TEST_SPAWN_STAGE_NONE;
        }

      priv->result.workers_expected = expected;
      bk7258_rpmsg_test_heap_snapshot(&priv->result.heap_after_spawn);

      wait_start = clock_systime_ticks();
      wait_limit = MSEC2TICK(
                     priv->timeout_ms > BK7258_RPMSG_TEST_REPORT_MARGIN ?
                     priv->timeout_ms - BK7258_RPMSG_TEST_REPORT_MARGIN :
                     priv->timeout_ms / 2u);
      wait_slice = MSEC2TICK(BK7258_RPMSG_TEST_DONE_POLL_MS);
      if (wait_slice < 1)
        {
          wait_slice = 1;
        }

      while (__atomic_load_n(&priv->workers_done,
                             __ATOMIC_ACQUIRE) < expected)
        {
          elapsed = clock_systime_ticks() - wait_start;
          if (elapsed >= wait_limit)
            {
              if (status >= 0)
                {
                  status = -ETIMEDOUT;
                }

              __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
              break;
            }

          (void)nxsem_tickwait_uninterruptible(
                  &priv->done_sem,
                  wait_slice < wait_limit - elapsed ?
                  wait_slice : wait_limit - elapsed);
        }

      if (__atomic_load_n(&priv->workers_done, __ATOMIC_ACQUIRE) < expected)
        {
          clock_t cleanup_start = clock_systime_ticks();
          clock_t cleanup_limit =
            MSEC2TICK(BK7258_RPMSG_TEST_CLEANUP_TIMEOUT);

          __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
          (void)nxsem_post(&priv->reply_sem[0]);
          __asm volatile ("dmb sy; sev" ::: "memory");

          while (__atomic_load_n(&priv->workers_done,
                                 __ATOMIC_ACQUIRE) < expected)
            {
              elapsed = clock_systime_ticks() - cleanup_start;
              if (elapsed >= cleanup_limit)
                {
                  if (status >= 0)
                    {
                      status = -ETIMEDOUT;
                    }

                  break;
                }

              (void)nxsem_tickwait_uninterruptible(
                      &priv->done_sem,
                      wait_slice < cleanup_limit - elapsed ?
                      wait_slice : cleanup_limit - elapsed);
            }
        }

      __atomic_store_n(&priv->load_stop, true, __ATOMIC_RELEASE);
      if (load_started)
        {
          ret = nxsem_tickwait_uninterruptible(
                  &priv->load_done_sem, MSEC2TICK(1000));
          if (ret < 0 && status >= 0)
            {
              status = ret;
            }
        }

      priv->result.workers_done =
        __atomic_load_n(&priv->workers_done, __ATOMIC_ACQUIRE);
      bk7258_rpmsg_test_heap_snapshot(&priv->result.heap_report);

      for (i = 0; i < 2; i++)
        {
          bk7258_rpmsg_test_finalize_cpu(i);
          if (status >= 0 && priv->worker_status[i] < 0)
            {
              status = priv->worker_status[i];
            }

          if (status >= 0 &&
              (priv->result.cpu[i].sent != priv->count ||
               priv->result.cpu[i].received != priv->count ||
               priv->result.cpu[i].errors != 0 ||
               priv->result.cpu[i].sender_cpu != i ||
               priv->result.cpu[i].callback_cpu_mask != 1u ||
               priv->result.cpu[i].min_cycles == 0 ||
               priv->result.cpu[i].max_cycles == 0))
            {
              status = -EIO;
            }
        }

      ret = bk7258_rpmsg_test_send_report(
              status,
              __atomic_load_n(&priv->workers_done,
                              __ATOMIC_ACQUIRE) == expected);

      /* The stock syslog_rpmsg endpoint is established asynchronously by
       * Name Service.  Emit the N9-F probe only after a complete request and
       * report exchange proves that the link is connected.  Keeping this
       * behind an explicit flag also prevents syslog traffic from disturbing
       * the latency and throughput matrix.
       */

      if (ret >= 0 && status >= 0 &&
          (priv->flags & BK7258_RPMSG_TEST_FLAG_SYSLOG_PROBE) != 0)
        {
          syslog(LOG_ERR,
                 "BK7258 AP RPMsg syslog probe gen=%lu run=%lu\n",
                 (unsigned long)priv->generation,
                 (unsigned long)priv->run_id);
        }
    }

  return OK;
}

static void bk7258_rpmsg_test_send_error_report(
  const struct bk7258_rpmsg_test_wire_s *request, int status)
{
  struct bk7258_rpmsg_test_result_s result;
  struct bk7258_rpmsg_test_wire_s msg;

  memset(&result, 0, sizeof(result));
  result.magic = BK7258_RPMSG_TEST_RESULT_MAGIC;
  result.version = BK7258_RPMSG_TEST_RESULT_VERSION;
  result.size = sizeof(result);
  result.generation = request->generation;
  result.run_id = request->run_id;
  result.count = request->sequence;
  result.payload_size = request->payload_size;
  result.flags = request->slot;
  result.status = status;

  memset(&msg, 0, BK7258_RPMSG_TEST_WIRE_HEADER_SIZE);
  msg.magic = BK7258_RPMSG_TEST_WIRE_MAGIC;
  msg.version = BK7258_RPMSG_TEST_WIRE_VERSION;
  msg.command = BK7258_RPMSG_TEST_COMMAND_REPORT;
  msg.generation = request->generation;
  msg.run_id = request->run_id;
  msg.value = (uint32_t)status;
  memcpy(msg.data, &result, sizeof(result));
  (void)bk7258_rpmsg_test_send_bounded(
          &msg, BK7258_RPMSG_TEST_WIRE_HEADER_SIZE + sizeof(result),
          BK7258_RPMSG_TEST_SEND_TIMEOUT);
}

#endif /* CONFIG_BK7258_AP_CORE */

static int bk7258_rpmsg_test_ept_cb(FAR struct rpmsg_endpoint *ept,
                                     FAR void *data, size_t len,
                                     uint32_t src, FAR void *priv_)
{
  struct bk7258_rpmsg_test_dev_s *priv = priv_;
  struct bk7258_rpmsg_test_wire_s *msg = data;

  (void)ept;
  (void)src;
  if (!bk7258_rpmsg_test_wire_valid(msg, len))
    {
      return -EINVAL;
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (msg->command == BK7258_RPMSG_TEST_COMMAND_START)
    {
      volatile struct bk7258_rptun_control_s *control =
        bk7258_rptun_control();
      bool expected = false;

      if (msg->generation != control->generation || msg->sequence == 0 ||
          msg->sequence > BK7258_RPMSG_TEST_MAX_COUNT ||
          msg->payload_size == 0 ||
          msg->payload_size > BK7258_RPMSG_TEST_MAX_PAYLOAD ||
          msg->value < BK7258_RPMSG_TEST_MIN_TIMEOUT ||
          msg->value > BK7258_RPMSG_TEST_MAX_TIMEOUT ||
          (msg->slot & ~BK7258_RPMSG_TEST_VALID_FLAGS) != 0)
        {
          bk7258_rpmsg_test_send_error_report(msg, -EINVAL);
          return -EINVAL;
        }

      if (!__atomic_compare_exchange_n(&priv->busy, &expected, true, false,
                                       __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE))
        {
          bk7258_rpmsg_test_send_error_report(msg, -EBUSY);
          return -EBUSY;
        }

      priv->generation = msg->generation;
      priv->run_id = msg->run_id;
      priv->count = msg->sequence;
      priv->payload_size = msg->payload_size;
      priv->flags = msg->slot;
      priv->timeout_ms = msg->value;
      __asm volatile ("dmb sy" ::: "memory");
      (void)nxsem_post(&priv->start_sem);
      return OK;
    }

  if (msg->command == BK7258_RPMSG_TEST_COMMAND_ECHO_RESPONSE &&
      msg->generation == priv->generation &&
      msg->run_id == priv->run_id && msg->slot < 2 &&
      msg->sequence == priv->awaiting[msg->slot])
    {
      int status = (int32_t)msg->value;

      if (status >= 0 && !bk7258_rpmsg_test_payload_valid(msg, len))
        {
          status = -EBADMSG;
        }

      __atomic_fetch_or(&priv->result.cpu[msg->slot].callback_cpu_mask,
                        1u << up_cpu_index(), __ATOMIC_RELAXED);
      __atomic_store_n(&priv->reply_status[msg->slot], status,
                       __ATOMIC_RELEASE);
      __asm volatile ("dmb sy" ::: "memory");
      if (msg->slot == 0)
        {
          (void)nxsem_post(&priv->reply_sem[msg->slot]);
        }
      else
        {
          __asm volatile ("sev" ::: "memory");
        }

      return OK;
    }
#else
  if (msg->command == BK7258_RPMSG_TEST_COMMAND_ECHO && msg->slot < 2)
    {
      volatile struct bk7258_rptun_control_s *control =
        bk7258_rptun_control();
      int status = OK;

      if (msg->generation != control->generation ||
          !bk7258_rpmsg_test_payload_valid(msg, len))
        {
          status = -EBADMSG;
        }

      msg->command = BK7258_RPMSG_TEST_COMMAND_ECHO_RESPONSE;
      msg->value = (uint32_t)status;
      return bk7258_rpmsg_test_send_bounded(
               msg, len, BK7258_RPMSG_TEST_SEND_TIMEOUT);
    }

  if (msg->command == BK7258_RPMSG_TEST_COMMAND_REPORT &&
      msg->generation == priv->waiting_generation &&
      msg->run_id == priv->waiting_run_id &&
      len == BK7258_RPMSG_TEST_WIRE_HEADER_SIZE +
             sizeof(struct bk7258_rpmsg_test_result_s))
    {
      memcpy(&priv->report, msg->data, sizeof(priv->report));
      __asm volatile ("dmb sy" ::: "memory");
      priv->report_valid = true;
      (void)nxsem_post(&priv->report_sem);
      return OK;
    }
#endif

  return -ENOMSG;
}

static void bk7258_rpmsg_test_device_created(FAR struct rpmsg_device *rdev,
                                              FAR void *priv_)
{
  struct bk7258_rpmsg_test_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);
#ifdef CONFIG_BK7258_AP_CORE
  int ret;
#endif

  if (cpuname == NULL || strcmp(cpuname, BK7258_RPMSG_TEST_REMOTE_NAME) != 0)
    {
      return;
    }

#ifdef CONFIG_BK7258_AP_CORE
  priv->ept.priv = priv;
  ret = rpmsg_create_ept(&priv->ept, rdev, BK7258_RPMSG_TEST_EPT_NAME,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                         bk7258_rpmsg_test_ept_cb, NULL);
  if (ret >= 0)
    {
      priv->connection_error = OK;
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
  else
    {
      priv->connection_error = ret;
    }
#else
  /* The AP announces the service through RPMsg Name Service.  The CP binds
   * it in bk7258_rpmsg_test_ns_bind(), matching stock server-side services
   * such as syslog_rpmsg_server instead of bypassing NS with two static
   * endpoints.
   */

  priv->connection_error = OK;
#endif
}

#ifndef CONFIG_BK7258_AP_CORE
static bool bk7258_rpmsg_test_ns_match(FAR struct rpmsg_device *rdev,
                                        FAR void *priv_,
                                        FAR const char *name,
                                        uint32_t dest)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  (void)dest;
  return cpuname != NULL &&
         strcmp(cpuname, BK7258_RPMSG_TEST_REMOTE_NAME) == 0 &&
         strcmp(name, BK7258_RPMSG_TEST_EPT_NAME) == 0;
}

static void bk7258_rpmsg_test_ns_bind(FAR struct rpmsg_device *rdev,
                                       FAR void *priv_,
                                       FAR const char *name,
                                       uint32_t dest)
{
  struct bk7258_rpmsg_test_dev_s *priv = priv_;
  int ret;

  priv->ept.priv = priv;
  ret = rpmsg_create_ept(&priv->ept, rdev, name,
                         RPMSG_ADDR_ANY, dest,
                         bk7258_rpmsg_test_ept_cb, NULL);
  if (ret >= 0)
    {
      priv->connection_error = OK;
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
      bk7258_rptun_mark_connected();
    }
  else
    {
      priv->connection_error = ret;
    }
}
#endif

static void bk7258_rpmsg_test_device_destroy(FAR struct rpmsg_device *rdev,
                                              FAR void *priv_)
{
  struct bk7258_rpmsg_test_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, BK7258_RPMSG_TEST_REMOTE_NAME) != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  priv->connection_error = -ENOTCONN;
#ifdef CONFIG_BK7258_AP_CORE
  __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
  (void)nxsem_post(&priv->start_sem);
  (void)nxsem_post(&priv->reply_sem[0]);
  __asm volatile ("dmb sy; sev" ::: "memory");
#else
  (void)nxsem_post(&priv->report_sem);
#endif
  if (priv->ept.rdev != NULL)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_rpmsg_test_initialize(void)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
#ifdef CONFIG_BK7258_AP_CORE
  pthread_t thread;
  uint32_t i;
  uint32_t spawn_stage;
#endif
  bool expected = false;
  int ret;

  if (!__atomic_compare_exchange_n(&priv->initialized, &expected, true,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return OK;
    }

#ifdef CONFIG_BK7258_AP_CORE
  ret = bk7258_rpmsg_test_sem_init(&priv->start_sem);

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->done_sem);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->worker_start_sem[0]);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->worker_start_sem[1]);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->reply_sem[0]);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->reply_sem[1]);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->load_start_sem);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->load_done_sem);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->tx_sem);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->tx_done_sem[0]);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->tx_done_sem[1]);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_spawn(
              &thread, 0, BK7258_RPMSG_TEST_TX_PRIO,
              bk7258_rpmsg_test_tx_gateway, NULL, &spawn_stage);
    }

  for (i = 0; i < 2 && ret >= 0; i++)
    {
      ret = bk7258_rpmsg_test_spawn(
              &thread, i, BK7258_RPMSG_TEST_WORKER_PRIO,
              bk7258_rpmsg_test_worker, (FAR void *)(uintptr_t)i,
              &spawn_stage);
    }

  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_spawn(
              &thread, 0, BK7258_RPMSG_TEST_LOAD_PRIO,
              bk7258_rpmsg_test_load_worker, NULL, &spawn_stage);
    }

  if (ret >= 0)
    {
      ret = kthread_create("bk-rpmsg-test",
                           BK7258_RPMSG_TEST_CONTROLLER_PRIO,
                           BK7258_RPMSG_TEST_THREAD_STACK,
                           bk7258_rpmsg_test_controller, NULL);
      if (ret >= 0)
        {
          ret = OK;
        }
    }
#else
  /* In the official SDK split the CP owns AON RTC initialization and the AP
   * reads the resulting shared counter.  Retain that ownership here through
   * the public SDK API instead of duplicating its register sequence.
   */

  ret = bk_aon_rtc_driver_init();
  if (ret >= 0)
    {
      ret = bk7258_rpmsg_test_sem_init(&priv->report_sem);
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(priv,
                                    bk7258_rpmsg_test_device_created,
                                    bk7258_rpmsg_test_device_destroy,
#ifdef CONFIG_BK7258_AP_CORE
                                    NULL, NULL);
#else
                                    bk7258_rpmsg_test_ns_match,
                                    bk7258_rpmsg_test_ns_bind);
#endif
    }

  if (ret < 0)
    {
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
    }

  return ret;
}

#ifndef CONFIG_BK7258_AP_CORE
int bk7258_rpmsg_test_run(uint32_t count, uint32_t payload_size,
                          uint32_t flags, uint32_t timeout_ms,
                          struct bk7258_rpmsg_test_result_s *result)
{
  struct bk7258_rpmsg_test_dev_s *priv = &g_bk7258_rpmsg_test;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_rpmsg_test_wire_s msg;
  clock_t start;
  int ret;

  if (result == NULL || count == 0 ||
      count > BK7258_RPMSG_TEST_MAX_COUNT || payload_size == 0 ||
      payload_size > BK7258_RPMSG_TEST_MAX_PAYLOAD ||
      (flags & ~BK7258_RPMSG_TEST_VALID_FLAGS) != 0 ||
      timeout_ms < BK7258_RPMSG_TEST_MIN_TIMEOUT ||
      timeout_ms > BK7258_RPMSG_TEST_MAX_TIMEOUT)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  ret = nxmutex_lock(&g_bk7258_rpmsg_test_lock);
  if (ret < 0)
    {
      return ret;
    }

  start = clock_systime_ticks();
  while (!bk7258_rpmsg_test_endpoint_ready() &&
         (clock_systime_ticks() - start) < MSEC2TICK(3000))
    {
      nxsig_usleep(1000);
    }

  if (!bk7258_rpmsg_test_endpoint_ready())
    {
      ret = priv->connection_error < 0 ?
            priv->connection_error : -ENOTCONN;
      goto out;
    }

  bk7258_rpmsg_test_flush_sem(&priv->report_sem);
  if (++g_bk7258_rpmsg_test_next_run == 0)
    {
      g_bk7258_rpmsg_test_next_run++;
    }

  priv->waiting_generation = control->generation;
  priv->waiting_run_id = g_bk7258_rpmsg_test_next_run;
  priv->report_valid = false;
  priv->connection_error = OK;
  memset(&priv->report, 0, sizeof(priv->report));

  memset(&msg, 0, BK7258_RPMSG_TEST_WIRE_HEADER_SIZE);
  msg.magic = BK7258_RPMSG_TEST_WIRE_MAGIC;
  msg.version = BK7258_RPMSG_TEST_WIRE_VERSION;
  msg.command = BK7258_RPMSG_TEST_COMMAND_START;
  msg.generation = priv->waiting_generation;
  msg.run_id = priv->waiting_run_id;
  msg.sequence = count;
  msg.slot = flags;
  msg.payload_size = payload_size;
  msg.value = timeout_ms;

  ret = bk7258_rpmsg_test_send_bounded(
          &msg, BK7258_RPMSG_TEST_WIRE_HEADER_SIZE,
          BK7258_RPMSG_TEST_SEND_TIMEOUT);
  if (ret < 0)
    {
      goto out;
    }

  ret = nxsem_tickwait_uninterruptible(&priv->report_sem,
                                        MSEC2TICK(timeout_ms));
  if (ret < 0)
    {
      goto out;
    }

  __asm volatile ("dmb sy" ::: "memory");
  if (!priv->report_valid)
    {
      ret = priv->connection_error < 0 ?
            priv->connection_error : -EPROTO;
      goto out;
    }

  memcpy(result, &priv->report, sizeof(*result));
  if (result->magic != BK7258_RPMSG_TEST_RESULT_MAGIC ||
      result->version != BK7258_RPMSG_TEST_RESULT_VERSION ||
      result->size != sizeof(*result) ||
      result->generation != priv->waiting_generation ||
      result->run_id != priv->waiting_run_id)
    {
      ret = -EPROTO;
      goto out;
    }

  ret = result->status;

out:
  nxmutex_unlock(&g_bk7258_rpmsg_test_lock);
  return ret;
}
#endif

#endif /* CONFIG_BK7258_RPMSG_TEST */
