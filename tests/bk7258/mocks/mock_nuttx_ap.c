/****************************************************************************
 * tests/mocks/mock_nuttx_ap.c
 *
 * Host implementations for the NuttX cache/kmalloc/mutex shims consumed by
 * the AP driver tests.  See the matching headers in mocks/nuttx/.
 ****************************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/cache.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

static size_t g_mock_cache_linesize;

size_t up_get_dcache_linesize(void)
{
  return g_mock_cache_linesize;
}

void mock_cache_set_linesize(size_t linesize)
{
  g_mock_cache_linesize = linesize;
}

struct mock_cache_log_s g_mock_cache_clean;
struct mock_cache_log_s g_mock_cache_flush;
struct mock_cache_log_s g_mock_cache_invalidate;

static void mock_cache_record(struct mock_cache_log_s *log, uintptr_t start,
                              uintptr_t end)
{
  if (log->count < MOCK_CACHE_LOG_MAX)
    {
      log->start[log->count] = start;
      log->end[log->count] = end;
    }

  log->count++;
}

void up_clean_dcache(uintptr_t start, uintptr_t end)
{
  mock_cache_record(&g_mock_cache_clean, start, end);
}

void up_flush_dcache(uintptr_t start, uintptr_t end)
{
  mock_cache_record(&g_mock_cache_flush, start, end);
}

void up_invalidate_dcache(uintptr_t start, uintptr_t end)
{
  mock_cache_record(&g_mock_cache_invalidate, start, end);
}

FAR void *kmm_memalign(size_t alignment, size_t size)
{
  FAR void *ptr = NULL;

  if (alignment < sizeof(void *))
    {
      alignment = sizeof(void *);
    }

  if (posix_memalign(&ptr, alignment, size) != 0)
    {
      return NULL;
    }

  return ptr;
}

void kmm_free(FAR void *ptr)
{
  free(ptr);
}

static int g_mock_mutex_failures;

int nxmutex_lock(FAR mutex_t *mutex)
{
  if (g_mock_mutex_failures > 0)
    {
      g_mock_mutex_failures--;
      return -EAGAIN;
    }

  return pthread_mutex_lock((pthread_mutex_t *)mutex) ? -EINVAL : 0;
}

int nxmutex_unlock(FAR mutex_t *mutex)
{
  return pthread_mutex_unlock((pthread_mutex_t *)mutex) ? -EINVAL : 0;
}

void mock_mutex_fail_next(int failures)
{
  g_mock_mutex_failures = failures;
}