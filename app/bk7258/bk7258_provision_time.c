/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_time.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <netutils/ntpclient.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static bool g_started;
static bool g_pending;
static int g_result = -EAGAIN;
static uint64_t g_utc;
static uint64_t g_monotonic;
static uint64_t monotonic_seconds(void)
{
  struct timespec now;
  return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ? now.tv_sec : 0;
}
static void *worker(void *unused)
{
  (void)unused;
  pthread_mutex_lock(&g_lock);
  for (;;)
    {
      while (!g_pending) pthread_cond_wait(&g_wake, &g_lock);
      pthread_mutex_unlock(&g_lock);
      int status = 0;
      int ret = ntpc_start();
      if (ret >= 0)
        {
          int child;
          do { child = waitpid(ret, &status, 0); } while (child < 0 && errno == EINTR);
          ret = child < 0 ? -errno : !WIFEXITED(status) || WEXITSTATUS(status) != 0 ? -EIO : 0;
        }
      struct ntpc_status_s samples;
      struct timespec wall = {0};
      if (ret == 0) ret = ntpc_status(&samples);
      if (ret == 0 && samples.nsamples == 0) ret = -ENODATA;
      if (ret == 0 && clock_gettime(CLOCK_REALTIME, &wall) < 0) ret = -errno;
      uint64_t stamp = monotonic_seconds();
      if (ret == 0 && (wall.tv_sec < 1704067200 || wall.tv_sec > 4133980799LL)) ret = -ERANGE;
      pthread_mutex_lock(&g_lock);
      g_result = ret;
      g_utc = ret == 0 ? wall.tv_sec : 0;
      g_monotonic = stamp;
      g_pending = false;
    }
  return NULL;
}
int bkprov_time_start(void)
{
  pthread_mutex_lock(&g_lock);
  int ret = 0;
  if (!g_started)
    {
      pthread_t thread;
      pthread_attr_t attr;
      ret = pthread_attr_init(&attr);
      if (ret == 0)
        {
          ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
          size_t stack = 8192;
#ifdef PTHREAD_STACK_MIN
          if (stack < PTHREAD_STACK_MIN) stack = PTHREAD_STACK_MIN;
#endif
          if (ret == 0) ret = pthread_attr_setstacksize(&attr, stack);
          if (ret == 0) ret = pthread_create(&thread, &attr, worker, NULL);
          pthread_attr_destroy(&attr);
        }
      if (ret == 0) g_started = true;
    }
  pthread_mutex_unlock(&g_lock);
  return -ret;
}
int bkprov_time_get(uint64_t minimum_utc, uint64_t *utc)
{
  if (utc == NULL) return -EINVAL;
  uint64_t now = monotonic_seconds();
  pthread_mutex_lock(&g_lock);
  int ret = -EAGAIN;
  if (!g_started) ret = -ENODEV;
  else if (g_pending) ret = -EAGAIN;
  else if (g_result == 0 && now >= g_monotonic && now - g_monotonic < 21600)
    {
      uint64_t value = g_utc + now - g_monotonic;
      if (value < minimum_utc) ret = -ERANGE;
      else { *utc = value; ret = 0; }
    }
  else if (g_result != -EAGAIN && g_result < 0 && now >= g_monotonic && now - g_monotonic < 60)
    ret = g_result;
  else
    {
      g_pending = true;
      pthread_cond_signal(&g_wake);
    }
  pthread_mutex_unlock(&g_lock);
  return ret;
}
