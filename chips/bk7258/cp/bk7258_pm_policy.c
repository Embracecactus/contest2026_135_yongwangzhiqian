/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/
 * bk7258_pm_policy.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned NuttX PM participant and BK7258 multi-client DVFS policy.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_PM_POLICY

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/mutex.h>
#include <nuttx/power/pm.h>

#include <arch/chip/bk7258_pm.h>

#include "bk7258_dvfs.h"
#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
#  include "bk7258_pm_coord.h"
#endif

/* CP v3.1.1.9 pm_dev_id_e values used by code linked into this port. */

#define BK7258_SDK_PM_DEV_PWM2          12u
#define BK7258_SDK_PM_DEV_USB           17u
#define BK7258_SDK_PM_DEV_WPAS          25u
#define BK7258_SDK_PM_DEV_SECURE        36u
#define BK7258_SDK_PM_DEV_CPU1          38u
#define BK7258_SDK_PM_DEV_CIF           39u
#define BK7258_SDK_PM_DEV_DEFAULT       41u

#define BK7258_PM_DEFAULT_FLOOR BK7258_PM_OPP_120M

struct bk7258_pm_policy_s
{
  mutex_t lock;
  struct pm_callback_s callback;
  bool initialized;
  uint8_t votes[BK7258_PM_FREQ_CLIENT_COUNT];
  uint8_t current;
  uint8_t peak;
  uint32_t transitions;
};

static struct bk7258_pm_policy_s g_bk7258_pm_policy =
{
  .lock = NXMUTEX_INITIALIZER,
  .callback =
    {
      .prio = 0,
    },
};

static int bk7258_pm_prepare(FAR struct pm_callback_s *callback, int domain,
                             enum pm_state_e state)
{
  (void)callback;
  (void)domain;

  /* PM_SLEEP always remains fail-closed.  PM_STANDBY is admitted only by
   * the complete CP/AP/AON protocol and only while its live prerequisites
   * are still true; a prepare rejection leaves the previous PM state active.
   */

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  if (state == PM_STANDBY)
    {
      return bk7258_pm_cp_can_standby() ? OK : -EBUSY;
    }
#endif

  return state == PM_NORMAL || state == PM_IDLE || state == PM_RESTORE ?
         OK : -EBUSY;
}

static void bk7258_pm_notify(FAR struct pm_callback_s *callback, int domain,
                             enum pm_state_e state)
{
  (void)callback;
  (void)domain;
  (void)state;
}

static uint8_t bk7258_pm_effective_locked(
  FAR const struct bk7258_pm_policy_s *policy)
{
  uint8_t effective = BK7258_PM_DEFAULT_FLOOR;
  unsigned int i;

  for (i = 0; i < BK7258_PM_FREQ_CLIENT_COUNT; i++)
    {
      if (policy->votes[i] != BK7258_PM_OPP_DEFAULT &&
          effective < policy->votes[i])
        {
          effective = policy->votes[i];
        }
    }

  return effective;
}

int bk7258_pm_frequency_vote(enum bk7258_pm_freq_client_e client,
                             bk7258_pm_opp_t opp)
{
  FAR struct bk7258_pm_policy_s *policy = &g_bk7258_pm_policy;
  uint8_t previous;
  uint8_t effective;
  int ret;

  if (client < 0 || client >= BK7258_PM_FREQ_CLIENT_COUNT ||
      opp < 0 || opp > BK7258_PM_OPP_DEFAULT)
    {
      return -EINVAL;
    }

  if (!policy->initialized)
    {
      return -EAGAIN;
    }

  ret = nxmutex_lock(&policy->lock);
  if (ret < 0)
    {
      return ret;
    }

  previous = policy->votes[client];
  policy->votes[client] = opp;
  effective = bk7258_pm_effective_locked(policy);

  if (effective != policy->current)
    {
      ret = bk7258_dvfs_set_opp(effective);
      if (ret < 0)
        {
          policy->votes[client] = previous;
          goto out;
        }

      policy->current = effective;
      if (policy->peak < effective)
        {
          policy->peak = effective;
        }

      policy->transitions++;
    }

  ret = OK;

out:
  nxmutex_unlock(&policy->lock);
  return ret;
}

int bk7258_pm_frequency_get_status(
  FAR struct bk7258_pm_frequency_status_s *status)
{
  FAR struct bk7258_pm_policy_s *policy = &g_bk7258_pm_policy;
  int ret;

  if (status == NULL)
    {
      return -EINVAL;
    }

  if (!policy->initialized)
    {
      return -EAGAIN;
    }

  ret = nxmutex_lock(&policy->lock);
  if (ret < 0)
    {
      return ret;
    }

  status->current = policy->current;
  status->peak = policy->peak;
  status->transitions = policy->transitions;
  nxmutex_unlock(&policy->lock);
  return OK;
}

bool bk7258_pm_frequency_votes_idle(void)
{
  FAR const struct bk7258_pm_policy_s *policy = &g_bk7258_pm_policy;
  unsigned int i;

  if (!policy->initialized)
    {
      return false;
    }

  /* pm_idle() has interrupts disabled and the scheduler locked before the
   * board's prepare callback runs, so the vote table cannot change during
   * this bounded read.
   */

  for (i = 0; i < BK7258_PM_FREQ_CLIENT_COUNT; i++)
    {
      if (policy->votes[i] != BK7258_PM_OPP_DEFAULT)
        {
          return false;
        }
    }

  return true;
}

static int bk7258_pm_sdk_client(unsigned int module)
{
  switch (module)
    {
      case BK7258_SDK_PM_DEV_DEFAULT:
        return BK7258_PM_FREQ_CLIENT_DEFAULT;
      case BK7258_SDK_PM_DEV_WPAS:
        return BK7258_PM_FREQ_CLIENT_WIFI;
      case BK7258_SDK_PM_DEV_CIF:
        return BK7258_PM_FREQ_CLIENT_BLUETOOTH;
      case BK7258_SDK_PM_DEV_USB:
        return BK7258_PM_FREQ_CLIENT_USB;
      case BK7258_SDK_PM_DEV_PWM2:
        return BK7258_PM_FREQ_CLIENT_PWM;
      case BK7258_SDK_PM_DEV_SECURE:
        return BK7258_PM_FREQ_CLIENT_SECURE;
      case BK7258_SDK_PM_DEV_CPU1:
        return BK7258_PM_FREQ_CLIENT_CPU1;
      default:
        return -ENOTSUP;
    }
}

int __wrap_bk_pm_module_vote_cpu_freq(unsigned int module, int frequency)
{
  int client = bk7258_pm_sdk_client(module);

  if (client < 0)
    {
      return client;
    }

  return bk7258_pm_frequency_vote(client, frequency);
}

void arm_pminitialize(void)
{
  FAR struct bk7258_pm_policy_s *policy = &g_bk7258_pm_policy;
  unsigned int i;

  pm_initialize();

  /* The greedy governor returns the first locked state.  A coordinated
   * build may select STANDBY but never SLEEP; ordinary profiles retain the
   * first-stage PM_IDLE ceiling.
   */

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  pm_stay(PM_IDLE_DOMAIN, PM_STANDBY);
#else
  pm_stay(PM_IDLE_DOMAIN, PM_IDLE);
#endif

  for (i = 0; i < BK7258_PM_FREQ_CLIENT_COUNT; i++)
    {
      policy->votes[i] = BK7258_PM_OPP_DEFAULT;
    }

  policy->current = bk7258_dvfs_get_opp();
  policy->peak = policy->current;
  policy->callback.prepare = bk7258_pm_prepare;
  policy->callback.notify = bk7258_pm_notify;
  policy->initialized = true;

  /* Registration cannot fail for this first, statically allocated callback.
   * Keep the void ARM initialization ABI and leave the policy active even if
   * a malformed external configuration already registered the same node. */

  (void)pm_register(&policy->callback);
}

#endif /* CONFIG_BK7258_PM_POLICY */
