/****************************************************************************
 * apps/testing/bk7258/bk7258_native_app_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/spinlock.h>
#ifdef CONFIG_TESTS_BK7258_NATIVE_TRANSPORT
#  include <nuttx/rpmsg/rpmsg.h>
#endif

#include <arch/chip/bk7258_rptun.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "bk7258_native_abi.h"
#include "bk7258_native_apps.h"
#include "bk7258_native_fixture.h"
#include "bk7258_native_loader.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKNATIVE_TEST_PATH "/cpdata/shaniu/native-loader-test.elf"
#define BKNATIVE_TEST_DIRECTORY "/cpdata/shaniu"
#define BKNATIVE_LOCAL_TEST_PATH "/data/bknative-local-test.bin"
#define BKNATIVE_TEST_CYCLES 100u
#define BKNATIVE_TEST_READY_TRIES 240u
#define BKNATIVE_TEST_READY_USEC 250000u
#define BKNATIVE_TRANSPORT_CYCLES 1000u
#define BKNATIVE_TRANSPORT_RETRIES 1000u
#define BKNATIVE_LOCK_ROUNDS 10u
#define BKNATIVE_LOCK_ITERATIONS 100000u
#define BKNATIVE_LOCK_TRY_LIMIT 1000000u
#define BKNATIVE_SPINLOCK_BASE 0x28000000u
#define BKNATIVE_SPINLOCK_END  0x28010000u
#define BKNATIVE_INTERNAL_SRAM_END 0x280a0000u

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRACE
extern int bk7258_native_irq_sample_enable(int enable);
extern void bk7258_native_trace_enable(int enable);
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bknative_test_call_s
{
  int (*run)(unsigned int cpu, const void *argument);
  const void *argument;
  unsigned int cpu;
  int result;
};

#if defined(CONFIG_SMP) && defined(CONFIG_TESTS_BK7258_NATIVE_TRANSPORT)
struct bknative_lock_probe_s
{
  FAR rspinlock_t *lock;
  FAR volatile uint32_t *side_effect;
  bool nested_atomic;
  volatile uint32_t ready[2];
  volatile uint32_t counter;
  volatile uint32_t progress[2];
  volatile uint32_t max_tries[2];
  volatile uint32_t observed;
  volatile int failure;
};

struct bknative_lock_call_s
{
  FAR struct bknative_lock_probe_s *probe;
  unsigned int cpu;
  int result;
};
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t g_bknative_test_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_bknative_test_started;
/* Read-only CP diagnostics can inspect these words even if AP logging stops:
 * phase, completed cycles, logical CPU, reserved.
 */

static volatile uint32_t g_bknative_test_trace[4];

#ifdef CONFIG_SMP
static rspinlock_t g_bknative_probe_spin = RSPINLOCK_INITIALIZER;
static volatile uint32_t g_bknative_probe_pair[10];
#ifdef CONFIG_TESTS_BK7258_NATIVE_TRANSPORT
static rspinlock_t g_bknative_probe_special
  __attribute__((used, section(".sram_spinlock_section"))) =
  RSPINLOCK_INITIALIZER;
static volatile uint32_t g_bknative_probe_atomic;
#endif

static int bknative_test_contention(unsigned int cpu)
{
  volatile uint32_t *pair = g_bknative_probe_pair;

  if (sched_getcpu() != (int)cpu)
    {
      return -EXDEV;
    }

  __atomic_store_n(&pair[cpu], 1, __ATOMIC_RELEASE);
  while (__atomic_load_n(&pair[2], __ATOMIC_ACQUIRE) == 0)
    {
    }

  for (unsigned int i = 0; i < 100000; i++)
    {
      __atomic_fetch_add(&pair[3], 1, __ATOMIC_RELAXED);
    }

  __atomic_store_n(&pair[4 + cpu], 1, __ATOMIC_RELEASE);
  while (__atomic_load_n(&pair[4 + (cpu ^ 1)], __ATOMIC_ACQUIRE) == 0)
    {
    }

  if (pair[3] != 200000)
    {
      return -EIO;
    }

  for (unsigned int i = 0; i < 100000; i++)
    {
      irqstate_t flags =
        rspin_lock_irqsave_nopreempt(&g_bknative_probe_spin);
      pair[6]++;
      rspin_unlock_irqrestore_nopreempt(&g_bknative_probe_spin, flags);
      pair[7 + cpu] = i + 1;
    }

  return 0;
}

static void *bknative_test_contention_worker(void *argument)
{
  int *result = argument;
  *result = bknative_test_contention(1);
  return NULL;
}

static int bknative_test_contention_pair(void)
{
  pthread_attr_t attributes;
  pthread_t thread;
  cpu_set_t affinity = 0;
  int secondary = -EINPROGRESS;
  int ret;
  int join_ret;

  CPU_SET(1, &affinity);
  ret = pthread_attr_init(&attributes);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_attr_setstacksize(&attributes, 8192);
  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attributes, sizeof(affinity),
                                        &affinity);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attributes,
                            bknative_test_contention_worker, &secondary);
    }

  pthread_attr_destroy(&attributes);
  if (ret != 0)
    {
      return -ret;
    }

  while (__atomic_load_n(&g_bknative_probe_pair[1], __ATOMIC_ACQUIRE) == 0)
    {
      usleep(1000);
    }

  __atomic_store_n(&g_bknative_probe_pair[2], 1, __ATOMIC_RELEASE);
  ret = bknative_test_contention(0);
  join_ret = pthread_join(thread, NULL);
  if (ret == 0)
    {
      ret = join_ret != 0 ? -join_ret : secondary;
    }

  if (ret == 0 && (g_bknative_probe_pair[6] != 200000 ||
                  g_bknative_probe_spin.val != 0))
    {
      ret = -EIO;
    }

  syslog(LOG_INFO, "BKNATIVE_PROBE contention result=%d atomic=%" PRIu32
         " locked=%" PRIu32 " lock=%08" PRIx32 "\n", ret,
         g_bknative_probe_pair[3], g_bknative_probe_pair[6],
         g_bknative_probe_spin.val);
  return ret;
}

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRANSPORT
static int bknative_test_lock_contend(
  FAR struct bknative_lock_probe_s *probe, unsigned int cpu)
{
  if (sched_getcpu() != (int)cpu)
    {
      return -EXDEV;
    }

  __atomic_store_n(&probe->ready[cpu], 1, __ATOMIC_RELEASE);
  while (__atomic_load_n(&probe->ready[cpu ^ 1], __ATOMIC_ACQUIRE) == 0)
    {
      sched_yield();
    }

  for (unsigned int i = 0; i < BKNATIVE_LOCK_ITERATIONS; i++)
    {
      irqstate_t flags;
      uint32_t expected;
      uint32_t observed;
      uint32_t tries = 0;

      if (__atomic_load_n(&probe->failure, __ATOMIC_ACQUIRE) != 0)
        {
          return -EOWNERDEAD;
        }

      while (!rspin_trylock_irqsave_nopreempt(probe->lock, flags))
        {
          if (__atomic_load_n(&probe->failure, __ATOMIC_ACQUIRE) != 0)
            {
              return -EOWNERDEAD;
            }

          if (++tries >= BKNATIVE_LOCK_TRY_LIMIT)
            {
              probe->progress[cpu] = i;
              return -ETIMEDOUT;
            }

          if ((tries & 1023u) == 0)
            {
              sched_yield();
            }
        }

      if (probe->nested_atomic)
        {
          __atomic_fetch_or(probe->side_effect, 1u << cpu,
                            __ATOMIC_RELEASE);
          expected = (1u << 16) | (cpu + 1u);
          observed = atomic_read(&probe->lock->val);
          if (observed != expected)
            {
              probe->observed = observed;
              __atomic_store_n(&probe->failure, -EOWNERDEAD,
                               __ATOMIC_RELEASE);

              /* This CPU no longer owns the word, so a normal rspin unlock
               * would corrupt the new owner exactly as the RPMsg failure
               * does.  Only unwind this CPU's paired scheduler/IRQ state.
               */

              sched_unlock();
              up_irq_restore(flags);
              probe->progress[cpu] = i;
              return -EOWNERDEAD;
            }
        }

      probe->counter++;
      rspin_unlock_irqrestore_nopreempt(probe->lock, flags);
      if (tries > probe->max_tries[cpu])
        {
          probe->max_tries[cpu] = tries;
        }

      probe->progress[cpu] = i + 1;
    }

  return OK;
}

static FAR void *bknative_test_lock_worker(FAR void *argument)
{
  FAR struct bknative_lock_call_s *call = argument;

  call->result = bknative_test_lock_contend(call->probe, call->cpu);
  return NULL;
}

static int bknative_test_lock_pair(FAR rspinlock_t *lock,
                                   bool nested_atomic,
                                   FAR uint32_t *max_tries)
{
  struct bknative_lock_probe_s probe;
  struct bknative_lock_call_s call;
  pthread_attr_t attributes;
  pthread_t thread;
  cpu_set_t affinity = 0;
  int ret;
  int join_ret;

  memset(&probe, 0, sizeof(probe));
  atomic_set(&lock->val, 0);
  g_bknative_probe_atomic = 0;
  probe.lock = lock;
  probe.side_effect = &g_bknative_probe_atomic;
  probe.nested_atomic = nested_atomic;
  call.probe = &probe;
  call.cpu = 1;
  call.result = -EINPROGRESS;

  CPU_SET(1, &affinity);
  ret = pthread_attr_init(&attributes);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_attr_setstacksize(&attributes, 8192);
  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attributes, sizeof(affinity),
                                        &affinity);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attributes,
                           bknative_test_lock_worker, &call);
    }

  pthread_attr_destroy(&attributes);
  if (ret != 0)
    {
      return -ret;
    }

  ret = bknative_test_lock_contend(&probe, 0);
  join_ret = pthread_join(thread, NULL);
  if (ret == 0)
    {
      ret = join_ret != 0 ? -join_ret : call.result;
    }

  *max_tries = probe.max_tries[0] > probe.max_tries[1] ?
               probe.max_tries[0] : probe.max_tries[1];
  if (ret == 0 &&
      (probe.counter != BKNATIVE_LOCK_ITERATIONS * 2u ||
       atomic_read(&lock->val) != 0))
    {
      ret = -EIO;
    }

  if (ret < 0)
    {
      syslog(LOG_ERR,
             "BKNATIVE_LOCK FAIL progress=%" PRIu32 "/%" PRIu32
             " counter=%" PRIu32 " word=%08" PRIx32
             " observed=%08" PRIx32 " nested=%u result=%d\n",
             probe.progress[0], probe.progress[1], probe.counter,
             atomic_read(&lock->val), probe.observed, nested_atomic, ret);
    }

  return ret;
}

static int bknative_test_lock_series(const char *kind,
                                     FAR rspinlock_t *lock,
                                     uintptr_t expected_start,
                                     uintptr_t expected_end,
                                     bool nested_atomic)
{
  uintptr_t address = (uintptr_t)lock;
  const char *region =
    address >= BKNATIVE_SPINLOCK_BASE && address < BKNATIVE_SPINLOCK_END ?
      "special" :
    address >= BKNATIVE_SPINLOCK_END &&
    address < BKNATIVE_INTERNAL_SRAM_END ? "internal-sram" :
    address >= 0x60000000u && address < 0x64000000u ? "psram" : "other";
  uint32_t maximum = 0;
  unsigned int completed = 0;
  int ret = OK;

  if (address < expected_start || address >= expected_end)
    {
      ret = -ERANGE;
      goto done;
    }

  for (unsigned int round = 0; round < BKNATIVE_LOCK_ROUNDS; round++)
    {
      uint32_t max_tries = 0;

      ret = bknative_test_lock_pair(lock, nested_atomic, &max_tries);
      if (max_tries > maximum)
        {
          maximum = max_tries;
        }

      if (ret < 0)
        {
          break;
        }

      completed++;
    }

done:
  syslog(ret == 0 ? LOG_INFO : LOG_ERR,
         "BKNATIVE_LOCK %s kind=%s address=%p region=%s rounds=%u/%u "
         "iterations=%u max_tries=%" PRIu32
         " nested=%u atomic=%p result=%d\n",
         ret == 0 ? "PASS" : "FAIL", kind, lock, region, completed,
         BKNATIVE_LOCK_ROUNDS, BKNATIVE_LOCK_ITERATIONS, maximum,
         nested_atomic, &g_bknative_probe_atomic, ret);
  return ret;
}

static int bknative_test_lock_placement(void)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  FAR rspinlock_t *internal;
  int ret;

  ret = bknative_test_lock_series("special", &g_bknative_probe_special,
                                  BKNATIVE_SPINLOCK_BASE,
                                  BKNATIVE_SPINLOCK_END, false);
  if (ret < 0)
    {
      return ret;
    }

  internal = (FAR rspinlock_t *)(uintptr_t)state->heap_test;
  if (internal == NULL)
    {
      return -ENODATA;
    }

  ret = bknative_test_lock_series("early-kernel-heap", internal,
                                  BKNATIVE_SPINLOCK_END,
                                  BKNATIVE_INTERNAL_SRAM_END, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = bknative_test_lock_series("special-nested-atomic",
                                  &g_bknative_probe_special,
                                  BKNATIVE_SPINLOCK_BASE,
                                  BKNATIVE_SPINLOCK_END, true);
  if (ret < 0)
    {
      return ret;
    }

  return bknative_test_lock_series("early-heap-nested-atomic", internal,
                                   BKNATIVE_SPINLOCK_END,
                                   BKNATIVE_INTERNAL_SRAM_END, true);
}
#endif
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bknative_test_descriptors(void)
{
  DIR *directory;
  struct dirent *entry;
  int count = 0;

  directory = opendir("/proc/self/fd");
  if (directory == NULL)
    {
      return -1;
    }

  while ((entry = readdir(directory)) != NULL)
    {
      if (strcmp(entry->d_name, ".") != 0 &&
          strcmp(entry->d_name, "..") != 0)
        {
          count++;
        }
    }

  closedir(directory);
  return count;
}

static int bknative_test_write_fixture(void)
{
  size_t offset = 0;
  int descriptor;

  if (g_bknative_fixture_size == 0 ||
      g_bknative_fixture_size > BKNATIVE_IMAGE_MAX)
    {
      return -EFBIG;
    }

  for (unsigned int attempt = 0;
       attempt < BKNATIVE_TEST_READY_TRIES; attempt++)
    {
      struct stat status;
      if (stat(BKNATIVE_TEST_DIRECTORY, &status) == 0 &&
          S_ISDIR(status.st_mode))
        {
          break;
        }

      if (attempt + 1 == BKNATIVE_TEST_READY_TRIES)
        {
          return errno != 0 ? -errno : -ENOENT;
        }

      usleep(BKNATIVE_TEST_READY_USEC);
    }

  descriptor = open(BKNATIVE_TEST_PATH, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (descriptor < 0)
    {
      return -errno;
    }

  while (offset < g_bknative_fixture_size)
    {
      ssize_t written = write(descriptor, g_bknative_fixture + offset,
                              g_bknative_fixture_size - offset);
      if (written <= 0)
        {
          int error = written < 0 ? -errno : -EIO;
          close(descriptor);
          unlink(BKNATIVE_TEST_PATH);
          return error;
        }

      offset += written;
    }

  if (fsync(descriptor) < 0)
    {
      int error = -errno;
      close(descriptor);
      unlink(BKNATIVE_TEST_PATH);
      return error;
    }

  if (close(descriptor) < 0)
    {
      int error = -errno;
      unlink(BKNATIVE_TEST_PATH);
      return error;
    }

  return 0;
}

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRANSPORT
static int bknative_test_write_local_fixture(void)
{
  size_t offset = 0;
  int descriptor;

  descriptor = open(BKNATIVE_LOCAL_TEST_PATH,
                    O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (descriptor < 0)
    {
      return -errno;
    }

  while (offset < g_bknative_fixture_size)
    {
      ssize_t written = write(descriptor, g_bknative_fixture + offset,
                              g_bknative_fixture_size - offset);
      if (written <= 0)
        {
          int error = written < 0 ? -errno : -EIO;
          close(descriptor);
          unlink(BKNATIVE_LOCAL_TEST_PATH);
          return error;
        }

      offset += written;
    }

  if (close(descriptor) < 0)
    {
      int error = -errno;
      unlink(BKNATIVE_LOCAL_TEST_PATH);
      return error;
    }

  return OK;
}

static int bknative_test_local_file(unsigned int cpu, const void *argument)
{
  uint8_t bytes[32];
  int descriptor;
  int ret = OK;
  (void)cpu;
  (void)argument;

  descriptor = open(BKNATIVE_LOCAL_TEST_PATH, O_RDONLY);
  if (descriptor < 0)
    {
      return -errno;
    }

  for (unsigned int cycle = 0; cycle < BKNATIVE_TEST_CYCLES; cycle++)
    {
      ssize_t length;

      if (lseek(descriptor, 0, SEEK_SET) != 0)
        {
          ret = -errno;
          break;
        }

      length = read(descriptor, bytes, sizeof(bytes));
      if (length < 0)
        {
          ret = -errno;
          break;
        }

      if (length != sizeof(bytes) ||
          memcmp(bytes, g_bknative_fixture, sizeof(bytes)) != 0)
        {
          ret = -EIO;
          break;
        }
    }

  if (close(descriptor) < 0 && ret == OK)
    {
      ret = -errno;
    }

  return ret;
}

static int bknative_test_raw_rpmsg(unsigned int cpu, const void *argument)
{
  (void)cpu;
  (void)argument;

  for (uint32_t cycle = 0; cycle < BKNATIVE_TRANSPORT_CYCLES; cycle++)
    {
      unsigned int retry;
      int ret;

      for (retry = 0; retry < BKNATIVE_TRANSPORT_RETRIES; retry++)
        {
          ret = bk7258_rptun_native_direct_probe(cycle);
          if (ret != RPMSG_ERR_NO_BUFF)
            {
              break;
            }

          usleep(1000);
        }

      if (ret < 0)
        {
          return ret;
        }

      if (retry == BKNATIVE_TRANSPORT_RETRIES)
        {
          return -ETIMEDOUT;
        }

      usleep(1000);
    }

  return OK;
}
#endif

static const char *bknative_test_region(const void *address)
{
  uintptr_t value = (uintptr_t)address;
  return value >= 0x60000000u && value < 0x64000000u ? "psram" : "other";
}

static uint64_t bknative_test_milliseconds(void)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
      return 0;
    }

  return (uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u;
}

static int bknative_test_thread(unsigned int cpu, const void *argument)
{
  volatile uint32_t total = 0;
  (void)cpu;
  (void)argument;

  /* No allocator, log transport, filesystem or ELF call in this probe. */

  for (uint32_t value = 0; value < 10000; value++)
    {
      total += value;
    }

  return total == 49995000u ? 0 : -EIO;
}

static int bknative_test_yield(unsigned int cpu, const void *argument)
{
  (void)cpu;
  (void)argument;
  return sched_yield() == 0 ? 0 : -errno;
}

static int bknative_test_program(unsigned int cpu, const void *argument)
{
  const struct bknative_program_s *program = argument;
  union
  {
    uint64_t alignment;
    uint8_t bytes[BKNATIVE_STATE_MAX];
  } state;

  struct bknative_frame_s frame;
  int ret;
  (void)cpu;

  memset(&state, 0, sizeof(state));
  memset(&frame, 0, sizeof(frame));
  ret = program->start(state.bytes, 8);
  if (ret == 0)
    {
      ret = program->step(state.bytes, 0, &frame);
      if (ret != 0 || frame.expression != 1)
        {
          ret = -EPROTO;
        }
    }

  if (ret == 0)
    {
      ret = program->step(state.bytes, 4000, &frame);
      if (ret != 0 || frame.expression != 2)
        {
          ret = -EPROTO;
        }
    }

  if (ret == 0 && program->step(state.bytes, 8000, &frame) != 1)
    {
      ret = -EPROTO;
    }

  return ret;
}

#ifdef CONFIG_SMP
static volatile uintptr_t g_bknative_cross_program;
static volatile uint32_t g_bknative_cross_request;
static volatile uint32_t g_bknative_cross_response;
static volatile uint32_t g_bknative_cross_ready;
static volatile int g_bknative_cross_result;

static void *bknative_test_cross_worker(void *argument)
{
  uint32_t completed = 0;
  (void)argument;

  if (sched_getcpu() != 1)
    {
      __atomic_store_n(&g_bknative_cross_result, -EXDEV,
                       __ATOMIC_RELAXED);
      __atomic_store_n(&g_bknative_cross_ready, 1, __ATOMIC_RELEASE);
      return NULL;
    }

  __atomic_store_n(&g_bknative_cross_ready, 1, __ATOMIC_RELEASE);
  for (; ; )
    {
      const struct bknative_program_s *program;
      uint32_t request =
        __atomic_load_n(&g_bknative_cross_request, __ATOMIC_ACQUIRE);
      int ret;

      if (request == completed)
        {
          usleep(1000);
          continue;
        }

      program = (const struct bknative_program_s *)
        __atomic_load_n(&g_bknative_cross_program, __ATOMIC_ACQUIRE);
      ret = program ? bknative_test_program(1, program) : 0;
      __atomic_store_n(&g_bknative_cross_result, ret, __ATOMIC_RELAXED);
      __atomic_store_n(&g_bknative_cross_response, request,
                       __ATOMIC_RELEASE);
      completed = request;
      if (!program)
        {
          return NULL;
        }
    }
}

static int bknative_test_cross_execution(void)
{
  struct mallinfo before;
  struct mallinfo after;
  pthread_attr_t attributes;
  pthread_t thread;
  cpu_set_t affinity = 0;
  uint64_t started;
  unsigned int completed = 0;
  int descriptors_before;
  int ret;
  int join_ret;

  if (sched_getcpu() != 0)
    {
      return -EXDEV;
    }

  before = mallinfo();
  started = bknative_test_milliseconds();
  descriptors_before = bknative_test_descriptors();
  __atomic_store_n(&g_bknative_cross_program, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_bknative_cross_request, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_bknative_cross_response, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_bknative_cross_ready, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&g_bknative_cross_result, -EINPROGRESS,
                   __ATOMIC_RELAXED);

  CPU_SET(1, &affinity);
  ret = pthread_attr_init(&attributes);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_attr_setstacksize(&attributes, 8192);
  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attributes, sizeof(affinity),
                                        &affinity);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attributes,
                           bknative_test_cross_worker, NULL);
    }

  pthread_attr_destroy(&attributes);
  if (ret != 0)
    {
      return -ret;
    }

  while (__atomic_load_n(&g_bknative_cross_ready, __ATOMIC_ACQUIRE) == 0)
    {
      usleep(1000);
    }

  ret = __atomic_load_n(&g_bknative_cross_result, __ATOMIC_RELAXED);
  if (ret == -EINPROGRESS)
    {
      ret = 0;
    }
  else
    {
      goto join;
    }

  for (unsigned int cycle = 0;
       ret == 0 && cycle < BKNATIVE_TEST_CYCLES; cycle++)
    {
      const struct bknative_program_s *program;
      void *handle;
      uint32_t request = cycle + 1;
      int close_ret;

      ret = bknative_loader_open(BKNATIVE_TEST_PATH, &handle, &program);
      if (ret != 0)
        {
          break;
        }

      __atomic_store_n(&g_bknative_cross_program, (uintptr_t)program,
                       __ATOMIC_RELEASE);
      __atomic_store_n(&g_bknative_cross_request, request,
                       __ATOMIC_RELEASE);
      while (__atomic_load_n(&g_bknative_cross_response,
                             __ATOMIC_ACQUIRE) != request)
        {
          usleep(1000);
        }

      ret = __atomic_load_n(&g_bknative_cross_result, __ATOMIC_RELAXED);
      close_ret = bknative_loader_close(handle);
      if (ret == 0)
        {
          ret = close_ret;
        }

      if (ret == 0)
        {
          completed++;
          if (completed % 25 == 0)
            {
              syslog(LOG_INFO, "BKNATIVE_CROSS progress owner=0 "
                     "executor=1 cycles=%u\n", completed);
            }
        }
    }

  __atomic_store_n(&g_bknative_cross_program, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&g_bknative_cross_request,
                   BKNATIVE_TEST_CYCLES + 1, __ATOMIC_RELEASE);
  while (__atomic_load_n(&g_bknative_cross_response,
                         __ATOMIC_ACQUIRE) != BKNATIVE_TEST_CYCLES + 1)
    {
      usleep(1000);
    }

join:
  join_ret = pthread_join(thread, NULL);
  if (ret == 0 && join_ret != 0)
    {
      ret = -join_ret;
    }

  after = mallinfo();
  syslog(LOG_INFO,
         "BKNATIVE_CROSS %s owner=0 executor=1 cycles=%u "
         "elapsed_ms=%" PRIu64 " fd=%d/%d heap_used=%u/%u "
         "heap_free=%u/%u result=%d\n",
         ret == 0 ? "PASS" : "FAIL", completed,
         bknative_test_milliseconds() - started, descriptors_before,
         bknative_test_descriptors(), before.uordblks, after.uordblks,
         before.fordblks, after.fordblks, ret);
  return ret;
}
#endif

static int bknative_test_file(unsigned int cpu, const void *argument)
{
  uint8_t bytes[32];
  int descriptor;
  ssize_t length;
  int ret;
  (void)cpu;
  (void)argument;

  g_bknative_test_trace[1] = 0;
  g_bknative_test_trace[2] = cpu;
  __atomic_store_n(&g_bknative_test_trace[0], 100, __ATOMIC_RELEASE);
  descriptor = open(BKNATIVE_TEST_PATH, O_RDONLY);
  if (descriptor < 0)
    {
      return -errno;
    }

  ret = 0;
  for (unsigned int cycle = 0; cycle < BKNATIVE_TEST_CYCLES; cycle++)
    {
      __atomic_store_n(&g_bknative_test_trace[0], 101, __ATOMIC_RELEASE);
      if (lseek(descriptor, 0, SEEK_SET) != 0)
        {
          ret = -errno;
          break;
        }

      __atomic_store_n(&g_bknative_test_trace[0], 102, __ATOMIC_RELEASE);
      length = read(descriptor, bytes, sizeof(bytes));
      ret = length < 0 ? -errno :
            length != sizeof(bytes) ||
            memcmp(bytes, g_bknative_fixture, sizeof(bytes)) != 0 ? -EIO : 0;
      if (ret < 0)
        {
          break;
        }

      g_bknative_test_trace[1] = cycle + 1;
    }

  __atomic_store_n(&g_bknative_test_trace[0], 103, __ATOMIC_RELEASE);
  if (close(descriptor) < 0 && ret == 0)
    {
      ret = -errno;
    }

  __atomic_store_n(&g_bknative_test_trace[0], 104, __ATOMIC_RELEASE);
  return ret;
}

#ifdef CONFIG_SMP
static int bknative_test_loader_owner(unsigned int cpu,
                                      const void *argument)
{
  struct bkcontrol_status_s status;
  struct bknative_output_s frame;
  const struct bknative_program_s *program = NULL;
  char output[1];
  void *handle = NULL;
  int ret;

  (void)argument;
  if (sched_getcpu() != (int)cpu)
    {
      return -EXDEV;
    }

  ret = bknative_init();
  if (ret != -EXDEV)
    {
      return ret < 0 ? ret : -EPERM;
    }

  ret = bknative_apply("{}");
  if (ret != -EXDEV)
    {
      return ret < 0 ? ret : -EPERM;
    }

  ret = bknative_read(output, sizeof(output));
  if (ret != -EXDEV)
    {
      return ret < 0 ? ret : -EPERM;
    }

  ret = bknative_step(0, false, &frame);
  if (ret != -EXDEV)
    {
      return ret < 0 ? ret : -EPERM;
    }

  ret = bknative_control(BKCONTROL_CONFIG_BEGIN, 0, NULL, 0, &status);
  if (ret != -EXDEV)
    {
      return ret < 0 ? ret : -EPERM;
    }

  ret = bknative_abort(-ECANCELED);
  if (ret != -EXDEV)
    {
      return ret < 0 ? ret : -EPERM;
    }

  ret = bknative_stop();
  if (ret != -EXDEV)
    {
      return ret < 0 ? ret : -EPERM;
    }

  ret = bknative_loader_open(BKNATIVE_TEST_PATH, &handle, &program);
  return ret == -EXDEV && handle == NULL && program == NULL ? 0 :
         ret < 0 ? ret : -EPERM;
}
#endif

static int bknative_test_cpu(unsigned int cpu, const void *argument)
{
  union
  {
    uint64_t alignment;
    uint8_t bytes[BKNATIVE_STATE_MAX];
  } state;

  struct mallinfo before;
  struct mallinfo after;
  uint64_t started;
  int descriptors_before;
  unsigned int completed = 0;
  int observed;
  int ret = 0;
  (void)argument;

  g_bknative_test_trace[1] = 0;
  g_bknative_test_trace[2] = cpu;
  __atomic_store_n(&g_bknative_test_trace[0], 1, __ATOMIC_RELEASE);
  before = mallinfo();
  __atomic_store_n(&g_bknative_test_trace[0], 2, __ATOMIC_RELEASE);
  started = bknative_test_milliseconds();
  __atomic_store_n(&g_bknative_test_trace[0], 3, __ATOMIC_RELEASE);
  descriptors_before = bknative_test_descriptors();
  __atomic_store_n(&g_bknative_test_trace[0], 4, __ATOMIC_RELEASE);

  observed = sched_getcpu();
  if (observed != (int)cpu)
    {
      ret = observed < 0 ? -errno : -EXDEV;
      goto done;
    }

  for (unsigned int cycle = 0; cycle < BKNATIVE_TEST_CYCLES; cycle++)
    {
      const struct bknative_program_s *program;
      struct bknative_frame_s frame;
      void *handle;
      int close_ret;

      __atomic_store_n(&g_bknative_test_trace[0], 10, __ATOMIC_RELEASE);
      ret = bknative_loader_open(BKNATIVE_TEST_PATH, &handle, &program);
      __atomic_store_n(&g_bknative_test_trace[0], 11, __ATOMIC_RELEASE);
      if (ret < 0)
        {
          break;
        }

      memset(&state, 0, sizeof(state));
      memset(&frame, 0, sizeof(frame));
      __atomic_store_n(&g_bknative_test_trace[0], 12, __ATOMIC_RELEASE);
      ret = program->start(state.bytes, 8);
      if (ret == 0)
        {
          ret = program->step(state.bytes, 0, &frame);
          if (ret != 0 || frame.expression != 1)
            {
              ret = -EPROTO;
            }
        }

      if (ret == 0)
        {
          ret = program->step(state.bytes, 4000, &frame);
          if (ret != 0 || frame.expression != 2)
            {
              ret = -EPROTO;
            }
        }

      if (ret == 0 && program->step(state.bytes, 8000, &frame) != 1)
        {
          ret = -EPROTO;
        }

      if (cycle == 0)
        {
          __atomic_store_n(&g_bknative_test_trace[0], 20, __ATOMIC_RELEASE);
          syslog(LOG_INFO, "BKNATIVE_HIL cpu=%u entry=%p start=%p step=%p\n",
                 cpu, program, program->start, program->step);
          syslog(LOG_INFO, "BKNATIVE_HIL cpu=%u code_region=%s\n", cpu,
                 bknative_test_region(program->step));
        }

      __atomic_store_n(&g_bknative_test_trace[0], 30, __ATOMIC_RELEASE);
      close_ret = bknative_loader_close(handle);
      __atomic_store_n(&g_bknative_test_trace[0], 31, __ATOMIC_RELEASE);
      if (ret == 0)
        {
          ret = close_ret;
        }

      if (ret < 0)
        {
          break;
        }

      completed++;
      g_bknative_test_trace[1] = completed;
      if (completed % 25 == 0)
        {
          syslog(LOG_INFO, "BKNATIVE_HIL progress cpu=%u cycles=%u\n",
                 cpu, completed);
        }

      __atomic_store_n(&g_bknative_test_trace[0], 40, __ATOMIC_RELEASE);
      sched_yield();
      __atomic_store_n(&g_bknative_test_trace[0], 41, __ATOMIC_RELEASE);
    }

done:
  __atomic_store_n(&g_bknative_test_trace[0], 50, __ATOMIC_RELEASE);
  after = mallinfo();
  syslog(LOG_INFO,
         "BKNATIVE_HIL %s cpu=%u observed=%d cycles=%u elapsed_ms=%" PRIu64
         " fd=%d/%d heap_used=%u/%u heap_free=%u/%u result=%d\n",
         ret == 0 ? "PASS" : "FAIL", cpu, sched_getcpu(),
         completed,
         bknative_test_milliseconds() - started, descriptors_before,
         bknative_test_descriptors(), before.uordblks, after.uordblks,
         before.fordblks, after.fordblks, ret);
  return ret;
}

static void *bknative_test_cpu_worker(void *argument)
{
  struct bknative_test_call_s *call = argument;
  if (sched_getcpu() != (int)call->cpu)
    {
      call->result = -EXDEV;
      return NULL;
    }

  call->result = call->run(call->cpu, call->argument);
  return NULL;
}

static int bknative_test_on_cpu(unsigned int cpu, const char *phase,
                               int (*run)(unsigned int, const void *),
                               const void *argument)
{
  struct bknative_test_call_s call =
    {
      .run = run,
      .argument = argument,
      .cpu = cpu,
      .result = -EINPROGRESS
    };

  pthread_attr_t attributes;
  pthread_t thread;
  int ret;

  /* Fix placement before the worker starts. Moving the running test thread
   * exercises the port's migration path instead of the ELF loader.
   */

  ret = pthread_attr_init(&attributes);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_attr_setstacksize(&attributes, 8192);
#ifdef CONFIG_SMP
  if (ret == 0)
    {
      cpu_set_t affinity = 0;
      CPU_SET(cpu, &affinity);
      ret = pthread_attr_setaffinity_np(&attributes, sizeof(affinity),
                                        &affinity);
    }
#endif

  if (ret == 0)
    {
      syslog(LOG_INFO, "BKNATIVE_PROBE BEGIN phase=%s cpu=%u caller=%d\n",
             phase, cpu, sched_getcpu());
      usleep(100000);
      ret = pthread_create(&thread, &attributes,
                            bknative_test_cpu_worker, &call);
    }

  pthread_attr_destroy(&attributes);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_join(thread, NULL);
  ret = ret == 0 ? call.result : -ret;
  syslog(LOG_INFO, "BKNATIVE_PROBE %s phase=%s cpu=%u result=%d\n",
         ret == 0 ? "PASS" : "FAIL", phase, cpu, ret);
  return ret;
}

static void *bknative_test_worker(void *argument)
{
  int ret;
  unsigned int cpu_count = 1;
  bool fixture_owned = false;
  (void)argument;

#ifdef CONFIG_SMP
  cpu_count = CONFIG_SMP_NCPUS < 2 ? CONFIG_SMP_NCPUS : 2;
#endif

  /* Let ordinary startup finish before isolating the CPU1 failure. */

  sleep(15);
  ret = 0;
  if (ret == 0)
    {
      ret = bknative_test_on_cpu(cpu_count - 1, "thread",
                             bknative_test_thread, NULL);
    }

  if (ret == 0)
    {
      ret = bknative_test_on_cpu(cpu_count - 1, "yield",
                                 bknative_test_yield, NULL);
    }

#ifdef CONFIG_SMP
  if (ret == 0 && cpu_count == 2)
    {
      syslog(LOG_INFO, "BKNATIVE_PROBE BEGIN phase=contention\n");
      ret = bknative_test_contention_pair();
    }

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRANSPORT
  if (ret == 0 && cpu_count == 2)
    {
      syslog(LOG_INFO, "BKNATIVE_PROBE BEGIN phase=lock-placement\n");
      ret = bknative_test_lock_placement();
    }
#endif
#endif

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRACE
  if (ret == 0)
    {
      ret = bk7258_native_irq_sample_enable(1);
    }

  if (ret == 0)
    {
      bk7258_native_trace_enable(1);
    }
#endif

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRANSPORT
  if (ret == 0)
    {
      ret = bknative_test_write_local_fixture();
    }

  if (ret == 0)
    {
      ret = bknative_test_on_cpu(0, "local-file",
                                 bknative_test_local_file, NULL);
    }

  if (ret == 0 && cpu_count == 2)
    {
      ret = bknative_test_on_cpu(1, "local-file",
                                 bknative_test_local_file, NULL);
    }

  if (unlink(BKNATIVE_LOCAL_TEST_PATH) < 0 && ret == 0)
    {
      ret = -errno;
    }

  if (ret == 0)
    {
      const volatile uint32_t *lock = bk7258_rptun_native_lock_word();

      if (lock == NULL)
        {
          ret = -ENOTCONN;
        }

      if (ret == 0)
        {
          syslog(LOG_INFO,
                 "BKNATIVE_PROBE raw-rpmsg lock=%p value=%08" PRIx32 "\n",
                 lock, *lock);
          ret = bknative_test_on_cpu(0, "raw-rpmsg",
                                     bknative_test_raw_rpmsg, NULL);
        }
    }

  if (ret == 0 && cpu_count == 2)
    {
      ret = bknative_test_on_cpu(1, "raw-rpmsg",
                                 bknative_test_raw_rpmsg, NULL);
    }
#endif

  if (ret == 0)
    {
      ret = bknative_test_write_fixture();
      fixture_owned = ret == 0;
    }

#ifdef CONFIG_SMP
  if (ret == 0 && cpu_count == 2)
    {
      ret = bknative_test_on_cpu(1, "loader-owner-reject",
                                 bknative_test_loader_owner, NULL);
    }
#endif

  if (ret == 0)
    {
      syslog(LOG_INFO, "BKNATIVE_PROBE BEGIN phase=file cpu=0\n");
      ret = bknative_test_file(0, NULL);
      syslog(LOG_INFO, "BKNATIVE_PROBE %s phase=file cpu=0 result=%d\n",
             ret == 0 ? "PASS" : "FAIL", ret);
    }

#ifdef CONFIG_SMP
  if (ret == 0 && cpu_count == 2)
    {
      syslog(LOG_INFO,
             "BKNATIVE_PROBE BEGIN phase=cross owner=0 executor=1\n");
      ret = bknative_test_cross_execution();
    }
#endif

  if (ret == 0)
    {
      syslog(LOG_INFO, "BKNATIVE_PROBE BEGIN phase=loader cpu=0\n");
      ret = bknative_test_cpu(0, NULL);
    }

  if (fixture_owned && unlink(BKNATIVE_TEST_PATH) < 0 && ret == 0)
    {
      ret = -errno;
    }

  if (ret < 0)
    {
      syslog(LOG_ERR, "BKNATIVE_HIL FAIL fixture result=%d\n", ret);
    }

#ifdef CONFIG_TESTS_BK7258_NATIVE_TRACE
  bk7258_native_trace_enable(0);
  bk7258_native_irq_sample_enable(0);
#endif
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_native_app_test_initialize(void)
{
  pthread_t thread;
  pthread_attr_t attributes;
  int ret;

  ret = pthread_mutex_lock(&g_bknative_test_lock);
  if (ret != 0)
    {
      return -ret;
    }

  if (g_bknative_test_started)
    {
      pthread_mutex_unlock(&g_bknative_test_lock);
      return -EALREADY;
    }

  ret = pthread_attr_init(&attributes);
  if (ret == 0)
    {
      /* File/RPMsg and ELF loader frames exceed a trivial worker's budget. */

      ret = pthread_attr_setstacksize(&attributes, 8192);
#ifdef CONFIG_SMP
      if (ret == 0)
        {
          cpu_set_t affinity = 1;
          ret = pthread_attr_setaffinity_np(&attributes, sizeof(affinity),
                                            &affinity);
        }
#endif

      if (ret == 0)
        {
          ret = pthread_create(&thread, &attributes,
                                bknative_test_worker, NULL);
        }

      pthread_attr_destroy(&attributes);
    }

  if (ret == 0)
    {
      g_bknative_test_started = true;
      pthread_detach(thread);
    }

  pthread_mutex_unlock(&g_bknative_test_lock);
  return ret == 0 ? 0 : -ret;
}
