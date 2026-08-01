/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/
 * bk7258_ap_health.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N10 AP logical-CPU1 heartbeat publisher.  The physical CPU1/AP logical
 * CPU0 management loop already owns bk7258_ap_boot_state_s::heartbeat.  This
 * permanent task is pinned to logical CPU1 and advances the existing CPU2
 * probe heartbeat so CP can distinguish primary progress from secondary
 * scheduler progress without extending either shared-memory ABI.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_SUPERVISOR

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>

#include "bk7258_ap_health.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_AP_HEALTH_CPU            1u
#define BK7258_AP_HEALTH_START_TIMEOUT  1000u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_bk7258_ap_health_initialized;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static FAR void *bk7258_ap_health_worker(FAR void *arg)
{
  volatile struct bk7258_ap_boot_state_s *boot = bk7258_ap_boot_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  uint32_t generation = (uint32_t)(uintptr_t)arg;

  for (;;)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (boot->generation != generation)
        {
          break;
        }

      if (up_cpu_index() == BK7258_AP_HEALTH_CPU &&
          cpu2->generation == generation &&
          cpu2->state == BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE)
        {
          __atomic_fetch_add((uint32_t *)(uintptr_t)&cpu2->heartbeat, 1u,
                             __ATOMIC_RELEASE);
        }

      nxsig_usleep((unsigned int)CONFIG_BK7258_AP_HEARTBEAT_PERIOD_MS *
                   1000u);
    }

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_health_initialize(void)
{
  volatile struct bk7258_ap_boot_state_s *boot = bk7258_ap_boot_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  struct sched_param param;
  pthread_attr_t attr;
  pthread_t thread;
  cpu_set_t cpuset = (cpu_set_t)(1u << BK7258_AP_HEALTH_CPU);
  clock_t start;
  uint32_t heartbeat;
  bool attr_initialized = false;
  int ret;

  if (g_bk7258_ap_health_initialized)
    {
      return OK;
    }

  if (boot->generation == 0 ||
      cpu2->generation != boot->generation ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE)
    {
      return -ESTALE;
    }

  heartbeat = __atomic_load_n((uint32_t *)(uintptr_t)&cpu2->heartbeat,
                              __ATOMIC_ACQUIRE);
  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      attr_initialized = true;
      ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(
              &attr, CONFIG_BK7258_AP_SUPERVISOR_STACKSIZE);
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
      param.sched_priority = CONFIG_BK7258_AP_SUPERVISOR_PRIORITY;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attr, bk7258_ap_health_worker,
                           (FAR void *)(uintptr_t)boot->generation);
    }

  if (attr_initialized)
    {
      (void)pthread_attr_destroy(&attr);
    }

  if (ret != 0)
    {
      return -ret;
    }

  /* The AP init coordinator temporarily outranks this task.  Sleep while
   * waiting so logical CPU1 gets a scheduling opportunity and require one
   * observed increment before AP READY is published.
   */

  start = clock_systime_ticks();
  while (__atomic_load_n((uint32_t *)(uintptr_t)&cpu2->heartbeat,
                         __ATOMIC_ACQUIRE) == heartbeat)
    {
      if ((clock_t)(clock_systime_ticks() - start) >=
          MSEC2TICK(BK7258_AP_HEALTH_START_TIMEOUT))
        {
          return -ETIMEDOUT;
        }

      nxsig_usleep(1000);
    }

  g_bk7258_ap_health_initialized = true;
  return OK;
}

#endif /* CONFIG_BK7258_AP_SUPERVISOR */
