/****************************************************************************
 * contest2026_135_yongwangzhiqian/boards/bk7258/common/src/
 * bk7258_ota_trial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned policy for one MCUboot pending CP/AP generation.  The policy
 * consumes a fresh CP/AP Supervisor health token, never source or download
 * state, and binds confirmation to the exact active slot/version/counter.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_ota.h>
#include <arch/board/board.h>

#define BK7258_OTA_TRIAL_NAME "bk7258-ota-trial"

_Static_assert(CONFIG_BK7258_OTA_TRIAL_PRIORITY <
               CONFIG_BK7258_AP_SUPERVISOR_PRIORITY,
               "OTA trial policy must run below the AP Supervisor");
_Static_assert(CONFIG_BK7258_OTA_TRIAL_TIMEOUT_MS >
               CONFIG_BK7258_OTA_TRIAL_STABLE_MS +
               2 * CONFIG_BK7258_OTA_TRIAL_POLL_MS,
               "OTA trial timeout must exceed the stable window");
_Static_assert(CONFIG_BK7258_OTA_TRIAL_HEALTH_MAX_AGE_MS >=
               CONFIG_BK7258_AP_SUPERVISOR_POLL_MS +
               CONFIG_BK7258_AP_HEALTH_PROBE_TIMEOUT_MS,
               "OTA trial health age must allow one Supervisor probe");
_Static_assert(CONFIG_BK7258_OTA_TRIAL_CONFIRM_TIMEOUT_MS > 5000,
               "OTA confirmation lease must exceed the Flash lock timeout");

struct bk7258_ota_trial_s
{
  mutex_t lock;
  bool initialized;
  pid_t worker;
  pid_t deadline_worker;
  clock_t started;
  clock_t policy_tick;
  clock_t deadline_tick;
  clock_t confirm_tick;
  struct bk7258_ota_pair_snapshot_s expected;
  struct bk7258_ota_trial_status_s status;
};

static struct bk7258_ota_trial_s g_bk7258_ota_trial =
{
  .lock = NXMUTEX_INITIALIZER,
  .worker = INVALID_PROCESS_ID,
  .deadline_worker = INVALID_PROCESS_ID,
};

static uint32_t bk7258_ota_trial_age_ms(clock_t now, clock_t then)
{
  uint64_t age = TICK2MSEC((clock_t)(now - then));

  return age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
}

static bool bk7258_ota_trial_pair_equal(
  const struct bk7258_ota_pair_snapshot_s *left,
  const struct bk7258_ota_pair_snapshot_s *right)
{
  return left->active_slot == right->active_slot &&
         left->security_counter_present == right->security_counter_present &&
         left->security_counter == right->security_counter &&
         bk7258_mcuboot_version_equal(&left->version, &right->version);
}

static void bk7258_ota_trial_status_locked(
  struct bk7258_ota_trial_s *priv, uint32_t state, int error,
  uint32_t generation, uint32_t sequence, uint32_t elapsed,
  uint32_t stable_age)
{
  priv->status.state = state;
  priv->status.last_error = error;
  priv->status.supervisor_generation = generation;
  priv->status.sample_sequence = sequence;
  priv->status.elapsed_ms = elapsed;
  priv->status.stable_age_ms = stable_age;
}

static void bk7258_ota_trial_reset(
  struct bk7258_ota_trial_s *priv, int error) __attribute__((noreturn));

static void bk7258_ota_trial_reset(
  struct bk7258_ota_trial_s *priv, int error)
{
  if (nxmutex_lock(&priv->lock) >= 0)
    {
      bk7258_ota_trial_status_locked(
        priv, BK7258_OTA_TRIAL_RESETTING, error,
        priv->status.supervisor_generation,
        priv->status.sample_sequence,
        bk7258_ota_trial_age_ms(clock_systime_ticks(), priv->started),
        priv->status.stable_age_ms);
      nxmutex_unlock(&priv->lock);
    }

  syslog(LOG_ERR, "BOTA TRIAL RESET error=%d\n", error);
  bk7258_ota_system_reset();
}

static int bk7258_ota_trial_worker(int argc, char *argv[])
{
  struct bk7258_ota_trial_s *priv = &g_bk7258_ota_trial;
  struct bk7258_ap_supervisor_health_token_s token;
  struct bk7258_ap_supervisor_status_s supervisor;
  struct bk7258_ota_pair_snapshot_s current;
  uint32_t supervisor_generation = 0u;
  clock_t stable_tick = 0;
  bool stable_started = false;

  (void)argc;
  (void)argv;
  for (;;)
    {
      clock_t now = clock_systime_ticks();
      uint32_t elapsed = bk7258_ota_trial_age_ms(now, priv->started);
      uint32_t stable_age = 0u;
      bool confirm = false;
      int ret;

      if (nxmutex_lock(&priv->lock) >= 0)
        {
          uint32_t deadline_age = bk7258_ota_trial_age_ms(
                                    now, priv->deadline_tick);

          priv->policy_tick = now;
          nxmutex_unlock(&priv->lock);
          if (deadline_age > CONFIG_BK7258_OTA_TRIAL_HEALTH_MAX_AGE_MS)
            {
              bk7258_ota_trial_reset(priv, -EHOSTDOWN);
            }
        }

      if (elapsed >= CONFIG_BK7258_OTA_TRIAL_TIMEOUT_MS)
        {
          bk7258_ota_trial_reset(priv, -ETIMEDOUT);
        }

      memset(&current, 0, sizeof(current));
      ret = bk7258_ota_get_active_pair(&current);
      if (ret == 0 && current.state == BK7258_OTA_PAIR_CONFIRMED &&
          bk7258_ota_trial_pair_equal(&current, &priv->expected))
        {
          if (nxmutex_lock(&priv->lock) >= 0)
            {
              bk7258_ota_trial_status_locked(
                priv, BK7258_OTA_TRIAL_CONFIRMED, 0,
                supervisor_generation, priv->status.sample_sequence,
                elapsed, priv->status.stable_age_ms);
              priv->worker = INVALID_PROCESS_ID;
              nxmutex_unlock(&priv->lock);
            }
          return OK;
        }

      if (ret == 0 &&
          (current.state != BK7258_OTA_PAIR_PENDING ||
           !bk7258_ota_trial_pair_equal(&current, &priv->expected)))
        {
          bk7258_ota_trial_reset(priv, -ESTALE);
        }

      if (ret < 0)
        {
          stable_started = false;
          if (nxmutex_lock(&priv->lock) >= 0)
            {
              bk7258_ota_trial_status_locked(
                priv, BK7258_OTA_TRIAL_WAITING_HEALTH, ret,
                supervisor_generation, priv->status.sample_sequence,
                elapsed, 0u);
              nxmutex_unlock(&priv->lock);
            }
          goto sleep;
        }

      memset(&supervisor, 0, sizeof(supervisor));
      ret = bk7258_ap_supervisor_get_status(&supervisor);
      if (ret == 0 && supervisor.generation != 0u)
        {
          if (supervisor_generation == 0u)
            {
              supervisor_generation = supervisor.generation;
            }
          else if (supervisor.generation != supervisor_generation)
            {
              bk7258_ota_trial_reset(priv, -ESTALE);
            }
        }

      memset(&token, 0, sizeof(token));
      ret = bk7258_ap_supervisor_health_token(
              supervisor_generation,
              CONFIG_BK7258_OTA_TRIAL_HEALTH_MAX_AGE_MS, &token);
      if (ret == 0)
        {
          if (supervisor_generation == 0u)
            {
              supervisor_generation = token.generation;
            }
          if (token.generation != supervisor_generation)
            {
              bk7258_ota_trial_reset(priv, -ESTALE);
            }

          if (!stable_started)
            {
              stable_tick = now;
              stable_started = true;
            }
          stable_age = bk7258_ota_trial_age_ms(now, stable_tick);
          confirm = stable_age >= CONFIG_BK7258_OTA_TRIAL_STABLE_MS;
          if (nxmutex_lock(&priv->lock) >= 0)
            {
              bk7258_ota_trial_status_locked(
                priv, confirm ? BK7258_OTA_TRIAL_CONFIRMING :
                                BK7258_OTA_TRIAL_STABLE,
                0, supervisor_generation, token.sample_sequence,
                elapsed, stable_age);
              if (confirm)
                {
                  priv->confirm_tick = now;
                }
              nxmutex_unlock(&priv->lock);
            }
        }
      else
        {
          if (ret == -ESTALE && supervisor_generation != 0u)
            {
              bk7258_ota_trial_reset(priv, ret);
            }
          stable_started = false;
          if (nxmutex_lock(&priv->lock) >= 0)
            {
              bk7258_ota_trial_status_locked(
                priv, BK7258_OTA_TRIAL_WAITING_HEALTH,
                ret == -EAGAIN ? 0 : ret, supervisor_generation,
                supervisor.sample_sequence, elapsed, 0u);
              nxmutex_unlock(&priv->lock);
            }
        }

      if (confirm)
        {
          ret = bk7258_ota_confirm_pair_health(
                  &priv->expected, &token,
                  CONFIG_BK7258_OTA_TRIAL_HEALTH_MAX_AGE_MS);
          if (ret == 0 || ret == -EALREADY)
            {
              if (nxmutex_lock(&priv->lock) >= 0)
                {
                  bk7258_ota_trial_status_locked(
                    priv, BK7258_OTA_TRIAL_CONFIRMED, 0,
                    supervisor_generation, token.sample_sequence,
                    elapsed, stable_age);
                  priv->worker = INVALID_PROCESS_ID;
                  nxmutex_unlock(&priv->lock);
                }
              syslog(LOG_INFO,
                     "BOTA TRIAL CONFIRMED slot=%u counter=%lu\n",
                     (unsigned int)priv->expected.active_slot,
                     (unsigned long)priv->expected.security_counter);
              return OK;
            }

          if (nxmutex_lock(&priv->lock) >= 0)
            {
              bk7258_ota_trial_status_locked(
                priv, BK7258_OTA_TRIAL_STABLE, ret,
                supervisor_generation, token.sample_sequence,
                elapsed, stable_age);
              nxmutex_unlock(&priv->lock);
            }
        }

sleep:
      nxsig_usleep((unsigned int)CONFIG_BK7258_OTA_TRIAL_POLL_MS *
                   1000u);
    }

  return OK;
}

static int bk7258_ota_trial_deadline_worker(int argc, char *argv[])
{
  struct bk7258_ota_trial_s *priv = &g_bk7258_ota_trial;

  (void)argc;
  (void)argv;
  for (;;)
    {
      struct bk7258_ota_pair_snapshot_s current;
      clock_t now = clock_systime_ticks();
      uint32_t elapsed = bk7258_ota_trial_age_ms(now, priv->started);
      uint32_t policy_age;
      uint32_t confirm_age;
      uint32_t state;

      if (nxmutex_lock(&priv->lock) < 0)
        {
          goto sleep;
        }
      priv->deadline_tick = now;
      policy_age = bk7258_ota_trial_age_ms(now, priv->policy_tick);
      confirm_age = priv->confirm_tick == 0 ? 0u :
        bk7258_ota_trial_age_ms(now, priv->confirm_tick);
      state = priv->status.state;
      if (state == BK7258_OTA_TRIAL_CONFIRMED ||
          state == BK7258_OTA_TRIAL_NOT_PENDING)
        {
          priv->deadline_worker = INVALID_PROCESS_ID;
          nxmutex_unlock(&priv->lock);
          return OK;
        }
      nxmutex_unlock(&priv->lock);

      if ((state == BK7258_OTA_TRIAL_CONFIRMING &&
           confirm_age >= CONFIG_BK7258_OTA_TRIAL_CONFIRM_TIMEOUT_MS) ||
          (state != BK7258_OTA_TRIAL_CONFIRMING &&
           (policy_age > CONFIG_BK7258_OTA_TRIAL_HEALTH_MAX_AGE_MS ||
            elapsed >= CONFIG_BK7258_OTA_TRIAL_TIMEOUT_MS)))
        {
          memset(&current, 0, sizeof(current));
          if (bk7258_ota_get_active_pair(&current) == 0 &&
              current.state == BK7258_OTA_PAIR_CONFIRMED &&
              bk7258_ota_trial_pair_equal(&current, &priv->expected))
            {
              if (nxmutex_lock(&priv->lock) >= 0)
                {
                  bk7258_ota_trial_status_locked(
                    priv, BK7258_OTA_TRIAL_CONFIRMED, 0,
                    priv->status.supervisor_generation,
                    priv->status.sample_sequence, elapsed,
                    priv->status.stable_age_ms);
                  priv->deadline_worker = INVALID_PROCESS_ID;
                  nxmutex_unlock(&priv->lock);
                }
              return OK;
            }

          bk7258_ota_trial_reset(
            priv, state == BK7258_OTA_TRIAL_CONFIRMING ||
                  elapsed >= CONFIG_BK7258_OTA_TRIAL_TIMEOUT_MS ?
                  -ETIMEDOUT : -EHOSTDOWN);
        }

sleep:
      nxsig_usleep((unsigned int)CONFIG_BK7258_OTA_TRIAL_POLL_MS *
                   1000u);
    }

  return OK;
}

int bk7258_ota_trial_initialize(void)
{
  struct bk7258_ota_trial_s *priv = &g_bk7258_ota_trial;
  struct bk7258_ota_pair_snapshot_s pair;
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

  memset(&pair, 0, sizeof(pair));
  ret = bk7258_ota_get_active_pair(&pair);
  memset(&priv->status, 0, sizeof(priv->status));
  priv->status.version = BK7258_OTA_TRIAL_STATUS_VERSION;
  priv->status.size = sizeof(priv->status);
  if (ret < 0)
    {
      priv->status.state = BK7258_OTA_TRIAL_ERROR;
      priv->status.last_error = ret;
      priv->initialized = true;
      nxmutex_unlock(&priv->lock);
      syslog(LOG_ERR, "BOTA TRIAL PAIR error=%d\n", ret);
      bk7258_ota_system_reset();
    }

  priv->expected = pair;
  priv->status.active_slot = pair.active_slot;
  priv->status.image_version = pair.version;
  priv->status.security_counter = pair.security_counter;
  priv->initialized = true;
  if (pair.state == BK7258_OTA_PAIR_CONFIRMED)
    {
      priv->status.state = BK7258_OTA_TRIAL_NOT_PENDING;
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  priv->started = clock_systime_ticks();
  priv->policy_tick = priv->started;
  priv->deadline_tick = priv->started;
  priv->status.state = BK7258_OTA_TRIAL_WAITING_HEALTH;
  priv->worker = kthread_create(
    BK7258_OTA_TRIAL_NAME, CONFIG_BK7258_OTA_TRIAL_PRIORITY,
    CONFIG_BK7258_OTA_TRIAL_STACKSIZE, bk7258_ota_trial_worker, NULL);
  if (priv->worker < 0)
    {
      ret = priv->worker;
      priv->status.state = BK7258_OTA_TRIAL_ERROR;
      priv->status.last_error = ret;
    }
  else
    {
      priv->deadline_worker = kthread_create(
        "bk7258-ota-deadline", CONFIG_BK7258_OTA_TRIAL_PRIORITY,
        CONFIG_BK7258_OTA_TRIAL_STACKSIZE,
        bk7258_ota_trial_deadline_worker, NULL);
      ret = priv->deadline_worker < 0 ? priv->deadline_worker : OK;
    }
  nxmutex_unlock(&priv->lock);

  if (ret < 0)
    {
      syslog(LOG_ERR, "BOTA TRIAL WORKER error=%d\n", ret);
      bk7258_ota_system_reset();
    }

  syslog(LOG_INFO, "BOTA TRIAL ARM slot=%u counter=%lu\n",
         (unsigned int)pair.active_slot,
         (unsigned long)pair.security_counter);
  return OK;
}

int bk7258_ota_trial_get_status(struct bk7258_ota_trial_status_s *status)
{
  struct bk7258_ota_trial_s *priv = &g_bk7258_ota_trial;
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
      status->policy_age_ms = priv->policy_tick == 0 ? UINT32_MAX :
        bk7258_ota_trial_age_ms(now, priv->policy_tick);
      status->deadline_age_ms = priv->deadline_tick == 0 ? UINT32_MAX :
        bk7258_ota_trial_age_ms(now, priv->deadline_tick);
      status->confirm_age_ms = priv->confirm_tick == 0 ? 0u :
        bk7258_ota_trial_age_ms(now, priv->confirm_tick);
      ret = OK;
    }
  nxmutex_unlock(&priv->lock);
  return ret;
}

#endif /* CONFIG_BK7258_OTA_AUTO_CONFIRM */
