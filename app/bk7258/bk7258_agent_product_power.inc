/* SPDX-License-Identifier: Apache-2.0 */
/* Product-owned power coordination. Included by the product translation unit
 * and host contracts; all resource participants remain external dependencies. */

#if defined(CONFIG_BK7258_PRODUCT_KEYS) && defined(CONFIG_BK7258_PM_SOFT_OFF)
static bool g_power_pending;
static bool g_shutdown_requested;
static bool g_shutdown_failed;
static uint64_t g_shutdown_deadline;
static bool g_power_volume_owned;
static bool g_power_storage_stopped;
static bool g_power_trigger_stopped;
static bool g_power_vision_quiesced;
static bool g_power_haptic_quiesced;
static uint64_t g_power_query_at;
#endif

#ifdef CONFIG_BK7258_PM_SOFT_OFF
static void product_power_fail(int error)
{
  /* Retain user intent and completed resource exits. Ordinary polling and
   * late events cannot silently resume operation after a failed shutdown.
   */
  g_shutdown_failed = true;
  g_shutdown_requested = false;
  g_power_pending = false;
  g_product_error = error;
  (void)bk7258_display_power(3);
}

static void product_power_drain(void)
{
  /* Keep already-admitted work and cancellation progressing without opening
   * new admission. Once storage has stopped there can be no pending commit.
   */
  if (!g_power_storage_stopped)
    {
      (void)bkprov_network_cancel();
      bkprov_network_step();
      bkprov_config_step();
    }

  if (atomic_load(&g_voice_initialized)) (void)voice_channel_recover();
}

static int product_power_request(void)
{
  int ret;
  /* The intent is latched independently of cloud readiness. This final
   * transition still requires actual resource drain; cancellation alone
   * is not evidence of a completed durable transaction.
   */

  if ((atomic_load(&g_voice_initialized) && !voice_channel_is_idle()) ||
      atomic_load(&g_probe_running) ||
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
  if (!g_power_volume_owned)
    {
      ret = bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_POWER);
      if (ret < 0) return ret;
      g_power_volume_owned = true;
    }
#ifdef CONFIG_BK7258_VISION_SERVICE
  ret = g_power_vision_quiesced ? 0 : bk7258_vision_quiesce(true);
  if (ret < 0)
    {
      goto failed;
    }

  g_power_vision_quiesced = true;
#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  ret = g_power_haptic_quiesced ? 0 : bkhaptic_service_quiesce(true);
  if (ret < 0)
    {
      goto failed;
    }

  g_power_haptic_quiesced = true;
#endif
  ret = g_power_storage_stopped ? 0 : bkprov_storage_stop();
  if (ret < 0)
    {
      goto failed;
    }

  g_power_storage_stopped = true;
  /* Reads remain available while preparing. Close the transport only after
   * resource exits, so query traffic cannot hold up the final CP request.
   */
  ret = bkprov_owner_quiesce(true);
  if (ret < 0) return ret;
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
          goto failed;
        }
    }

  g_power_pending = true;
  g_power_query_at = bkvoice_config_now_ms(NULL) + 500;
  return ret;

failed:
  product_power_fail(ret);

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
  if (!g_power_pending && !g_shutdown_requested && !g_shutdown_failed)
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
              product_power_fail(-ECANCELED);
              syslog(LOG_WARNING, "BKKEYS power not accepted; intent retained\n");
            }
        }

      return g_power_pending || g_shutdown_failed;
    }

  if (g_shutdown_failed && !power)
    {
      (void)bkprov_owner_prepare_stop(now);
      product_power_drain();
      return true;
    }
#else
  (void)now;
#endif
  if (power)
    {
#ifdef CONFIG_BK7258_PM_SOFT_OFF
      g_shutdown_failed = false;
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
      int admission = bkprov_owner_prepare_stop(now);
      /* Independent shutdown participants must still run after an admission
       * failure. A start flag is not proof that failed startup owns nothing.
       * Retain the stop acknowledgement only after the owner reports success.
       */
      int trigger = 0;
      if (!g_power_trigger_stopped)
        {
          trigger = bk7258_agent_trigger_stop();
          if (trigger == 0)
            {
              g_power_trigger_stopped = true;
              g_trigger_started = false;
              atomic_store(&g_trigger_prepare_pending, true);
            }
        }

      product_power_drain();
      if (admission < 0 || trigger < 0)
        {
          int error = admission < 0 ? admission : trigger;
          /* Quiescing is a handshake: EAGAIN/EBUSY is incomplete, not a
           * permanent failure. A permanent participant error takes priority.
           */
          if (trigger < 0 && trigger != -EAGAIN && trigger != -EBUSY)
            error = trigger;
          if (error == -EAGAIN || error == -EBUSY)
            {
              if (now >= g_shutdown_deadline)
                product_power_fail(-ETIMEDOUT);
            }
          else
            {
              product_power_fail(error);
            }
          return true;
        }
      ret = product_power_request();
      if (g_power_pending)
        {
          g_shutdown_requested = false;
        }
      else if ((ret != -EBUSY && ret != -EAGAIN) ||
               now >= g_shutdown_deadline)
        {
          if (ret == -EBUSY || ret == -EAGAIN) ret = -ETIMEDOUT;
          product_power_fail(ret);
          syslog(LOG_ERR, "BKKEYS shutdown failed=%d; intent retained\n", ret);
        }
    }

  return g_power_pending || g_shutdown_requested || g_shutdown_failed;
#else
  return false;
#endif
}
