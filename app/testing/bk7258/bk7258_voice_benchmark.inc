/* SPDX-License-Identifier: Apache-2.0 */
/* Included only by the development BKTEST product adapter. The existing
 * product task executes requests; Agent owns ASR/reply/TTS/Media lifecycle.
 * No settings, identities, user history or firmware bytes are written here.
 */

static int product_benchmark_check(void *context)
{
  return bkengvoice_canceled((uint32_t)(uintptr_t)context) ? -ECANCELED : 0;
}

#ifdef CONFIG_BK7258_TTS_ISOLATION
/* Execute on the existing outbound owner. The service task must remain free
 * to process cancellation and rearm while synchronous TTS drains Media. */
static atomic_bool g_tts_isolation_active;
static _Atomic(pthread_t) g_tts_isolation_owner;

static int product_tts_isolation_text(uint64_t request, const char *text,
                                       int (*check)(uint64_t))
{
  if (!atomic_load(&g_tts_isolation_active) ||
      !pthread_equal(pthread_self(), atomic_load(&g_tts_isolation_owner)))
    return product_focus_text(request, text, check);
  int ret = check ? check(request) : 0;
  if (!ret) ret = voice_channel_speak_reply(request, text);
  return ret ? ret : 1;
}

static bool product_tts_isolation_dispatch(agent_msg_t *message, int *result)
{
  if (strcmp(message->channel, "BKTEST_TTS")) return false;
  uint32_t session = (uint32_t)message->request_id;
  int ret = session != atomic_load(&g_benchmark_session) ? -ESTALE :
            product_benchmark_check((void *)(uintptr_t)session);
  if (!ret)
    {
      atomic_store(&g_tts_isolation_owner, pthread_self());
      atomic_store(&g_tts_isolation_active, true);
      ret = voice_channel_submit_text_checked(message->content,
        product_benchmark_check, (void *)(uintptr_t)session);
      atomic_store(&g_tts_isolation_active, false);
    }
  if (ret)
    {
      unsigned int expected = session;
      if (atomic_compare_exchange_strong(&g_benchmark_session, &expected, 0))
        bkengvoice_finish(session, voice_channel_request_id(), ret);
    }
  *result = ret;
  return true;
}
#endif

static int product_benchmark_config(const uint8_t *data, size_t size)
{
  /* A benchmark can supply a missing development configuration. It cannot
   * replace an installed product selection or borrow its owner authority. */
  /* Bootstrap discovery can remain open without a phone. It owns no cloud
   * request. Reject a ready phone transport, installed owner or active
   * network transaction, rather than waiting for the discovery window. */
  if (g_cloud_loaded || g_configured || !g_identity_bound ||
      g_control_bound || bkprov_network_busy() || bkprov_gatt_generation())
    return -EBUSY;
  struct bkengvoice_config_s config;
  int ret = bkengvoice_config_decode(data, size, &config);
  if (ret) return ret;
  struct benchmark_config_work_s
  {
    struct bkcloud_config_s cloud;
    struct bkprov_settings_s settings;
    uint8_t trust[BKVOICE_CONFIG_MAX_BYTES];
  } *work = calloc(1, sizeof(*work));
  if (!work) return -ENOMEM;
  ret = bkcloud_config_decode(&work->cloud, config.cloud, config.cloud_size);
  if (!ret)
    {
      memcpy(work->settings.host, work->cloud.host, sizeof(work->settings.host));
      memcpy(work->settings.address, config.address, 4);
      work->settings.port = work->cloud.port;
      work->settings.utc = config.utc;
      work->settings.ca = config.ca;
      work->settings.ca_size = config.ca_size;
      size_t trust_size = 0;
      ret = bkprov_settings_voice(&work->settings,
        g_identity.record + 48, g_identity.certificate_size,
        g_identity.record + 48 + g_identity.certificate_size,
        g_identity.key_size, work->trust, sizeof(work->trust), &trust_size);
      if (!ret)
        {
          g_benchmark_cloud = true;
          ret = product_load_cloud_models(work->trust, trust_size,
                                          config.cloud, config.cloud_size, NULL);
          if (!ret) ret = bkagent_cloud_verify_service();
          g_probe_result = g_service_result = ret;
          g_configured = ret == 0;
          g_product_error = ret;
          if (ret) atomic_store(&g_benchmark_clear, true);
        }
    }
  mbedtls_platform_zeroize(work, sizeof(*work));
  free(work);
  return ret;
}

static void product_benchmark_step(void)
{
  if (atomic_load(&g_benchmark_clear) && voice_channel_is_idle())
    {
      int ret = g_benchmark_cloud ? bkagent_cloud_clear() : 0;
      if (!ret)
        {
          if (g_benchmark_cloud)
            {
              g_cloud_loaded = g_configured = false;
              g_service_result = g_probe_result = -ENOTCONN;
            }
          g_benchmark_cloud = false;
          atomic_store(&g_benchmark_clear, false);
        }
    }
  struct bkengvoice_input_s input;
  if (bkengvoice_take(&input)) return;
  int ret = 0;
  uint32_t power = 0;
  int32_t power_error = 0;
  if (!atomic_load(&g_agent_core_ready) ||
      !atomic_load(&g_voice_initialized) || !voice_channel_is_idle() ||
      bkagent_ota_busy() || bkprov_network_busy() ||
      atomic_load(&g_benchmark_clear)) ret = -EBUSY;
  if (!ret) ret = bk7258_agent_power_status(NULL, &power, &power_error);
  if (!ret && (power || power_error)) ret = -EBUSY;
  if (!ret && g_trigger_started)
    {
      ret = bk7258_agent_trigger_stop();
      if (!ret) g_trigger_started = false;
    }
  if (!ret) ret = product_benchmark_check((void *)(uintptr_t)input.session);
  if (!ret && input.mode == 0)
    ret = product_benchmark_config(input.data, input.size);
  else if (!ret)
    {
      if (!g_configured || !g_cloud_loaded) ret = -ENOKEY;
      else
        {
          atomic_store(&g_benchmark_session, input.session);
#ifdef CONFIG_BK7258_TTS_ISOLATION
          if (input.mode == 3)
            {
              agent_msg_t message = {0};
              strcpy(message.channel, "BKTEST_TTS");
              message.request_id = input.session;
              message.content = strdup((const char *)input.data);
              ret = message.content ? message_bus_push_outbound(&message) :
                    -ENOMEM;
              if (ret) free(message.content);
            }
          else
#endif
          {
          /* A cancel arriving before claim is checked by the channel; its
           * same admission lease also guards later Agent/tool commit. */
          ret = input.mode == 1 ? voice_channel_submit_text_checked(
            (const char *)input.data, product_benchmark_check,
            (void *)(uintptr_t)input.session) :
            voice_channel_submit_pcm_checked(input.data, input.size,
              product_benchmark_check, (void *)(uintptr_t)input.session);
          }
        }
    }
  bkengvoice_release(input.session);
  if (ret || input.mode == 0)
    {
      atomic_store(&g_benchmark_session, 0);
      bkengvoice_finish(input.session, 0, ret);
    }
  syslog(LOG_NOTICE, "BKVOICE benchmark session=%lu mode=%lu submitted=%d\n",
         (unsigned long)input.session, (unsigned long)input.mode, ret);
}
