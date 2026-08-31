/****************************************************************************
 * chips/bk7258/cp/
 * bk7258_ap_supervisor.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N10 CP-owned AP liveness supervisor.  It observes three independent level
 * signals: the AP primary management loop, a task pinned to AP logical CPU1,
 * and a generation-safe RPMsg ping/pong endpoint.  It never runs lifecycle
 * work in an ISR or RPMsg callback.  Confirmed faults fail closed by moving
 * the shared RPTUN state to FAULTED; recovery reuses the existing apctl path.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_SUPERVISOR

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_rpmsg_health.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_AP_SUPERVISOR_NAME "bk7258-ap-health"
#define BK7258_AP_TRANSPORT_CP_RX  (1u << 0)
#define BK7258_AP_TRANSPORT_AP_RX  (1u << 1)
#define BK7258_AP_TRANSPORT_BIDIR  (BK7258_AP_TRANSPORT_CP_RX | \
                                    BK7258_AP_TRANSPORT_AP_RX)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_ap_supervisor_s
{
  mutex_t lock;
  bool initialized;
  bool armed;
  bool recovering;
  bool lifecycle;
  bool ready_seen;
  bool invalid_seen;
  bool auto_recover_pending;
  pid_t worker;
  uint32_t last_primary;
  uint32_t last_secondary;
  uint32_t last_cp_rx;
  uint32_t last_ap_rx;
  uint32_t transport_activity;
  clock_t ready_tick;
  clock_t invalid_tick;
  clock_t primary_tick;
  clock_t secondary_tick;
  clock_t transport_tick;
  clock_t last_probe_tick;
  clock_t healthy_tick;
  clock_t sample_tick;
  struct bk7258_ap_supervisor_status_s status;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_ap_supervisor_s g_bk7258_ap_supervisor =
{
  .lock = NXMUTEX_INITIALIZER,
  .worker = INVALID_PROCESS_ID,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t bk7258_ap_supervisor_age_ms(clock_t now, clock_t then)
{
  uint64_t age = TICK2MSEC((clock_t)(now - then));

  return age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
}

static bool bk7258_ap_supervisor_injects(
  const struct bk7258_ap_supervisor_s *priv, uint32_t injection)
{
#ifdef CONFIG_BK7258_AP_SUPERVISOR_FAULT_INJECTION
  return priv->status.injection == injection;
#else
  (void)priv;
  (void)injection;
  return false;
#endif
}

static void bk7258_ap_supervisor_status_initialize(
  struct bk7258_ap_supervisor_s *priv)
{
  memset(&priv->status, 0, sizeof(priv->status));
  priv->status.version = BK7258_AP_SUPERVISOR_STATUS_VERSION;
  priv->status.size = sizeof(priv->status);
  priv->status.state = BK7258_AP_SUPERVISOR_OFFLINE;
#ifdef CONFIG_BK7258_AP_SUPERVISOR_AUTO_RECOVER
  priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_AUTO_RECOVER;
#endif
}

static void bk7258_ap_supervisor_set_offline_locked(
  struct bk7258_ap_supervisor_s *priv, uint32_t generation)
{
  uint32_t saved = priv->status.flags &
                   BK7258_AP_SUPERVISOR_FLAG_FAULT_SAVED;

#ifdef CONFIG_BK7258_AP_SUPERVISOR_AUTO_RECOVER
  saved |= BK7258_AP_SUPERVISOR_FLAG_AUTO_RECOVER;
#endif
  priv->armed = false;
  priv->ready_seen = false;
  priv->invalid_seen = false;
  priv->auto_recover_pending = false;
  priv->status.state = BK7258_AP_SUPERVISOR_OFFLINE;
  priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
  priv->status.generation = generation;
  priv->status.flags = saved;
  priv->status.primary_heartbeat = 0;
  priv->status.secondary_heartbeat = 0;
  priv->status.transport_sequence = 0;
  priv->status.primary_age_ms = 0;
  priv->status.secondary_age_ms = 0;
  priv->status.transport_age_ms = 0;
  priv->status.healthy_age_ms = 0;
  priv->status.injection = BK7258_AP_SUPERVISOR_INJECT_NONE;
  priv->status.last_error = 0;
  priv->last_cp_rx = 0;
  priv->last_ap_rx = 0;
  priv->transport_activity = 0;
  priv->healthy_tick = 0;
}

static void bk7258_ap_supervisor_disarm_locked(
  struct bk7258_ap_supervisor_s *priv, uint32_t generation,
  uint32_t state)
{
  uint32_t saved = priv->status.flags &
                   BK7258_AP_SUPERVISOR_FLAG_FAULT_SAVED;

#ifdef CONFIG_BK7258_AP_SUPERVISOR_AUTO_RECOVER
  saved |= BK7258_AP_SUPERVISOR_FLAG_AUTO_RECOVER;
#endif
  priv->armed = false;
  priv->ready_seen = false;
  priv->invalid_seen = false;
  priv->status.state = state;
  priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
  priv->status.generation = generation;
  priv->status.flags = saved;
  priv->status.primary_age_ms = 0;
  priv->status.secondary_age_ms = 0;
  priv->status.transport_age_ms = 0;
  priv->status.healthy_age_ms = 0;
  priv->status.injection = BK7258_AP_SUPERVISOR_INJECT_NONE;
  priv->status.last_error = 0;
  priv->last_cp_rx = 0;
  priv->last_ap_rx = 0;
  priv->transport_activity = 0;
  priv->healthy_tick = 0;
}

static void bk7258_ap_supervisor_arm_locked(
  struct bk7258_ap_supervisor_s *priv,
  const struct bk7258_ap_boot_state_s *boot,
  const struct bk7258_cpu2_probe_state_s *cpu2,
  const struct bk7258_rptun_control_s *rptun,
  clock_t now)
{
  uint32_t saved = priv->status.flags &
                   BK7258_AP_SUPERVISOR_FLAG_FAULT_SAVED;

#ifdef CONFIG_BK7258_AP_SUPERVISOR_AUTO_RECOVER
  saved |= BK7258_AP_SUPERVISOR_FLAG_AUTO_RECOVER;
#endif
  priv->armed = true;
  priv->invalid_seen = false;
  priv->last_primary = boot->heartbeat;
  priv->last_secondary = cpu2->heartbeat;
  priv->last_cp_rx = rptun->cp_rx_sequence;
  priv->last_ap_rx = rptun->ap_rx_sequence;
  priv->transport_activity = 0;
  priv->primary_tick = now;
  priv->secondary_tick = now;
  priv->transport_tick = now;
  priv->last_probe_tick = now;
  priv->status.state = BK7258_AP_SUPERVISOR_ARMING;
  priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
  priv->status.generation = boot->generation;
  priv->status.flags = saved | BK7258_AP_SUPERVISOR_FLAG_ARMED;
  priv->status.primary_heartbeat = boot->heartbeat;
  priv->status.secondary_heartbeat = cpu2->heartbeat;
  priv->status.transport_sequence = 0;
  priv->status.primary_age_ms = 0;
  priv->status.secondary_age_ms = 0;
  priv->status.transport_age_ms = 0;
  priv->status.healthy_age_ms = 0;
  priv->status.injection = BK7258_AP_SUPERVISOR_INJECT_NONE;
  priv->status.last_error = 0;
  priv->healthy_tick = 0;
}

static bool bk7258_ap_supervisor_transport_activity_locked(
  struct bk7258_ap_supervisor_s *priv,
  volatile struct bk7258_rptun_control_s *rptun, clock_t now)
{
  uint32_t cp_rx;
  uint32_t ap_rx;

  if (rptun->magic != BK7258_RPTUN_CONTROL_MAGIC ||
      rptun->version != BK7258_RPTUN_CONTROL_VERSION ||
      rptun->size != sizeof(*rptun) ||
      rptun->generation != priv->status.generation ||
      rptun->state != BK7258_RPTUN_STATE_CONNECTED)
    {
      return false;
    }

  cp_rx = __atomic_load_n(
    (uint32_t *)(uintptr_t)&rptun->cp_rx_sequence, __ATOMIC_ACQUIRE);
  ap_rx = __atomic_load_n(
    (uint32_t *)(uintptr_t)&rptun->ap_rx_sequence, __ATOMIC_ACQUIRE);

  if (cp_rx != priv->last_cp_rx)
    {
      priv->last_cp_rx = cp_rx;
      priv->transport_activity |= BK7258_AP_TRANSPORT_CP_RX;
    }

  if (ap_rx != priv->last_ap_rx)
    {
      priv->last_ap_rx = ap_rx;
      priv->transport_activity |= BK7258_AP_TRANSPORT_AP_RX;
    }

  if (priv->transport_activity != BK7258_AP_TRANSPORT_BIDIR ||
      bk7258_ap_supervisor_injects(
        priv, BK7258_AP_SUPERVISOR_INJECT_RPMSG))
    {
      return false;
    }

  /* Bidirectional vring progress is stronger evidence than a concurrent
   * best-effort ping: the peer consumed one direction and returned traffic
   * in the other.  Defer the next active probe while application traffic is
   * already proving the same property.
   */

  priv->transport_activity = 0;
  priv->transport_tick = now;
  priv->last_probe_tick = now;
  priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_RPMSG_READY |
                        BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK;
  priv->status.last_error = 0;
  return true;
}

static void bk7258_ap_supervisor_capture_fault_locked(
  struct bk7258_ap_supervisor_s *priv,
  const struct bk7258_ap_fault_state_s *fault,
  const struct bk7258_cpu2_probe_state_s *cpu2,
  uint32_t generation)
{
  if (fault->magic == BK7258_AP_FAULT_STATE_MAGIC &&
      fault->version == BK7258_AP_FAULT_STATE_VERSION &&
      fault->size == sizeof(*fault) &&
      fault->generation == generation && fault->exception != 0)
    {
      priv->status.fault_generation = generation;
      priv->status.fault_exception = fault->exception;
      priv->status.fault_hfsr = fault->hfsr;
      priv->status.fault_cfsr = fault->cfsr;
      priv->status.fault_pc = fault->stacked_pc;
      priv->status.fault_lr = fault->stacked_lr;
      priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_FAULT_SAVED;
    }
  else if (cpu2->magic == BK7258_CPU2_PROBE_STATE_MAGIC &&
           cpu2->version == BK7258_CPU2_PROBE_STATE_VERSION &&
           cpu2->size == sizeof(*cpu2) &&
           cpu2->generation == generation && cpu2->fault_exception != 0)
    {
      priv->status.fault_generation = generation;
      priv->status.fault_exception = cpu2->fault_exception;
      priv->status.fault_hfsr = cpu2->fault_hfsr;
      priv->status.fault_cfsr = cpu2->fault_cfsr;
      priv->status.fault_pc = cpu2->fault_pc;
      priv->status.fault_lr = cpu2->fault_lr;
      priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_FAULT_SAVED;
    }
}

static void bk7258_ap_supervisor_mark_rptun_fault(
  uint32_t generation, int error)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t expected = BK7258_RPTUN_STATE_CONNECTED;

  if (control->magic != BK7258_RPTUN_CONTROL_MAGIC ||
      control->version != BK7258_RPTUN_CONTROL_VERSION ||
      control->size != sizeof(*control) ||
      control->generation != generation)
    {
      return;
    }

  if (__atomic_compare_exchange_n(
        (uint32_t *)(uintptr_t)&control->state, &expected,
        BK7258_RPTUN_STATE_FAULTED, false,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      control->error = error < 0 ? (uint32_t)-error : (uint32_t)error;
      __asm volatile ("dmb sy" ::: "memory");
    }
}

static void bk7258_ap_supervisor_fault_locked(
  struct bk7258_ap_supervisor_s *priv, uint32_t reason, int error,
  const struct bk7258_ap_fault_state_s *fault,
  const struct bk7258_cpu2_probe_state_s *cpu2)
{
  if (priv->status.state == BK7258_AP_SUPERVISOR_FAULTED ||
      priv->status.state == BK7258_AP_SUPERVISOR_LOCKOUT)
    {
      return;
    }

  priv->status.state = BK7258_AP_SUPERVISOR_FAULTED;
  priv->status.reason = reason;
  priv->status.last_error = error;
  priv->status.fault_count++;
  priv->status.consecutive_failures++;
  priv->healthy_tick = 0;
  priv->status.healthy_age_ms = 0;
  bk7258_ap_supervisor_capture_fault_locked(
    priv, fault, cpu2, priv->status.generation);
  bk7258_ap_supervisor_mark_rptun_fault(priv->status.generation, error);

#ifdef CONFIG_BK7258_AP_SUPERVISOR_AUTO_RECOVER
  if (priv->status.consecutive_failures >
      CONFIG_BK7258_AP_SUPERVISOR_MAX_RECOVERIES)
    {
      priv->status.state = BK7258_AP_SUPERVISOR_LOCKOUT;
    }
  else
    {
      priv->auto_recover_pending = true;
    }
#endif
}

static void bk7258_ap_supervisor_evaluate_locked(
  struct bk7258_ap_supervisor_s *priv, clock_t now,
  const struct bk7258_ap_fault_state_s *fault,
  const struct bk7258_cpu2_probe_state_s *cpu2)
{
  uint32_t suspect = CONFIG_BK7258_AP_SUPERVISOR_SUSPECT_MS;
  uint32_t timeout = CONFIG_BK7258_AP_SUPERVISOR_TIMEOUT_MS;
  uint32_t reason = BK7258_AP_SUPERVISOR_REASON_NONE;

  priv->status.primary_age_ms =
    bk7258_ap_supervisor_age_ms(now, priv->primary_tick);
  priv->status.secondary_age_ms =
    bk7258_ap_supervisor_age_ms(now, priv->secondary_tick);
  priv->status.transport_age_ms =
    bk7258_ap_supervisor_age_ms(now, priv->transport_tick);

  if (priv->status.primary_age_ms >= timeout)
    {
      bk7258_ap_supervisor_fault_locked(
        priv, BK7258_AP_SUPERVISOR_REASON_PRIMARY_TIMEOUT,
        -ETIMEDOUT, fault, cpu2);
      return;
    }

  if (priv->status.secondary_age_ms >= timeout)
    {
      bk7258_ap_supervisor_fault_locked(
        priv, BK7258_AP_SUPERVISOR_REASON_SECONDARY_TIMEOUT,
        -ETIMEDOUT, fault, cpu2);
      return;
    }

  if (priv->status.transport_age_ms >= timeout)
    {
      bk7258_ap_supervisor_fault_locked(
        priv, BK7258_AP_SUPERVISOR_REASON_RPMSG_TIMEOUT,
        priv->status.last_error < 0 ?
        priv->status.last_error : -ETIMEDOUT, fault, cpu2);
      return;
    }

  if (priv->status.primary_age_ms >= suspect)
    {
      reason = BK7258_AP_SUPERVISOR_REASON_PRIMARY_TIMEOUT;
    }
  else if (priv->status.secondary_age_ms >= suspect)
    {
      reason = BK7258_AP_SUPERVISOR_REASON_SECONDARY_TIMEOUT;
    }
  else if (priv->status.transport_age_ms >= suspect)
    {
      reason = BK7258_AP_SUPERVISOR_REASON_RPMSG_TIMEOUT;
    }

  if (reason != BK7258_AP_SUPERVISOR_REASON_NONE)
    {
      priv->status.state = BK7258_AP_SUPERVISOR_SUSPECT;
      priv->status.reason = reason;
      priv->healthy_tick = 0;
      priv->status.healthy_age_ms = 0;
    }
  else if ((priv->status.flags &
            (BK7258_AP_SUPERVISOR_FLAG_PRIMARY |
             BK7258_AP_SUPERVISOR_FLAG_SECONDARY |
             BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK)) ==
           (BK7258_AP_SUPERVISOR_FLAG_PRIMARY |
            BK7258_AP_SUPERVISOR_FLAG_SECONDARY |
            BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK))
    {
      priv->status.state = BK7258_AP_SUPERVISOR_HEALTHY;
      priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
      if (priv->healthy_tick == 0)
        {
          priv->healthy_tick = now;
        }
      priv->status.healthy_age_ms =
        bk7258_ap_supervisor_age_ms(now, priv->healthy_tick);
#ifdef CONFIG_BK7258_AP_SUPERVISOR_AUTO_RECOVER
      if (priv->status.healthy_age_ms >=
          CONFIG_BK7258_AP_SUPERVISOR_STABLE_MS)
        {
          priv->status.consecutive_failures = 0;
        }
#endif
    }
  else
    {
      priv->status.state = BK7258_AP_SUPERVISOR_ARMING;
      priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
      priv->healthy_tick = 0;
      priv->status.healthy_age_ms = 0;
    }
}

static bool bk7258_ap_supervisor_shared_valid(
  const struct bk7258_ap_boot_state_s *boot,
  const struct bk7258_cpu2_probe_state_s *cpu2,
  const struct bk7258_rptun_control_s *rptun)
{
  return boot->magic == BK7258_AP_BOOT_STATE_MAGIC &&
         boot->version == BK7258_AP_BOOT_STATE_VERSION &&
         boot->size == sizeof(*boot) && boot->generation != 0 &&
         boot->state == BK7258_AP_STATE_READY &&
         boot->error == BK7258_AP_ERROR_NONE &&
         cpu2->magic == BK7258_CPU2_PROBE_STATE_MAGIC &&
         cpu2->version == BK7258_CPU2_PROBE_STATE_VERSION &&
         cpu2->size == sizeof(*cpu2) &&
         cpu2->generation == boot->generation &&
         rptun->magic == BK7258_RPTUN_CONTROL_MAGIC &&
         rptun->version == BK7258_RPTUN_CONTROL_VERSION &&
         rptun->size == sizeof(*rptun) &&
         rptun->generation == boot->generation &&
         (rptun->flags & (BK7258_RPTUN_FLAG_AP_RPTUN_READY |
                          BK7258_RPTUN_FLAG_AP_READY)) ==
                         (BK7258_RPTUN_FLAG_AP_RPTUN_READY |
                          BK7258_RPTUN_FLAG_AP_READY);
}

static bool bk7258_ap_supervisor_shared_snapshot(
  struct bk7258_ap_boot_state_s *boot,
  struct bk7258_cpu2_probe_state_s *cpu2,
  struct bk7258_ap_fault_state_s *fault,
  struct bk7258_rptun_control_s *rptun)
{
  volatile struct bk7258_ap_boot_state_s *shared_boot =
    bk7258_ap_boot_state();
  volatile struct bk7258_cpu2_probe_state_s *shared_cpu2 =
    bk7258_cpu2_probe_state();
  volatile struct bk7258_ap_fault_state_s *shared_fault =
    bk7258_ap_fault_state();
  volatile struct bk7258_rptun_control_s *shared_rptun =
    bk7258_rptun_control();
  uint32_t boot_generation;
  uint32_t boot_state;
  uint32_t cpu2_generation;
  uint32_t cpu2_state;
  uint32_t rptun_generation;
  uint32_t rptun_state;
  unsigned int attempt;

  for (attempt = 0u; attempt < 3u; attempt++)
    {
      boot_generation = __atomic_load_n(
        (uint32_t *)(uintptr_t)&shared_boot->generation,
        __ATOMIC_ACQUIRE);
      boot_state = __atomic_load_n(
        (uint32_t *)(uintptr_t)&shared_boot->state, __ATOMIC_ACQUIRE);
      cpu2_generation = __atomic_load_n(
        (uint32_t *)(uintptr_t)&shared_cpu2->generation,
        __ATOMIC_ACQUIRE);
      cpu2_state = __atomic_load_n(
        (uint32_t *)(uintptr_t)&shared_cpu2->state, __ATOMIC_ACQUIRE);
      rptun_generation = __atomic_load_n(
        (uint32_t *)(uintptr_t)&shared_rptun->generation,
        __ATOMIC_ACQUIRE);
      rptun_state = __atomic_load_n(
        (uint32_t *)(uintptr_t)&shared_rptun->state, __ATOMIC_ACQUIRE);

      memcpy(boot, (const void *)(uintptr_t)shared_boot, sizeof(*boot));
      memcpy(cpu2, (const void *)(uintptr_t)shared_cpu2, sizeof(*cpu2));
      memcpy(fault, (const void *)(uintptr_t)shared_fault, sizeof(*fault));
      memcpy(rptun, (const void *)(uintptr_t)shared_rptun, sizeof(*rptun));
      __asm volatile ("dmb sy" ::: "memory");

      if (boot_generation == __atomic_load_n(
            (uint32_t *)(uintptr_t)&shared_boot->generation,
            __ATOMIC_ACQUIRE) &&
          boot_state == __atomic_load_n(
            (uint32_t *)(uintptr_t)&shared_boot->state,
            __ATOMIC_ACQUIRE) &&
          cpu2_generation == __atomic_load_n(
            (uint32_t *)(uintptr_t)&shared_cpu2->generation,
            __ATOMIC_ACQUIRE) &&
          cpu2_state == __atomic_load_n(
            (uint32_t *)(uintptr_t)&shared_cpu2->state,
            __ATOMIC_ACQUIRE) &&
          rptun_generation == __atomic_load_n(
            (uint32_t *)(uintptr_t)&shared_rptun->generation,
            __ATOMIC_ACQUIRE) &&
          rptun_state == __atomic_load_n(
            (uint32_t *)(uintptr_t)&shared_rptun->state,
            __ATOMIC_ACQUIRE) &&
          boot->generation == boot_generation &&
          boot->state == boot_state &&
          cpu2->generation == cpu2_generation &&
          cpu2->state == cpu2_state &&
          rptun->generation == rptun_generation &&
          rptun->state == rptun_state)
        {
          return true;
        }
    }

  return false;
}

static void bk7258_ap_supervisor_invalid_locked(
  struct bk7258_ap_supervisor_s *priv, clock_t now,
  uint32_t generation, uint32_t reason, int error,
  const struct bk7258_ap_fault_state_s *fault,
  const struct bk7258_cpu2_probe_state_s *cpu2)
{
  uint32_t age;

  priv->healthy_tick = 0;
  priv->status.healthy_age_ms = 0;

  if (!priv->invalid_seen)
    {
      priv->invalid_seen = true;
      priv->invalid_tick = now;
    }

  age = bk7258_ap_supervisor_age_ms(now, priv->invalid_tick);
  priv->status.state = age >= CONFIG_BK7258_AP_SUPERVISOR_SUSPECT_MS ?
    BK7258_AP_SUPERVISOR_SUSPECT : BK7258_AP_SUPERVISOR_ARMING;
  priv->status.reason = age >= CONFIG_BK7258_AP_SUPERVISOR_SUSPECT_MS ?
    reason : BK7258_AP_SUPERVISOR_REASON_NONE;

  if (age >= CONFIG_BK7258_AP_SUPERVISOR_TIMEOUT_MS)
    {
      if (!priv->armed)
        {
          priv->status.generation = generation;
        }

      bk7258_ap_supervisor_fault_locked(
        priv, reason, error, fault, cpu2);
    }
}

static void bk7258_ap_supervisor_stale_locked(
  struct bk7258_ap_supervisor_s *priv)
{
  priv->healthy_tick = 0;
  priv->status.healthy_age_ms = 0;
  priv->status.flags &= ~(BK7258_AP_SUPERVISOR_FLAG_PRIMARY |
                          BK7258_AP_SUPERVISOR_FLAG_SECONDARY |
                          BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK);
  if (priv->status.state != BK7258_AP_SUPERVISOR_FAULTED &&
      priv->status.state != BK7258_AP_SUPERVISOR_LOCKOUT &&
      priv->status.state != BK7258_AP_SUPERVISOR_RECOVERING)
    {
      priv->status.state = BK7258_AP_SUPERVISOR_ARMING;
      priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
    }
}

static bool bk7258_ap_supervisor_monitor(void)
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;
  struct bk7258_rpmsg_health_result_s probe;
  struct bk7258_ap_boot_state_s boot;
  struct bk7258_cpu2_probe_state_s cpu2;
  struct bk7258_ap_fault_state_s fault;
  struct bk7258_rptun_control_s rptun;
  volatile struct bk7258_rptun_control_s *shared_rptun =
    bk7258_rptun_control();
  clock_t now = clock_systime_ticks();
  bool do_probe = false;
  bool boot_header_valid;
  bool coherent;
  bool transport_active;
  bool valid;
  uint32_t probe_generation;
  int ret;

  coherent = bk7258_ap_supervisor_shared_snapshot(
               &boot, &cpu2, &fault, &rptun);

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return false;
    }

  if (priv->recovering || priv->lifecycle)
    {
      nxmutex_unlock(&priv->lock);
      return true;
    }

  boot_header_valid = coherent &&
    boot.magic == BK7258_AP_BOOT_STATE_MAGIC &&
    boot.version == BK7258_AP_BOOT_STATE_VERSION &&
    boot.size == sizeof(boot) && boot.generation != 0;

  if (boot_header_valid &&
      (boot.state == BK7258_AP_STATE_OFF ||
       boot.state == BK7258_AP_STATE_STOPPED))
    {
      bk7258_ap_supervisor_set_offline_locked(priv, boot.generation);
      nxmutex_unlock(&priv->lock);
      return true;
    }

  /* An all-zero record is the normal pre-start state only while the
   * supervisor has never armed.  If a running generation loses its header,
   * classify it as corrupt shared state instead of silently falling back to
   * OFFLINE.
   */

  if (coherent && boot.magic == 0 && !priv->armed && !priv->ready_seen)
    {
      bk7258_ap_supervisor_set_offline_locked(priv, 0);
      nxmutex_unlock(&priv->lock);
      return true;
    }

  if (!boot_header_valid)
    {
      bk7258_ap_supervisor_invalid_locked(
        priv, now, boot.generation,
        BK7258_AP_SUPERVISOR_REASON_BAD_SHARED_STATE,
        -EPROTO, &fault, &cpu2);
      nxmutex_unlock(&priv->lock);
      return true;
    }

  if (boot.state == BK7258_AP_STATE_FAILED)
    {
      priv->status.generation = boot.generation;
      bk7258_ap_supervisor_fault_locked(
        priv,
        (fault.magic == BK7258_AP_FAULT_STATE_MAGIC &&
         fault.generation == boot.generation && fault.exception != 0) ?
        BK7258_AP_SUPERVISOR_REASON_AP_EXCEPTION :
        BK7258_AP_SUPERVISOR_REASON_AP_REPORTED_FAILURE,
        boot.error == 0 ? -EIO : -(int)boot.error, &fault, &cpu2);
      nxmutex_unlock(&priv->lock);
      return true;
    }

  /* A confirmed fault remains fail-closed until a new generation appears or
   * recovery is explicitly requested.  Do not let later live shared counters
   * downgrade FAULTED to SUSPECT.
   */

  if (priv->status.generation == boot.generation &&
      (priv->status.state == BK7258_AP_SUPERVISOR_FAULTED ||
       priv->status.state == BK7258_AP_SUPERVISOR_LOCKOUT))
    {
      nxmutex_unlock(&priv->lock);
      return true;
    }

  /* AP startup can legitimately spend tens of seconds in the preserved N8
   * validation gates.  Its existing start deadline owns that phase; N10 arms
   * only after AP READY, avoiding a false crash during cold boot.
   */

  if (boot.state != BK7258_AP_STATE_READY)
    {
      if (priv->armed && priv->status.generation == boot.generation)
        {
          bk7258_ap_supervisor_invalid_locked(
            priv, now, boot.generation,
            BK7258_AP_SUPERVISOR_REASON_BAD_SHARED_STATE,
            -EPROTO, &fault, &cpu2);
        }
      else
        {
          bk7258_ap_supervisor_disarm_locked(
            priv, boot.generation, BK7258_AP_SUPERVISOR_ARMING);
        }

      nxmutex_unlock(&priv->lock);
      return true;
    }

  if (!priv->ready_seen || priv->status.generation != boot.generation)
    {
      priv->ready_seen = true;
      priv->ready_tick = now;
      priv->armed = false;
      priv->invalid_seen = false;
      priv->status.state = BK7258_AP_SUPERVISOR_ARMING;
      priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
      priv->status.generation = boot.generation;
      priv->healthy_tick = 0;
      priv->status.healthy_age_ms = 0;
    }

  /* QUIESCING is a CP-authored lifecycle transition.  Disarm while apctl
   * stops/restarts the AP so the ordinary stop path cannot be misclassified
   * as an RPTUN crash before it publishes STOPPED or a new generation.
   */

  if (rptun.magic == BK7258_RPTUN_CONTROL_MAGIC &&
      rptun.version == BK7258_RPTUN_CONTROL_VERSION &&
      rptun.size == sizeof(rptun) &&
      rptun.generation == boot.generation &&
      rptun.state == BK7258_RPTUN_STATE_QUIESCING)
    {
      bk7258_ap_supervisor_disarm_locked(
        priv, boot.generation, BK7258_AP_SUPERVISOR_ARMING);
      nxmutex_unlock(&priv->lock);
      return true;
    }

  valid = coherent &&
          bk7258_ap_supervisor_shared_valid(&boot, &cpu2, &rptun);
  if (!valid ||
      cpu2.state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE ||
      rptun.state != BK7258_RPTUN_STATE_CONNECTED)
    {
      uint32_t reason;
      int error;

      if (!valid)
        {
          reason = BK7258_AP_SUPERVISOR_REASON_BAD_SHARED_STATE;
          error = -EPROTO;
        }
      else if (cpu2.state != BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE)
        {
          reason = cpu2.state == BK7258_CPU2_PROBE_STATE_FAILED ?
            BK7258_AP_SUPERVISOR_REASON_AP_REPORTED_FAILURE :
            BK7258_AP_SUPERVISOR_REASON_SECONDARY_TIMEOUT;
          error = cpu2.error == 0 ? -EHOSTDOWN : -(int)cpu2.error;
        }
      else
        {
          reason = BK7258_AP_SUPERVISOR_REASON_RPTUN_DISCONNECTED;
          error = rptun.error == 0 ? -ENOTCONN : -(int)rptun.error;
        }

      if (!priv->armed && !priv->invalid_seen)
        {
          priv->invalid_seen = true;
          priv->invalid_tick = priv->ready_tick;
        }

      bk7258_ap_supervisor_invalid_locked(
        priv, now, boot.generation, reason, error, &fault, &cpu2);
      nxmutex_unlock(&priv->lock);
      return true;
    }

  priv->invalid_seen = false;

  if (!priv->armed || priv->status.generation != boot.generation)
    {
      bk7258_ap_supervisor_arm_locked(priv, &boot, &cpu2, &rptun, now);
    }

  shared_rptun->cp_epoch = boot.generation;
  __atomic_fetch_add(
    (uint32_t *)(uintptr_t)&shared_rptun->cp_heartbeat, 1u,
    __ATOMIC_RELEASE);

  priv->status.primary_heartbeat = boot.heartbeat;
  priv->status.secondary_heartbeat = cpu2.heartbeat;
  if (boot.heartbeat != priv->last_primary)
    {
      priv->last_primary = boot.heartbeat;
      if (!bk7258_ap_supervisor_injects(
            priv, BK7258_AP_SUPERVISOR_INJECT_PRIMARY))
        {
          priv->primary_tick = now;
          priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_PRIMARY;
        }
    }

  if (cpu2.heartbeat != priv->last_secondary)
    {
      priv->last_secondary = cpu2.heartbeat;
      if (!bk7258_ap_supervisor_injects(
            priv, BK7258_AP_SUPERVISOR_INJECT_SECONDARY))
        {
          priv->secondary_tick = now;
          priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_SECONDARY;
        }
    }

  if (bk7258_rpmsg_health_ready())
    {
      priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_RPMSG_READY;
      transport_active =
        bk7258_ap_supervisor_transport_activity_locked(
          priv, shared_rptun, now);
    }
  else
    {
      priv->status.flags &= ~(BK7258_AP_SUPERVISOR_FLAG_RPMSG_READY |
                              BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK);
      transport_active = false;
    }

  if (bk7258_ap_supervisor_injects(
        priv, BK7258_AP_SUPERVISOR_INJECT_RPMSG))
    {
      priv->status.last_error = -ETIMEDOUT;
    }
  else if ((priv->status.flags &
            BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK) == 0 ||
           (clock_t)(now - priv->last_probe_tick) >=
             MSEC2TICK(CONFIG_BK7258_AP_HEALTH_PROBE_INTERVAL_MS))
    {
      priv->last_probe_tick = now;
      do_probe = true;
    }

  bk7258_ap_supervisor_evaluate_locked(priv, now, &fault, &cpu2);
  nxmutex_unlock(&priv->lock);

  if (!do_probe)
    {
      return true;
    }

  memset(&probe, 0, sizeof(probe));
  probe_generation = boot.generation;
  ret = bk7258_rpmsg_health_probe(
          probe_generation, CONFIG_BK7258_AP_HEALTH_PROBE_TIMEOUT_MS,
          &probe);
  coherent = bk7258_ap_supervisor_shared_snapshot(
               &boot, &cpu2, &fault, &rptun);
  now = clock_systime_ticks();
  if (!coherent)
    {
      if (nxmutex_lock(&priv->lock) >= 0)
        {
          bk7258_ap_supervisor_stale_locked(priv);
          nxmutex_unlock(&priv->lock);
          return true;
        }
      return false;
    }

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return false;
    }

  if (!priv->recovering && !priv->lifecycle && priv->armed &&
      priv->status.generation == probe_generation &&
      boot.generation == probe_generation &&
      bk7258_ap_supervisor_shared_valid(&boot, &cpu2, &rptun) &&
      cpu2.state == BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE &&
      rptun.state == BK7258_RPTUN_STATE_CONNECTED &&
      priv->status.state != BK7258_AP_SUPERVISOR_FAULTED &&
      priv->status.state != BK7258_AP_SUPERVISOR_LOCKOUT)
    {
      transport_active =
        bk7258_ap_supervisor_transport_activity_locked(
          priv, shared_rptun, now);
      if (bk7258_ap_supervisor_injects(
            priv, BK7258_AP_SUPERVISOR_INJECT_RPMSG))
        {
          priv->status.last_error = -ETIMEDOUT;
        }
      else if (ret >= 0 && probe.generation == probe_generation)
        {
          if (!transport_active)
            {
              priv->transport_tick = now;
              priv->status.transport_sequence = probe.sequence;
              priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_RPMSG_READY |
                                    BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK;
            }

          priv->status.last_error = 0;
          priv->last_cp_rx = __atomic_load_n(
            (uint32_t *)(uintptr_t)&shared_rptun->cp_rx_sequence,
            __ATOMIC_ACQUIRE);
          priv->last_ap_rx = __atomic_load_n(
            (uint32_t *)(uintptr_t)&shared_rptun->ap_rx_sequence,
            __ATOMIC_ACQUIRE);
          priv->transport_activity = 0;
        }
      else if (!transport_active)
        {
          /* Application traffic completing in both directions while the
           * best-effort probe is pending is stronger evidence than a
           * transient probe allocation timeout.  Only record the probe
           * error when no such traffic was observed.
           */

          priv->status.last_error = ret < 0 ? ret : -ESTALE;
        }

      bk7258_ap_supervisor_evaluate_locked(priv, now, &fault, &cpu2);
    }
  else
    {
      bk7258_ap_supervisor_stale_locked(priv);
    }

  nxmutex_unlock(&priv->lock);
  return true;
}

static int bk7258_ap_supervisor_recover_internal(uint32_t timeout_ms,
                                                 bool manual)
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->initialized)
    {
      nxmutex_unlock(&priv->lock);
      return -EAGAIN;
    }

  if (priv->recovering)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  if (priv->lifecycle)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  if (!manual && priv->status.state == BK7258_AP_SUPERVISOR_LOCKOUT)
    {
      nxmutex_unlock(&priv->lock);
      return -ECANCELED;
    }

  if (manual &&
      priv->status.state != BK7258_AP_SUPERVISOR_FAULTED &&
      priv->status.state != BK7258_AP_SUPERVISOR_SUSPECT &&
      priv->status.state != BK7258_AP_SUPERVISOR_LOCKOUT)
    {
      nxmutex_unlock(&priv->lock);
      return -EALREADY;
    }

  priv->recovering = true;
  priv->auto_recover_pending = false;
  priv->status.injection = BK7258_AP_SUPERVISOR_INJECT_NONE;
  priv->status.flags &= ~BK7258_AP_SUPERVISOR_FLAG_INJECTED;
  priv->status.state = BK7258_AP_SUPERVISOR_RECOVERING;
  priv->healthy_tick = 0;
  priv->status.healthy_age_ms = 0;
  nxmutex_unlock(&priv->lock);

  ret = bk7258_ap_restart(timeout_ms);

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return ret;
    }

  priv->recovering = false;
  priv->armed = false;
  priv->ready_seen = false;
  priv->invalid_seen = false;
  priv->healthy_tick = 0;
  priv->status.healthy_age_ms = 0;
  if (ret >= 0)
    {
      priv->status.recovery_count++;
      priv->status.state = BK7258_AP_SUPERVISOR_ARMING;
      priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
      priv->status.last_error = 0;
      if (manual)
        {
          priv->status.consecutive_failures = 0;
        }
    }
  else
    {
      priv->status.state = BK7258_AP_SUPERVISOR_FAULTED;
      priv->status.reason = BK7258_AP_SUPERVISOR_REASON_RECOVERY_FAILED;
      priv->status.last_error = ret;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static int bk7258_ap_supervisor_worker(int argc, char *argv[])
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;

  (void)argc;
  (void)argv;
  for (;;)
    {
      bool recover = false;
      bool sampled;

      sampled = bk7258_ap_supervisor_monitor();
      if (nxmutex_lock(&priv->lock) >= 0)
        {
#ifdef CONFIG_BK7258_AP_SUPERVISOR_AUTO_RECOVER
          recover = priv->auto_recover_pending && !priv->recovering;
          priv->auto_recover_pending = false;
#endif
          if (sampled)
            {
              priv->sample_tick = clock_systime_ticks();
              if (++priv->status.sample_sequence == 0u)
                {
                  priv->status.sample_sequence++;
                }
            }
          nxmutex_unlock(&priv->lock);
        }

#ifdef CONFIG_BK7258_AP_SUPERVISOR_AUTO_RECOVER
      if (recover)
        {
          (void)bk7258_ap_supervisor_recover_internal(
            BK7258_AP_DEFAULT_TIMEOUT_MS, false);
        }
#else
      (void)recover;
#endif
      nxsig_usleep((unsigned int)CONFIG_BK7258_AP_SUPERVISOR_POLL_MS *
                   1000u);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_supervisor_initialize(void)
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->initialized)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  bk7258_ap_supervisor_status_initialize(priv);
  priv->status.sample_age_ms = UINT32_MAX;
  priv->initialized = true;
  priv->worker = kthread_create(
    BK7258_AP_SUPERVISOR_NAME, CONFIG_BK7258_AP_SUPERVISOR_PRIORITY,
    CONFIG_BK7258_AP_SUPERVISOR_STACKSIZE,
    bk7258_ap_supervisor_worker, NULL);
  if (priv->worker < 0)
    {
      ret = priv->worker;
      priv->initialized = false;
      priv->worker = INVALID_PROCESS_ID;
    }
  else
    {
      ret = OK;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_ap_supervisor_get_status(
  struct bk7258_ap_supervisor_status_s *status)
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;
  int ret;

  if (status == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->initialized)
    {
      ret = -EAGAIN;
    }
  else
    {
      clock_t now = clock_systime_ticks();

      memcpy(status, &priv->status, sizeof(*status));
      status->sample_age_ms = priv->sample_tick == 0 ? UINT32_MAX :
        bk7258_ap_supervisor_age_ms(now, priv->sample_tick);
      ret = OK;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_ap_supervisor_health_token(
  uint32_t expected_generation, uint32_t max_age_ms,
  struct bk7258_ap_supervisor_health_token_s *token)
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;
  const uint32_t required = BK7258_AP_SUPERVISOR_FLAG_ARMED |
                            BK7258_AP_SUPERVISOR_FLAG_PRIMARY |
                            BK7258_AP_SUPERVISOR_FLAG_SECONDARY |
                            BK7258_AP_SUPERVISOR_FLAG_RPMSG_READY |
                            BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK;
  uint32_t sample_age;
  clock_t now;
  int ret;

  if (max_age_ms == 0u || token == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  now = clock_systime_ticks();
  sample_age = priv->sample_tick == 0 ? UINT32_MAX :
    bk7258_ap_supervisor_age_ms(now, priv->sample_tick);
  if (!priv->initialized || priv->worker < 0 ||
      priv->status.generation == 0u)
    {
      ret = -EAGAIN;
    }
  else if (expected_generation != 0u &&
           priv->status.generation != expected_generation)
    {
      ret = -ESTALE;
    }
  else if (priv->status.state != BK7258_AP_SUPERVISOR_HEALTHY ||
           (priv->status.flags & required) != required ||
           sample_age > max_age_ms)
    {
      ret = -EAGAIN;
    }
  else
    {
      token->generation = priv->status.generation;
      token->sample_sequence = priv->status.sample_sequence;
      token->flags = priv->status.flags;
      token->healthy_age_ms = priv->status.healthy_age_ms;
      token->sample_age_ms = sample_age;
      ret = OK;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_ap_supervisor_recover(uint32_t timeout_ms)
{
  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_DEFAULT_TIMEOUT_MS;
    }

  return bk7258_ap_supervisor_recover_internal(timeout_ms, true);
}

void bk7258_ap_supervisor_lifecycle_begin(void)
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return;
    }

  if (priv->initialized)
    {
      priv->lifecycle = true;
      if (!priv->recovering &&
          priv->status.state != BK7258_AP_SUPERVISOR_FAULTED &&
          priv->status.state != BK7258_AP_SUPERVISOR_LOCKOUT)
        {
          bk7258_ap_supervisor_disarm_locked(
            priv, priv->status.generation,
            BK7258_AP_SUPERVISOR_ARMING);
        }
    }

  nxmutex_unlock(&priv->lock);
}

void bk7258_ap_supervisor_lifecycle_end(void)
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return;
    }

  priv->lifecycle = false;
  priv->invalid_seen = false;
  nxmutex_unlock(&priv->lock);
}

#ifdef CONFIG_BK7258_AP_SUPERVISOR_FAULT_INJECTION
int bk7258_ap_supervisor_inject(uint32_t injection)
{
  struct bk7258_ap_supervisor_s *priv = &g_bk7258_ap_supervisor;
  volatile struct bk7258_ap_boot_state_s *boot =
    bk7258_ap_boot_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  clock_t now = clock_systime_ticks();
  int ret;

  if (injection > BK7258_AP_SUPERVISOR_INJECT_RPMSG)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->initialized)
    {
      ret = -EAGAIN;
    }
  else if (priv->recovering ||
           priv->status.state == BK7258_AP_SUPERVISOR_FAULTED ||
           priv->status.state == BK7258_AP_SUPERVISOR_LOCKOUT)
    {
      ret = -EBUSY;
    }
  else if (injection == BK7258_AP_SUPERVISOR_INJECT_NONE)
    {
      if (priv->status.injection == BK7258_AP_SUPERVISOR_INJECT_NONE)
        {
          ret = -EALREADY;
        }
      else
        {
          __asm volatile ("dmb sy" ::: "memory");
          priv->last_primary = boot->heartbeat;
          priv->last_secondary = cpu2->heartbeat;
          priv->primary_tick = now;
          priv->secondary_tick = now;
          priv->transport_tick = now;
          priv->last_probe_tick = now;
          priv->status.injection = BK7258_AP_SUPERVISOR_INJECT_NONE;
          priv->status.flags &=
            ~(BK7258_AP_SUPERVISOR_FLAG_INJECTED |
              BK7258_AP_SUPERVISOR_FLAG_PRIMARY |
              BK7258_AP_SUPERVISOR_FLAG_SECONDARY |
              BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK);
          priv->status.state = BK7258_AP_SUPERVISOR_ARMING;
          priv->status.reason = BK7258_AP_SUPERVISOR_REASON_NONE;
          priv->status.last_error = 0;
          priv->healthy_tick = 0;
          priv->status.healthy_age_ms = 0;
          ret = OK;
        }
    }
  else if (priv->status.state != BK7258_AP_SUPERVISOR_HEALTHY ||
           priv->status.injection != BK7258_AP_SUPERVISOR_INJECT_NONE)
    {
      ret = -EAGAIN;
    }
  else
    {
      priv->status.injection = injection;
      priv->status.flags |= BK7258_AP_SUPERVISOR_FLAG_INJECTED;
      if (injection == BK7258_AP_SUPERVISOR_INJECT_PRIMARY)
        {
          priv->primary_tick = now;
        }
      else if (injection == BK7258_AP_SUPERVISOR_INJECT_SECONDARY)
        {
          priv->secondary_tick = now;
        }
      else
        {
          priv->transport_tick = now;
        }

      ret = OK;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}
#endif

#endif /* CONFIG_BK7258_AP_SUPERVISOR */
