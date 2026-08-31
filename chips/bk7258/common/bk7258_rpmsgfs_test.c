/****************************************************************************
 * chips/bk7258/common/
 * bk7258_rpmsgfs_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded control plane for exercising the stock NuttX RPMsgFS client on
 * AP.  File operations run only in one disposable logical-CPU0 worker; no
 * VFS call or blocking send is made from an RPMsg endpoint callback.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_RPMSGFS_TEST

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_rptun.h>

#include "bk7258_rptun.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_RPMSGFS_TEST_EPT_NAME       "bk7258-rpmsgfs-test"
#define BK7258_RPMSGFS_TEST_WIRE_MAGIC     0x57465242u /* "BRFW" */
#define BK7258_RPMSGFS_TEST_WIRE_VERSION   1u
#define BK7258_RPMSGFS_TEST_SEND_TIMEOUT   500u
#define BK7258_RPMSGFS_TEST_CONNECT_MS     3000u
#define BK7258_RPMSGFS_TEST_MIN_TIMEOUT    1000u
#define BK7258_RPMSGFS_TEST_MAX_TIMEOUT    120000u

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_RPMSGFS_TEST_REMOTE_NAME  "cp"
#  define BK7258_RPMSGFS_TEST_DIR          "/cpdata/rpmsgfs-n11"
#  define BK7258_RPMSGFS_TEST_FILE         \
     "/cpdata/rpmsgfs-n11/item.bin"
#  define BK7258_RPMSGFS_TEST_RENAMED      \
     "/cpdata/rpmsgfs-n11/renamed.bin"
#else
#  define BK7258_RPMSGFS_TEST_REMOTE_NAME  "ap"
#endif

enum bk7258_rpmsgfs_test_command_e
{
  BK7258_RPMSGFS_TEST_COMMAND_START = 1,
  BK7258_RPMSGFS_TEST_COMMAND_REPORT
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_rpmsgfs_test_wire_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t command;
  uint32_t generation;
  uint32_t sequence;
  uint32_t iterations;
  uint32_t payload_size;
  uint32_t timeout_ms;
  struct bk7258_rpmsgfs_test_result_s result;
};

struct bk7258_rpmsgfs_test_dev_s
{
  struct rpmsg_endpoint ept;
  bool initialized;
  bool endpoint_created;
  int connection_error;
#ifdef CONFIG_BK7258_AP_CORE
  sem_t start_sem;
  bool abort;
  bool busy;
  int request_status;
  uint32_t request_generation;
  uint32_t request_sequence;
  uint32_t request_iterations;
  uint32_t request_payload_size;
  uint32_t request_timeout_ms;
#else
  sem_t report_sem;
  bool report_valid;
  uint32_t waiting_generation;
  uint32_t waiting_sequence;
  struct bk7258_rpmsgfs_test_result_s report;
#endif
};

/****************************************************************************
 * Compile-time ABI Gates
 ****************************************************************************/

static_assert(offsetof(struct bk7258_rpmsgfs_test_wire_s, result) == 32u,
              "BK7258 RPMsgFS wire header changed");
static_assert(sizeof(struct bk7258_rpmsgfs_test_wire_s) <=
              BK7258_RPMSG_TEST_FRAME_SIZE,
              "BK7258 RPMsgFS report cannot fit one RPMsg frame");

#if defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_BK7258_AP_SUPERVISOR)
static_assert(CONFIG_BK7258_RPMSGFS_TEST_PRIORITY <
              CONFIG_BK7258_AP_SUPERVISOR_PRIORITY,
              "RPMsgFS test worker must remain below AP heartbeat");
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_rpmsgfs_test_dev_s g_bk7258_rpmsgfs_test;

#ifndef CONFIG_BK7258_AP_CORE
static mutex_t g_bk7258_rpmsgfs_test_lock = NXMUTEX_INITIALIZER;
static uint32_t g_bk7258_rpmsgfs_test_next_sequence;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_rpmsgfs_test_sem_init(sem_t *sem)
{
  int ret = nxsem_init(sem, 0, 0);

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(sem, SEM_PRIO_NONE);
    }
#endif

  return ret;
}

static void bk7258_rpmsgfs_test_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static bool bk7258_rpmsgfs_test_generation_ready(uint32_t generation)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  __asm volatile ("dmb sy" ::: "memory");
  return generation != 0 &&
         control->magic == BK7258_RPTUN_CONTROL_MAGIC &&
         control->version == BK7258_RPTUN_CONTROL_VERSION &&
         control->size == sizeof(*control) &&
         control->generation == generation &&
         control->state == BK7258_RPTUN_STATE_CONNECTED;
}

static bool bk7258_rpmsgfs_test_endpoint_ready(void)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = &g_bk7258_rpmsgfs_test;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  return __atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) &&
         bk7258_rpmsgfs_test_generation_ready(control->generation) &&
         is_rpmsg_ept_ready(&priv->ept);
}

static bool bk7258_rpmsgfs_test_deadline_expired(clock_t started,
                                                  uint32_t timeout_ms)
{
  clock_t elapsed = clock_systime_ticks() - started;

  return elapsed >= MSEC2TICK(timeout_ms);
}

static int bk7258_rpmsgfs_test_send_bounded(const void *data, size_t len,
                                             uint32_t generation,
                                             uint32_t timeout_ms)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = &g_bk7258_rpmsgfs_test;
  clock_t started = clock_systime_ticks();
  int ret = -ENOTCONN;

  for (; ; )
    {
      if (!bk7258_rpmsgfs_test_endpoint_ready() ||
          !bk7258_rpmsgfs_test_generation_ready(generation))
        {
          return -ENOTCONN;
        }

      ret = rpmsg_trysend(&priv->ept, data, len);
      if (ret >= 0)
        {
          return OK;
        }

      if (bk7258_rpmsgfs_test_deadline_expired(started, timeout_ms))
        {
          break;
        }

      nxsig_usleep(1000);
    }

  return ret < 0 ? ret : -ETIMEDOUT;
}

#ifdef CONFIG_BK7258_AP_CORE

static int bk7258_rpmsgfs_test_neg_errno(void)
{
  int error = get_errno();
  return error > 0 ? -error : -EIO;
}

static void bk7258_rpmsgfs_test_heap(uint32_t *used, uint32_t *free_bytes,
                                     uint32_t *largest)
{
  struct mallinfo info = mallinfo();

  *used = info.uordblks;
  *free_bytes = info.fordblks;
  *largest = info.mxordblk;
}

static uint32_t bk7258_rpmsgfs_test_checksum(const uint8_t *data,
                                              uint32_t length)
{
  uint32_t hash = 2166136261u;
  uint32_t i;

  for (i = 0; i < length; i++)
    {
      hash ^= data[i];
      hash *= 16777619u;
    }

  return hash;
}

static void bk7258_rpmsgfs_test_cleanup(void)
{
  (void)unlink(BK7258_RPMSGFS_TEST_RENAMED);
  (void)unlink(BK7258_RPMSGFS_TEST_FILE);
  (void)rmdir(BK7258_RPMSGFS_TEST_DIR);
}

static int bk7258_rpmsgfs_test_files(
  struct bk7258_rpmsgfs_test_result_s *result)
{
  uint8_t tx[BK7258_RPMSGFS_TEST_MAX_PAYLOAD];
  uint8_t rx[BK7258_RPMSGFS_TEST_MAX_PAYLOAD];
  struct stat st;
  struct dirent *entry;
  DIR *dir = NULL;
  uint32_t iteration;
  uint32_t offset;
  uint32_t checksum;
  bool found;
  ssize_t transferred;
  int fd = -1;
  int ret = OK;

  result->step = BK7258_RPMSGFS_TEST_STEP_CLEANUP;
  bk7258_rpmsgfs_test_cleanup();

  result->step = BK7258_RPMSGFS_TEST_STEP_MKDIR;
  if (mkdir(BK7258_RPMSGFS_TEST_DIR, 0777) < 0 &&
      get_errno() != EEXIST)
    {
      return bk7258_rpmsgfs_test_neg_errno();
    }

  for (iteration = 0; iteration < result->iterations_requested; iteration++)
    {
      if (!bk7258_rpmsgfs_test_generation_ready(result->generation) ||
          __atomic_load_n(&g_bk7258_rpmsgfs_test.abort,
                          __ATOMIC_ACQUIRE))
        {
          result->step = BK7258_RPMSGFS_TEST_STEP_GENERATION;
          ret = -ENOTCONN;
          goto out;
        }

      for (offset = 0; offset < result->payload_size; offset++)
        {
          tx[offset] = (uint8_t)(result->sequence + iteration * 33u +
                                 offset);
        }

      memset(rx, 0, result->payload_size);
      checksum = bk7258_rpmsgfs_test_checksum(tx, result->payload_size);

      result->step = BK7258_RPMSGFS_TEST_STEP_OPEN_WRITE;
      fd = open(BK7258_RPMSGFS_TEST_FILE,
                O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd < 0)
        {
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      result->step = BK7258_RPMSGFS_TEST_STEP_WRITE;
      offset = 0;
      while (offset < result->payload_size)
        {
          transferred = write(fd, &tx[offset],
                              result->payload_size - offset);
          if (transferred <= 0)
            {
              ret = transferred < 0 ? bk7258_rpmsgfs_test_neg_errno() :
                                      -EIO;
              goto out;
            }

          offset += (uint32_t)transferred;
          result->bytes_written += (uint32_t)transferred;
        }

      result->step = BK7258_RPMSGFS_TEST_STEP_SYNC;
      if (fsync(fd) < 0)
        {
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      if (close(fd) < 0)
        {
          fd = -1;
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      fd = -1;
      result->step = BK7258_RPMSGFS_TEST_STEP_STAT;
      if (stat(BK7258_RPMSGFS_TEST_FILE, &st) < 0)
        {
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      if (st.st_size != (off_t)result->payload_size)
        {
          ret = -EFBIG;
          goto out;
        }

      result->step = BK7258_RPMSGFS_TEST_STEP_OPEN_READ;
      fd = open(BK7258_RPMSGFS_TEST_FILE, O_RDONLY);
      if (fd < 0)
        {
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      result->step = BK7258_RPMSGFS_TEST_STEP_SEEK;
      if (lseek(fd, 0, SEEK_SET) != 0)
        {
          ret = -EIO;
          goto out;
        }

      result->step = BK7258_RPMSGFS_TEST_STEP_READ;
      offset = 0;
      while (offset < result->payload_size)
        {
          transferred = read(fd, &rx[offset],
                             result->payload_size - offset);
          if (transferred <= 0)
            {
              ret = transferred < 0 ? bk7258_rpmsgfs_test_neg_errno() :
                                      -EIO;
              goto out;
            }

          offset += (uint32_t)transferred;
          result->bytes_read += (uint32_t)transferred;
        }

      if (close(fd) < 0)
        {
          fd = -1;
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      fd = -1;
      result->expected_checksum ^= checksum + iteration;
      result->actual_checksum ^=
        bk7258_rpmsgfs_test_checksum(rx, result->payload_size) + iteration;
      result->step = BK7258_RPMSGFS_TEST_STEP_VERIFY;
      if (memcmp(tx, rx, result->payload_size) != 0 ||
          result->actual_checksum != result->expected_checksum)
        {
          ret = -EBADMSG;
          goto out;
        }

      result->step = BK7258_RPMSGFS_TEST_STEP_RENAME;
      if (rename(BK7258_RPMSGFS_TEST_FILE,
                 BK7258_RPMSGFS_TEST_RENAMED) < 0)
        {
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      result->step = BK7258_RPMSGFS_TEST_STEP_OPENDIR;
      dir = opendir(BK7258_RPMSGFS_TEST_DIR);
      if (dir == NULL)
        {
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      found = false;
      result->step = BK7258_RPMSGFS_TEST_STEP_READDIR;
      while ((entry = readdir(dir)) != NULL)
        {
          result->dir_entries++;
          if (strcmp(entry->d_name, "renamed.bin") == 0)
            {
              found = true;
            }
        }

      if (closedir(dir) < 0)
        {
          dir = NULL;
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      dir = NULL;
      if (!found)
        {
          ret = -ENOENT;
          goto out;
        }

      result->step = BK7258_RPMSGFS_TEST_STEP_UNLINK;
      if (unlink(BK7258_RPMSGFS_TEST_RENAMED) < 0)
        {
          ret = bk7258_rpmsgfs_test_neg_errno();
          goto out;
        }

      result->iterations_completed++;
    }

  result->step = BK7258_RPMSGFS_TEST_STEP_RMDIR;
  if (rmdir(BK7258_RPMSGFS_TEST_DIR) < 0)
    {
      ret = bk7258_rpmsgfs_test_neg_errno();
      goto out;
    }

  result->step = BK7258_RPMSGFS_TEST_STEP_NONE;

out:
  if (fd >= 0)
    {
      (void)close(fd);
    }

  if (dir != NULL)
    {
      (void)closedir(dir);
    }

  if (ret < 0)
    {
      bk7258_rpmsgfs_test_cleanup();
    }

  return ret;
}

static void bk7258_rpmsgfs_test_send_immediate_error(
  const struct bk7258_rpmsgfs_test_wire_s *request, int status)
{
  struct bk7258_rpmsgfs_test_wire_s response;

  memset(&response, 0, sizeof(response));
  response.magic = BK7258_RPMSGFS_TEST_WIRE_MAGIC;
  response.version = BK7258_RPMSGFS_TEST_WIRE_VERSION;
  response.command = BK7258_RPMSGFS_TEST_COMMAND_REPORT;
  response.generation = request->generation;
  response.sequence = request->sequence;
  response.iterations = request->iterations;
  response.payload_size = request->payload_size;
  response.timeout_ms = request->timeout_ms;
  response.result.magic = BK7258_RPMSGFS_TEST_RESULT_MAGIC;
  response.result.version = BK7258_RPMSGFS_TEST_RESULT_VERSION;
  response.result.size = sizeof(response.result);
  response.result.generation = request->generation;
  response.result.sequence = request->sequence;
  response.result.iterations_requested = request->iterations;
  response.result.payload_size = request->payload_size;
  response.result.status = status;
  response.result.worker_cpu = up_cpu_index();
  (void)rpmsg_trysend(&g_bk7258_rpmsgfs_test.ept, &response,
                      offsetof(struct bk7258_rpmsgfs_test_wire_s, result) +
                      sizeof(response.result));
}

static FAR void *bk7258_rpmsgfs_test_worker(FAR void *arg)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = &g_bk7258_rpmsgfs_test;
  struct bk7258_rpmsgfs_test_wire_s response;
  struct bk7258_rpmsgfs_test_result_s *result = &response.result;
  uint32_t generation;
  uint32_t sequence;
  int status;

  (void)arg;
  for (; ; )
    {
      if (nxsem_wait_uninterruptible(&priv->start_sem) < 0)
        {
          continue;
        }

      if (!__atomic_load_n(&priv->busy, __ATOMIC_ACQUIRE))
        {
          continue;
        }

      generation = priv->request_generation;
      sequence = priv->request_sequence;
      memset(&response, 0, sizeof(response));
      response.magic = BK7258_RPMSGFS_TEST_WIRE_MAGIC;
      response.version = BK7258_RPMSGFS_TEST_WIRE_VERSION;
      response.command = BK7258_RPMSGFS_TEST_COMMAND_REPORT;
      response.generation = generation;
      response.sequence = sequence;
      response.iterations = priv->request_iterations;
      response.payload_size = priv->request_payload_size;
      response.timeout_ms = priv->request_timeout_ms;

      result->magic = BK7258_RPMSGFS_TEST_RESULT_MAGIC;
      result->version = BK7258_RPMSGFS_TEST_RESULT_VERSION;
      result->size = sizeof(*result);
      result->generation = generation;
      result->sequence = sequence;
      result->iterations_requested = priv->request_iterations;
      result->payload_size = priv->request_payload_size;
      result->worker_cpu = up_cpu_index();
      result->status = -EINPROGRESS;

      bk7258_rpmsgfs_test_heap(&result->heap_before_used,
                               &result->heap_before_free,
                               &result->heap_before_largest);

      status = priv->request_status;
      if (status >= 0 && result->worker_cpu != 0)
        {
          result->step = BK7258_RPMSGFS_TEST_STEP_GENERATION;
          status = -EXDEV;
        }

      if (status >= 0 && !bk7258_rpmsgfs_test_generation_ready(generation))
        {
          result->step = BK7258_RPMSGFS_TEST_STEP_GENERATION;
          status = -ENOTCONN;
        }

      if (status >= 0)
        {
          status = bk7258_rpmsgfs_test_files(result);
        }

      result->status = status;
      bk7258_rpmsgfs_test_heap(&result->heap_after_used,
                               &result->heap_after_free,
                               &result->heap_after_largest);
      __asm volatile ("dmb sy" ::: "memory");

      if (bk7258_rpmsgfs_test_send_bounded(
            &response, offsetof(struct bk7258_rpmsgfs_test_wire_s, result) +
                       sizeof(response.result), generation,
            BK7258_RPMSGFS_TEST_SEND_TIMEOUT) < 0)
        {
          result->step = BK7258_RPMSGFS_TEST_STEP_REPORT;
        }

      __atomic_store_n(&priv->busy, false, __ATOMIC_RELEASE);
    }

  return NULL;
}

static int bk7258_rpmsgfs_test_spawn_worker(void)
{
  pthread_attr_t attr;
  struct sched_param param;
  cpu_set_t cpuset = (cpu_set_t)1u;
  pthread_t thread;
  int initialized = 0;
  int ret;

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      initialized = 1;
      ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr,
                                      CONFIG_BK7258_RPMSGFS_TEST_STACKSIZE);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = CONFIG_BK7258_RPMSGFS_TEST_PRIORITY;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attr, bk7258_rpmsgfs_test_worker, NULL);
    }

  if (initialized)
    {
      (void)pthread_attr_destroy(&attr);
    }

  return ret == 0 ? OK : -ret;
}

#endif /* CONFIG_BK7258_AP_CORE */

static int bk7258_rpmsgfs_test_ept_cb(FAR struct rpmsg_endpoint *ept,
                                       FAR void *data, size_t len,
                                       uint32_t src, FAR void *priv_)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = priv_;
  struct bk7258_rpmsgfs_test_wire_s *msg = data;

  (void)ept;
  (void)src;
  if (msg == NULL ||
      len < offsetof(struct bk7258_rpmsgfs_test_wire_s, result) ||
      msg->magic != BK7258_RPMSGFS_TEST_WIRE_MAGIC ||
      msg->version != BK7258_RPMSGFS_TEST_WIRE_VERSION)
    {
      return -EINVAL;
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (msg->command == BK7258_RPMSGFS_TEST_COMMAND_START)
    {
      bool expected = false;
      int request_status = OK;

      if (!__atomic_compare_exchange_n(&priv->busy, &expected, true, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
          bk7258_rpmsgfs_test_send_immediate_error(msg, -EBUSY);
          return OK;
        }

      if (msg->iterations == 0 ||
          msg->iterations > BK7258_RPMSGFS_TEST_MAX_ITERATIONS ||
          msg->payload_size == 0 ||
          msg->payload_size > BK7258_RPMSGFS_TEST_MAX_PAYLOAD ||
          msg->timeout_ms < BK7258_RPMSGFS_TEST_MIN_TIMEOUT ||
          msg->timeout_ms > BK7258_RPMSGFS_TEST_MAX_TIMEOUT)
        {
          request_status = -EINVAL;
        }
      else if (!bk7258_rpmsgfs_test_generation_ready(msg->generation))
        {
          request_status = -ENOTCONN;
        }

      priv->request_generation = msg->generation;
      priv->request_sequence = msg->sequence;
      priv->request_iterations = msg->iterations;
      priv->request_payload_size = msg->payload_size;
      priv->request_timeout_ms = msg->timeout_ms;
      priv->request_status = request_status;
      __asm volatile ("dmb sy" ::: "memory");
      (void)nxsem_post(&priv->start_sem);
      return OK;
    }
#else
  if (msg->command == BK7258_RPMSGFS_TEST_COMMAND_REPORT &&
      len == offsetof(struct bk7258_rpmsgfs_test_wire_s, result) +
             sizeof(msg->result) &&
      msg->generation == priv->waiting_generation &&
      msg->sequence == priv->waiting_sequence)
    {
      memcpy(&priv->report, &msg->result, sizeof(priv->report));
      __asm volatile ("dmb sy" ::: "memory");
      priv->report_valid = true;
      (void)nxsem_post(&priv->report_sem);
      return OK;
    }
#endif

  return -ENOMSG;
}

static void bk7258_rpmsgfs_test_device_created(
  FAR struct rpmsg_device *rdev, FAR void *priv_)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL ||
      strcmp(cpuname, BK7258_RPMSGFS_TEST_REMOTE_NAME) != 0)
    {
      return;
    }

#ifdef CONFIG_BK7258_AP_CORE
  priv->ept.priv = priv;
  priv->abort = false;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, BK7258_RPMSGFS_TEST_EPT_NAME,
    RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, bk7258_rpmsgfs_test_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
#else
  priv->connection_error = OK;
#endif
}

#ifndef CONFIG_BK7258_AP_CORE
static bool bk7258_rpmsgfs_test_ns_match(FAR struct rpmsg_device *rdev,
                                         FAR void *priv_,
                                         FAR const char *name,
                                         uint32_t dest)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  (void)dest;
  return cpuname != NULL &&
         strcmp(cpuname, BK7258_RPMSGFS_TEST_REMOTE_NAME) == 0 &&
         strcmp(name, BK7258_RPMSGFS_TEST_EPT_NAME) == 0;
}

static void bk7258_rpmsgfs_test_ns_bind(FAR struct rpmsg_device *rdev,
                                        FAR void *priv_,
                                        FAR const char *name,
                                        uint32_t dest)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = priv_;

  priv->ept.priv = priv;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, name, RPMSG_ADDR_ANY, dest,
    bk7258_rpmsgfs_test_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
}
#endif

static void bk7258_rpmsgfs_test_device_destroy(
  FAR struct rpmsg_device *rdev, FAR void *priv_)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL ||
      strcmp(cpuname, BK7258_RPMSGFS_TEST_REMOTE_NAME) != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  priv->connection_error = -ENOTCONN;
#ifdef CONFIG_BK7258_AP_CORE
  __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
  (void)nxsem_post(&priv->start_sem);
#else
  priv->report_valid = false;
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

int bk7258_rpmsgfs_test_initialize(void)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = &g_bk7258_rpmsgfs_test;
  int ret;

  if (priv->initialized)
    {
      return OK;
    }

  memset(priv, 0, sizeof(*priv));
  priv->connection_error = -ENOTCONN;
#ifdef CONFIG_BK7258_AP_CORE
  ret = bk7258_rpmsgfs_test_sem_init(&priv->start_sem);
#else
  ret = bk7258_rpmsgfs_test_sem_init(&priv->report_sem);
#endif
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsg_register_callback(
    priv, bk7258_rpmsgfs_test_device_created,
    bk7258_rpmsgfs_test_device_destroy,
#ifdef CONFIG_BK7258_AP_CORE
    NULL, NULL);
#else
    bk7258_rpmsgfs_test_ns_match, bk7258_rpmsgfs_test_ns_bind);
#endif
  if (ret < 0)
    {
#ifdef CONFIG_BK7258_AP_CORE
      nxsem_destroy(&priv->start_sem);
#else
      nxsem_destroy(&priv->report_sem);
#endif
      return ret;
    }

#ifdef CONFIG_BK7258_AP_CORE
  ret = bk7258_rpmsgfs_test_spawn_worker();
  if (ret < 0)
    {
      rpmsg_unregister_callback(
        priv, bk7258_rpmsgfs_test_device_created,
        bk7258_rpmsgfs_test_device_destroy, NULL, NULL);
      nxsem_destroy(&priv->start_sem);
      return ret;
    }
#endif

  priv->initialized = true;
  return OK;
}

#ifndef CONFIG_BK7258_AP_CORE
int bk7258_rpmsgfs_test_run(
  uint32_t iterations, uint32_t payload_size, uint32_t timeout_ms,
  struct bk7258_rpmsgfs_test_result_s *result)
{
  struct bk7258_rpmsgfs_test_dev_s *priv = &g_bk7258_rpmsgfs_test;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_rpmsgfs_test_wire_s request;
  clock_t connect_started;
  uint32_t waited = 0;
  uint32_t slice;
  int ret;

  if (result == NULL || iterations == 0 ||
      iterations > BK7258_RPMSGFS_TEST_MAX_ITERATIONS ||
      payload_size == 0 ||
      payload_size > BK7258_RPMSGFS_TEST_MAX_PAYLOAD ||
      timeout_ms < BK7258_RPMSGFS_TEST_MIN_TIMEOUT ||
      timeout_ms > BK7258_RPMSGFS_TEST_MAX_TIMEOUT)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  ret = nxmutex_lock(&g_bk7258_rpmsgfs_test_lock);
  if (ret < 0)
    {
      return ret;
    }

  connect_started = clock_systime_ticks();
  while (!bk7258_rpmsgfs_test_endpoint_ready() &&
         !bk7258_rpmsgfs_test_deadline_expired(
           connect_started, BK7258_RPMSGFS_TEST_CONNECT_MS))
    {
      nxsig_usleep(1000);
    }

  if (!bk7258_rpmsgfs_test_endpoint_ready())
    {
      ret = priv->connection_error < 0 ?
            priv->connection_error : -ENOTCONN;
      goto out;
    }

  bk7258_rpmsgfs_test_flush_sem(&priv->report_sem);
  if (++g_bk7258_rpmsgfs_test_next_sequence == 0)
    {
      g_bk7258_rpmsgfs_test_next_sequence++;
    }

  priv->waiting_generation = control->generation;
  priv->waiting_sequence = g_bk7258_rpmsgfs_test_next_sequence;
  priv->report_valid = false;
  priv->connection_error = OK;
  memset(&priv->report, 0, sizeof(priv->report));
  memset(&request, 0, offsetof(struct bk7258_rpmsgfs_test_wire_s, result));
  request.magic = BK7258_RPMSGFS_TEST_WIRE_MAGIC;
  request.version = BK7258_RPMSGFS_TEST_WIRE_VERSION;
  request.command = BK7258_RPMSGFS_TEST_COMMAND_START;
  request.generation = priv->waiting_generation;
  request.sequence = priv->waiting_sequence;
  request.iterations = iterations;
  request.payload_size = payload_size;
  request.timeout_ms = timeout_ms;

  ret = bk7258_rpmsgfs_test_send_bounded(
          &request, offsetof(struct bk7258_rpmsgfs_test_wire_s, result),
          priv->waiting_generation, BK7258_RPMSGFS_TEST_SEND_TIMEOUT);
  if (ret < 0)
    {
      goto out;
    }

  ret = -ETIMEDOUT;
  while (waited < timeout_ms)
    {
      slice = timeout_ms - waited;
      if (slice > 50u)
        {
          slice = 50u;
        }

      ret = nxsem_tickwait_uninterruptible(&priv->report_sem,
                                            MSEC2TICK(slice));
      if (ret >= 0)
        {
          break;
        }

      if (ret != -ETIMEDOUT)
        {
          goto out;
        }

      if (!bk7258_rpmsgfs_test_endpoint_ready() ||
          !bk7258_rpmsgfs_test_generation_ready(
            priv->waiting_generation))
        {
          ret = priv->connection_error < 0 ?
                priv->connection_error : -ENOTCONN;
          goto out;
        }

      waited += slice;
    }

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
  if (result->magic != BK7258_RPMSGFS_TEST_RESULT_MAGIC ||
      result->version != BK7258_RPMSGFS_TEST_RESULT_VERSION ||
      result->size != sizeof(*result) ||
      result->generation != priv->waiting_generation ||
      result->sequence != priv->waiting_sequence)
    {
      ret = -EPROTO;
      goto out;
    }

  ret = result->status;

out:
  nxmutex_unlock(&g_bk7258_rpmsgfs_test_lock);
  return ret;
}
#endif

#endif /* CONFIG_BK7258_RPMSGFS_TEST */
