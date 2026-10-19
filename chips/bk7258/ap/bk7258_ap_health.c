/****************************************************************************
 * chips/bk7258/ap/
 * bk7258_ap_health.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N10 AP logical-CPU1 heartbeat publisher.  The physical CPU1/AP logical
 * CPU0 management loop already owns bk7258_ap_boot_state_s::heartbeat.  This
 * permanent task is pinned to logical CPU1 and advances a dedicated health
 * heartbeat.  IPI diagnostics retain the original probe heartbeat and can no
 * longer mask a stalled health task.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_SUPERVISOR

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/sched.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>

#include "arm_internal.h"
#include "bk7258_ap_health.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_AP_HEALTH_CPU            1u
#define BK7258_AP_HEALTH_START_TIMEOUT  1000u
#define BK7258_AP_HEALTH_STALL_FIRST_MS 1000u
#define BK7258_AP_HEALTH_STALL_NEXT_MS  2000u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_bk7258_ap_health_initialized;
static FAR struct tcb_s *g_bk7258_ap_health_tcb[2];
static uint32_t g_bk7258_ap_health_generation;
static uint32_t g_bk7258_ap_health_heartbeat[2];
static clock_t g_bk7258_ap_health_tick[2];
static uint8_t g_bk7258_ap_health_stall_reported[2];

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
          __atomic_fetch_add(
            (uint32_t *)(uintptr_t)&cpu2->heartbeat, 1u,
            __ATOMIC_RELEASE);
          bk7258_ap_health_observe();
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
  FAR struct tcb_s *tcb;
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

  if (up_cpu_index() != 0)
    {
      return -EPERM;
    }

  /* Publish the permanent primary task before the secondary worker starts.
   * Each core observes only its peer, so the diagnostic counters have one
   * writer and a stalled primary can still be inspected by the secondary.
   */

  g_bk7258_ap_health_tcb[0] = this_task();
  g_bk7258_ap_health_generation = boot->generation;

  heartbeat = __atomic_load_n(
                (uint32_t *)(uintptr_t)&cpu2->heartbeat,
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

  /* Both tasks are permanent for the current AP generation.  Retain their
   * TCBs for best-effort, diagnostic-only peer snapshots; the observer never
   * changes them or takes the scheduler lock.
   */

  tcb = nxsched_get_tcb((pid_t)thread);
  if (tcb == NULL)
    {
      return -ESRCH;
    }

  g_bk7258_ap_health_tcb[BK7258_AP_HEALTH_CPU] = tcb;

  /* The AP init coordinator temporarily outranks this task.  Sleep while
   * waiting so logical CPU1 gets a scheduling opportunity and require one
   * observed increment before AP READY is published.
   */

  start = clock_systime_ticks();
  while (__atomic_load_n(
           (uint32_t *)(uintptr_t)&cpu2->heartbeat,
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

void bk7258_ap_health_observe(void)
{
#if defined(CONFIG_SMP) && defined(CONFIG_BK7258_AP_IPI) && \
    defined(CONFIG_BK7258_AP_SMP_SCHED_ONLINE)
  volatile struct bk7258_ap_boot_state_s *boot = bk7258_ap_boot_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  FAR struct tcb_s *tcb;
  FAR struct tcb_s *current;
  FAR struct tcb_s *running;
  clock_t now;
  uint32_t heartbeat;
  uint32_t age_ms;
  uint32_t threshold;
  uint32_t pending0;
  uint32_t pending1;
  unsigned int peer = up_cpu_index() ^ 1u;

  if (peer >= 2 || boot->state != BK7258_AP_STATE_READY ||
      boot->generation != g_bk7258_ap_health_generation)
    {
      return;
    }

  tcb = g_bk7258_ap_health_tcb[peer];
  if (tcb == NULL)
    {
      return;
    }

  heartbeat = __atomic_load_n(peer == 0 ?
                              (uint32_t *)(uintptr_t)&boot->heartbeat :
                              (uint32_t *)(uintptr_t)&cpu2->heartbeat,
                              __ATOMIC_ACQUIRE);
  now = clock_systime_ticks();
  if (heartbeat != g_bk7258_ap_health_heartbeat[peer])
    {
      g_bk7258_ap_health_heartbeat[peer] = heartbeat;
      g_bk7258_ap_health_tick[peer] = now;
      g_bk7258_ap_health_stall_reported[peer] = 0;
      return;
    }

  age_ms = TICK2MSEC((clock_t)(now - g_bk7258_ap_health_tick[peer]));
  threshold = g_bk7258_ap_health_stall_reported[peer] == 0 ?
              BK7258_AP_HEALTH_STALL_FIRST_MS :
              BK7258_AP_HEALTH_STALL_NEXT_MS;
  if (age_ms < threshold || g_bk7258_ap_health_stall_reported[peer] >= 2)
    {
      return;
    }

  current = current_task(peer);
  running = per_cpu_var_smp(g_running_tasks, peer);
  pending0 = bk7258_ap_ipi_smp_pending(0);
  pending1 = bk7258_ap_ipi_smp_pending(BK7258_AP_HEALTH_CPU);
  g_bk7258_ap_health_stall_reported[peer]++;

  syslog(LOG_ERR,
         "AP HEALTH STALL peer=%u hb=%" PRIu32 " age=%" PRIu32
         " tcb=%p state=%u cpu=%u prio=%d lock=%d wd=%u exp=%" PRIu32
         " now=%" PRIu32 " wd_delta=%" PRId32
         " cur=%p run=%p pending=%" PRIu32 "/%" PRIu32
         " aon=%08" PRIx32 "\n",
         peer, heartbeat, age_ms, tcb, (unsigned int)tcb->task_state,
         (unsigned int)tcb->cpu, tcb->sched_priority, tcb->lockcount,
         WDOG_ISACTIVE(&tcb->waitdog) ? 1u : 0u,
         (uint32_t)tcb->waitdog.expired, (uint32_t)now,
         (int32_t)(tcb->waitdog.expired - now), current, running,
         pending0, pending1, getreg32(BK7258_AON_PMU_R3_ADDR));
#endif
}

#endif /* CONFIG_BK7258_AP_SUPERVISOR */
