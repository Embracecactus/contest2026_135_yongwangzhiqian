/* SPDX-License-Identifier: Apache-2.0 */
/* Product-owned power coordination. Included by the product translation unit
 * and host contracts; all resource participants remain external dependencies. */

#if defined(CONFIG_BK7258_PRODUCT_KEYS) && defined(CONFIG_BK7258_PM_SOFT_OFF)
static bool g_power_pending;
static bool g_shutdown_requested;
static uint64_t g_shutdown_deadline;
static bool g_power_volume_owned;
static bool g_power_storage_stopped;
static bool g_power_vision_quiesced;
static bool g_power_haptic_quiesced;
static uint64_t g_power_query_at;
#endif

#ifdef CONFIG_BK7258_PM_SOFT_OFF
static int product_power_restore(void)
{
  int ret;
  if (g_power_storage_stopped)
    {
      ret = bkprov_storage_start("/cpdata/shaniu");
      if (ret < 0)
        {
          return ret;
        }

      g_power_storage_stopped = false;
    }

  if (g_power_volume_owned)
    {
      ret = bk7258_media_volume_release(BK7258_MEDIA_VOLUME_POWER);
      if (ret < 0)
        {
          return ret;
        }

      g_power_volume_owned = false;
    }

#ifdef CONFIG_BK7258_VISION_SERVICE
  if (g_power_vision_quiesced)
    {
      ret = bk7258_vision_quiesce(false);
      if (ret < 0)
        {
          return ret;
        }

      g_power_vision_quiesced = false;
    }

#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  if (g_power_haptic_quiesced)
    {
      ret = bkhaptic_service_quiesce(false);
      if (ret < 0)
        {
          return ret;
        }

      g_power_haptic_quiesced = false;
    }

#endif
  (void)bkprov_owner_quiesce(false);
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  (void)bk7258_display_power(0);
#endif
#ifdef CONFIG_BK7258_PROVISION_NATIVE
  (void)bkprov_bootstrap_start();
#endif
  return 0;
}

static int product_power_request(void)
{
  int ret;
  /* The intent is latched independently of cloud readiness. This final
   * transition still requires actual resource drain; cancellation alone
   * is not evidence of a completed durable transaction.
   */

  if ((atomic_load(&g_voice_initialized) && !voice_channel_is_idle()) ||
      atomic_load(&g_probe_running) || bkprov_owner_busy() ||
      bkprov_network_busy() || bkprov_config_busy() ||
      bkprov_scan_busy() ||
      bkagent_ota_busy() || bk7258_agent_trigger_model_pending())
    {
      return -EBUSY;
    }

#ifdef CONFIG_BK7258_PROVISION_NATIVE
  bkprov_bootstrap_cancel();
  if (bkprov_bootstrap_busy()) return -EBUSY;
#endif
  ret = bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_POWER);
  if (ret < 0)
    {
      return ret;
    }

  g_power_volume_owned = true;
#ifdef CONFIG_BK7258_VISION_SERVICE
  ret = bk7258_vision_quiesce(true);
  if (ret < 0)
    {
      goto restore;
    }

  g_power_vision_quiesced = true;
#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  ret = bkhaptic_service_quiesce(true);
  if (ret < 0)
    {
      goto restore;
    }

  g_power_haptic_quiesced = true;
#endif
  ret = bkprov_storage_stop();
  if (ret < 0)
    {
      goto restore;
    }

  g_power_storage_stopped = true;
  ret = bk7258_agent_trigger_stop();
  if (ret < 0)
    {
      goto restore;
    }

  g_trigger_started = false;
  atomic_store(&g_trigger_prepare_pending, true);
  sync();
  ret = bk7258_pm_soft_off_request();
  if (ret < 0)
    {
      /* A timeout does not mean the CP did not receive it; when the state is
       * unknown, the cleaned-up resource boundary is retained.
       */

      int pending = bk7258_pm_soft_off_status();
      if (pending == 0)
        {
          goto restore;
        }
    }

  g_power_pending = true;
  g_power_query_at = bkvoice_config_now_ms(NULL) + 500;
  return ret;

restore:
{
  int cleanup = product_power_restore();
  if (cleanup < 0)
    {
      g_power_pending = true;
      g_power_query_at = bkvoice_config_now_ms(NULL) + 500;
      return cleanup;
    }
}

  return ret;
}
#endif

static bool product_keys_step(uint64_t now)
{
  unsigned int volume = 0;
  int steps;
  int ret;
  bool power;
  bkvoice_keys_take(&steps, &power);
#if defined(CONFIG_BK7258_PM_SOFT_OFF) && defined(CONFIG_BK7258_DISPLAY_SERVICE)
  if (!g_power_pending && !g_shutdown_requested)
    (void)bk7258_display_power(bkvoice_keys_power_held() ? 1 : 0);
#endif
#ifdef CONFIG_BK7258_PM_SOFT_OFF
  if (g_power_pending)
    {
      if (now >= g_power_query_at)
        {
          ret = bk7258_pm_soft_off_status();
          g_power_query_at = now + 500;
          if (ret == 0)
            {
              ret = product_power_restore();
              if (!ret)
                {
                  g_power_pending = false;
                }

              syslog(LOG_WARNING, "BKKEYS power canceled restore=%d\n", ret);
            }
        }

      return g_power_pending;
    }

#else
  (void)now;
#endif
  if (power)
    {
#ifdef CONFIG_BK7258_PM_SOFT_OFF
      g_shutdown_requested = true;
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
      (void)bk7258_display_power(2);
#endif
      g_shutdown_deadline = now + 30000u;
      if (atomic_load(&g_voice_initialized)) voice_channel_cancel();
      ret = -EINPROGRESS;
#else
      ret = -ENOTSUP;
#endif
      g_product_error = ret;
      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKKEYS power request result=%d\n", ret);
    }
  else if (steps
#ifdef CONFIG_BK7258_PM_SOFT_OFF
           && !g_shutdown_requested
#endif
          )
    {
      ret = bkagent_ota_busy() ? -EBUSY :
              bkvoice_media_volume_step(steps, &volume);
      if (!ret)
        {
          ret = bkvoice_volume_store_set(volume);
        }

      g_product_error = ret;
      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKKEYS volume steps=%d observed=%u result=%d\n",
             steps, volume, ret);
    }

#ifdef CONFIG_BK7258_PM_SOFT_OFF
  if (g_shutdown_requested)
    {
      /* Commit workers and Wi-Fi lease cleanup must continue while admission
       * is closed. A successful cancel is not a completed shutdown.
       */
      (void)bkprov_owner_quiesce(true);
      (void)bkprov_network_cancel();
      bkprov_network_step();
      bkprov_config_step();
      if (atomic_load(&g_voice_initialized)) (void)voice_channel_recover();
      ret = product_power_request();
      if (g_power_pending)
        {
          g_shutdown_requested = false;
        }
      else if ((ret != -EBUSY && ret != -EAGAIN) ||
               now >= g_shutdown_deadline)
        {
          if (ret == -EBUSY || ret == -EAGAIN) ret = -ETIMEDOUT;
          int restored = product_power_restore();
          g_shutdown_requested = false;
          g_product_error = restored ? restored : ret;
          syslog(LOG_ERR, "BKKEYS shutdown failed=%d restore=%d\n",
                 ret, restored);
        }
    }

  return g_power_pending || g_shutdown_requested;
#else
  return false;
#endif
}
