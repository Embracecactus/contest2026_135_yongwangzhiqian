/* SPDX-License-Identifier: Apache-2.0 */

/* Serialized by the existing product loop. Only metadata crosses into the
 * peripheral workers; no sensor, framebuffer or storage I/O belongs here.
 */

#include "bk7258_motion_core.h"
#include "bk7258_haptic_service.h"

#if defined(CONFIG_BK7258_MOTION_SERVICE) && \
    defined(CONFIG_BK7258_DISPLAY_SERVICE)
static struct bkmotion_actions_s g_companion_motion;
static uint64_t g_companion_pulse_at;
static uint64_t g_companion_quiet_until;
static bool g_companion_pulsed;
static bool g_companion_motor_fault;
static bool g_companion_battery_low;
static unsigned g_companion_battery;
#endif

/* One coalesced native-app pulse hint, consumed by the next companion step
 * under the same cooldown, quiet window, battery and motor gates as task
 * completion. It is never queued beyond that step.
 */

#ifdef CONFIG_BK7258_NATIVE_APPS
static bool g_companion_app_pulse;

static void product_companion_request_pulse(uint64_t now)
{
  (void)now;
  g_companion_app_pulse = true;
}
#endif

/* The board motor is interlocked while the wake recorder owns the MIC. A
 * product pulse therefore uses the same bounded capture handoff as local
 * content: close capture, finish the pulse, then restore the wake model.
 */

#ifdef CONFIG_BK7258_HAPTIC_SERVICE
static int product_companion_pulse(unsigned int duration_ms)
{
  bool paused = false;
  int ret;

  if (g_trigger_started)
    {
      ret = bk7258_agent_trigger_pause_local();
      if (ret < 0)
        {
          g_product_error = ret;
          return -EBUSY;
        }

      paused = true;
    }

  ret = bkhaptic_service_pulse_wait(duration_ms);
  if (paused)
    {
      int rearm = bk7258_agent_trigger_rearm();
      if (rearm < 0)
        {
          g_product_error = rearm;
        }
    }

  return ret;
}
#endif

static void product_companion_step(uint64_t now, bool admitted,
                                   bool voice_idle, bool completed)
{
#if defined(CONFIG_BK7258_MOTION_SERVICE) && \
    defined(CONFIG_BK7258_DISPLAY_SERVICE)
  static const struct bkmotion_actions_config_s config =
  {
    .move_mms2 = 3000,
    .quiet_mms2 = 500,
    .gravity_min_mms2 = 8000,
    .gravity_max_mms2 = 12000,
    .tilt_enter_cos = 800,
    .tilt_leave_cos = 950,
    .settle_us = 400000,
    .cooldown_us = 2000000,
    .max_gap_us = 250000
  };
  struct bkmotion_rpc_response_s sample;
  enum bkmotion_action_e action;
  bool enabled = admitted && voice_idle;
  bool quiet = now < g_companion_quiet_until;
  int ret;
#ifdef CONFIG_BK7258_NATIVE_APPS
  bool app_pulse = g_companion_app_pulse;
  g_companion_app_pulse = false;
#else
  bool app_pulse = false;
#endif
#ifdef CONFIG_BK7258_HEALTH_SERVICE
  struct bk7258_health_service_snapshot_s health;
  bool health_valid = bk7258_health_service_snapshot(&health) == 0 &&
    health.sampled_ms <= now && now - health.sampled_ms <= 120000;
#endif

#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  struct bkhaptic_product_status_s haptic;
  if (bkhaptic_service_product_status(&haptic) == 0 && haptic.fault)
    {
      g_companion_motor_fault = true;
    }
#endif

  (void)bk7258_motion_service_poll(enabled && !quiet);
  if (!enabled)
    {
      (void)bkmotion_actions_step(&g_companion_motion, &config, NULL,
                                  false, &action);
      (void)bk7258_display_companion(0, 0);
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
      (void)bkhaptic_service_stop_product();
#endif
      return;
    }

#ifdef CONFIG_BK7258_HEALTH_SERVICE
  if (health_valid &&
      (health.flags & BK7258_HEALTH_SNAPSHOT_BATTERY_VOLTAGE_VALID))
    {
      if (health.battery_voltage_mv <= 3400) g_companion_battery_low = true;
      if (health.battery_voltage_mv >= 3600) g_companion_battery_low = false;
      unsigned battery = g_companion_battery_low ? 5 : 0;
      if ((health.flags & BK7258_HEALTH_SNAPSHOT_BATTERY_STATE_VALID) &&
          health.battery_state == BATTERY_CHARGING) battery = 4;
      if (battery != g_companion_battery)
        {
          g_companion_battery = battery;
          if (battery) (void)bk7258_display_companion(battery, 800);
        }
    }
#endif

  /* Completion consumes its own event immediately, even if a display, voice,
   * battery or motor gate rejects it. There is no delayed notification queue.
   */

  if ((completed || app_pulse) && (!g_companion_pulsed ||
      (now >= g_companion_pulse_at && now - g_companion_pulse_at >= 2000)))
    {
      g_companion_pulsed = true;
      g_companion_pulse_at = now;
      ret = completed ? bk7258_display_companion(2, 800) : 0;
#if defined(CONFIG_BK7258_HEALTH_SERVICE) && \
    defined(CONFIG_BK7258_HAPTIC_SERVICE)
      if ((ret == 0 || ret == -EALREADY) && !g_companion_motor_fault &&
          health_valid &&
          (health.flags & BK7258_HEALTH_SNAPSHOT_BATTERY_VOLTAGE_VALID) &&
          !g_companion_battery_low)
        {
          ret = product_companion_pulse(40);
          if (ret == 0)
            {
              g_companion_quiet_until = now + 500;
              quiet = true;
            }
          else if (ret != -EBUSY && ret != -EAGAIN)
            {
              g_companion_motor_fault = true;
              (void)bkhaptic_service_stop_product();
            }
        }
#endif
    }

  if (quiet)
    {
      (void)bkmotion_actions_step(&g_companion_motion, &config, NULL,
                                  false, &action);
      return;
    }

  ret = bk7258_motion_service_snapshot(&sample);
  if (ret < 0 || sample.timestamp_us / 1000 > now ||
      now - sample.timestamp_us / 1000 > 250)
    {
      (void)bkmotion_actions_step(&g_companion_motion, &config, NULL,
                                  false, &action);
      (void)bk7258_display_companion(0, 0);
      return;
    }

  ret = bkmotion_actions_step(&g_companion_motion, &config, &sample,
                              true, &action);
#ifdef CONFIG_BK7258_NATIVE_APPS
  if (ret == 0 && action != BKMOTION_ACTION_NONE && bknative_motion_owner())
    {
      /* A running app holding "motion" owns the reaction to this event. */

      bknative_post_events(1u << ((unsigned int)action - 1u));
      syslog(LOG_INFO, "BKCOMPANION motion=%u sample=%llu app=1\n",
             (unsigned int)action,
             (unsigned long long)sample.timestamp_us);
    }
  else
#endif
  if (ret == 0 && action != BKMOTION_ACTION_NONE)
    {
      ret = bk7258_display_companion((unsigned int)action, 800);
      syslog(LOG_INFO, "BKCOMPANION motion=%u sample=%llu accepted=%d\n",
             (unsigned int)action,
             (unsigned long long)sample.timestamp_us, ret);
    }
#else
  (void)now;
  (void)admitted;
  (void)voice_idle;
  (void)completed;
#endif
}
