/* SPDX-License-Identifier: Apache-2.0 */
/* Included only by the development BKTEST product adapter. The existing
 * product task executes requests; Agent owns ASR/reply/TTS/Media lifecycle.
 * Identity, user history and firmware bytes are never written here. The
 * claimed-device debug build may explicitly fill an owner-only configuration
 * through the existing settings editor; it never exports/replaces owner keys.
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
    return product_local_text(request, text, check);
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

#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS_CLAIMED_TEST
static char g_benchmark_model_original[128];

static int product_benchmark_restore_llm(void)
{
  if (!g_benchmark_model_original[0]) return 0;
  int ret = bkagent_cloud_test_llm_model(g_benchmark_model_original);
  if (!ret) ret = bkagent_cloud_verify_service();
  g_probe_result = g_service_result = ret;
  g_configured = ret == 0;
  if (!ret) g_benchmark_model_original[0] = 0;
  return ret;
}

static int product_benchmark_select_llm(const uint8_t *data, size_t size)
{
  uint32_t flags = 0, remaining = 0;
  int ret = bkfactory_diagnostics_status(&flags, &remaining);
  if (ret || !(flags & BKFACTORY_DIAGNOSTICS_ACTIVE) || !remaining)
    return ret ? ret : -EACCES;
  if (!g_configured || !g_cloud_loaded || g_benchmark_cloud ||
      bkprov_network_busy() || bkprov_gatt_generation() || bkprov_config_busy())
    return -EBUSY;
  if (size <= 4 || size > 131 || memchr(data + 4, 0, size - 4))
    return -EINVAL;
  char model[128];
  memcpy(model, data + 4, size - 4);
  model[size - 4] = 0;
  struct bkcloud_models_s previous;
  ret = bkagent_cloud_models_get(&previous);
  if (ret) return ret;
  if (strcmp(previous.chat_model, "mimo-v2.6-flash") &&
      strcmp(previous.chat_model, "mimo-v2.6-pro")) return -ENOTSUP;
  if (!g_benchmark_model_original[0])
    strcpy(g_benchmark_model_original, previous.chat_model);
  ret = bkagent_cloud_test_llm_model(model);
  if (!ret) ret = bkagent_cloud_verify_service();
  if (ret)
    {
      int restored = product_benchmark_restore_llm();
      if (restored) ret = restored;
    }
  return ret;
}
#endif

static int product_benchmark_config(const uint8_t *data, size_t size)
{
#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS_CLAIMED_TEST
  if (size >= 4 && !memcmp(data, "BVM1", 4))
    return product_benchmark_select_llm(data, size);
#endif
  /* A benchmark can supply a missing development configuration. It cannot
   * replace an installed product selection or borrow its owner authority.
   * The claimed-device exception remains tied to the active one-boot factory
   * principal and is compiled only into the explicitly authorized BKTEST
   * profile. */
  /* Bootstrap discovery can remain open without a phone. It owns no cloud
   * request. Reject a ready phone transport, installed owner or active
   * network transaction, rather than waiting for the discovery window. */
  bool control_blocked = g_control_bound;
#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS_CLAIMED_TEST
  if (control_blocked)
    {
      uint32_t flags = 0;
      uint32_t remaining_ms = 0;
      if (!bkfactory_diagnostics_status(&flags, &remaining_ms) &&
          (flags & BKFACTORY_DIAGNOSTICS_ACTIVE))
        control_blocked = false;
    }
#endif
  if (g_cloud_loaded || g_configured || !g_identity_bound ||
      control_blocked || bkprov_network_busy() || bkprov_gatt_generation())
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

#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS_CLAIMED_TEST
static int product_benchmark_persist_config(const uint8_t *data, size_t size)
{
  uint32_t flags = 0;
  uint32_t remaining = 0;
  int ret = bkfactory_diagnostics_status(&flags, &remaining);
  if (ret || !(flags & BKFACTORY_DIAGNOSTICS_ACTIVE) || !remaining)
    return ret ? ret : -EACCES;
  if (!g_identity_bound || !g_control_bound || g_cloud_loaded || g_configured ||
      bkprov_network_busy() || bkprov_gatt_generation() || bkprov_config_busy())
    return -EBUSY;
  /* This physical-debug grant can only fill the known owner-only state.
   * SCP1 flag 15 supplies Wi-Fi, cloud, API key and PSK explicitly. All
   * field/CA validation, revision matching, owner-key retention and fsync
   * publication remain in the existing settings editor/storage worker.
   */
  if (size < BKPROV_PATCH_HEADER || size > BKPROV_PATCH_MAX ||
      memcmp(data, "SCP1", 4) || data[28] || data[29] || data[30] ||
      data[31] != 15u)
    return -EBADMSG;
  struct persist_work_s
  {
    uint8_t previous[BKPROV_BUNDLE_MAX];
    struct bkprov_settings_s settings;
  } *work = calloc(1, sizeof(*work));
  if (!work) return -ENOMEM;
  size_t previous_size = 0;
  uint64_t revision = 0;
  uint8_t transaction[16];
  ret = bkprov_storage_snapshot(work->previous, sizeof(work->previous),
                                &previous_size, &revision, transaction);
  if (!ret) ret = bkprov_settings_decode(&work->settings, work->previous,
                                         previous_size);
  if (!ret && (!work->settings.deferred || !work->settings.control_key ||
               work->settings.ssid[0] || work->settings.ca_size ||
               work->settings.cloud_size)) ret = -EEXIST;
  mbedtls_platform_zeroize(work, sizeof(*work));
  free(work);
  if (!ret)
    {
      struct bkcontrol_status_s status = {0};
      ret = bkprov_config_control(BKCONTROL_CONFIG_APPLY, 0, data, size,
                                  &status);
    }
  /* Zero is submission, not a cloud-authentication or durability claim.
   * The ordinary product loop observes the durable revision notification,
   * restores Wi-Fi and verifies MiMo before publishing ready=true.
   */
  syslog(ret ? LOG_WARNING : LOG_NOTICE,
         "BKVOICE development config accepted=%u result=%d\n", !ret, ret);
  return ret;
}
#endif

#if defined(CONFIG_BK7258_TTS_TENCENT_BKTEST) || \
    defined(CONFIG_BK7258_TTS_ALIYUN_BKTEST)
static uint32_t product_benchmark_get32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}
#endif

#ifdef CONFIG_BK7258_TTS_TENCENT_BKTEST
/* BVT1 contains no long-term API credential. It carries one short-lived
 * signed WebSocket path plus the server trust needed for this one-boot test.
 * Header: magic, version, UTC, CA length, path length, IPv4, reserved. */
static int product_benchmark_tencent_config(const uint8_t *data, size_t size)
{
  if (!data || size < 32 || memcmp(data, "BVT1", 4) ||
      product_benchmark_get32(data + 4) != 1 ||
      product_benchmark_get32(data + 28)) return -EINVAL;
  uint64_t utc = ((uint64_t)product_benchmark_get32(data + 8) << 32) |
                 product_benchmark_get32(data + 12);
  size_t ca_size = product_benchmark_get32(data + 16);
  size_t path_size = product_benchmark_get32(data + 20);
  if (utc < UINT64_C(1704067200) || utc > UINT64_C(4133980799) ||
      !ca_size || ca_size > 4096 || !path_size ||
      path_size > BKTENCENT_TTS_PATH_MAX ||
      size != 32 + ca_size + path_size) return -EINVAL;

  struct tencent_config_work_s
  {
    struct bkprov_settings_s settings;
    uint8_t trust[BKVOICE_CONFIG_MAX_BYTES];
    char path[BKTENCENT_TTS_PATH_MAX + 1u];
  } *work = calloc(1, sizeof(*work));
  if (!work) return -ENOMEM;
  strcpy(work->settings.host, BKTENCENT_TTS_HOST);
  memcpy(work->settings.address, data + 24, 4);
  work->settings.port = BKTENCENT_TTS_PORT;
  work->settings.utc = utc;
  work->settings.ca = data + 32;
  work->settings.ca_size = ca_size;
  memcpy(work->path, data + 32 + ca_size, path_size);
  size_t trust_size = 0;
  int ret = bkprov_settings_voice(&work->settings,
    g_identity.record + 48, g_identity.certificate_size,
    g_identity.record + 48 + g_identity.certificate_size,
    g_identity.key_size, work->trust, sizeof(work->trust), &trust_size);
  if (!ret)
    ret = bkagent_tencent_tts_configure(work->trust, trust_size,
                                        work->path, path_size);
  mbedtls_platform_zeroize(work, sizeof(*work));
  free(work);
  return ret;
}
#endif

#ifdef CONFIG_BK7258_TTS_ALIYUN_BKTEST
/* BVA1 carries one API key through authenticated BKTEST into volatile RAM.
 * Header: magic, version, UTC, CA length, key length, IPv4, reserved. */
static int product_benchmark_aliyun_config(const uint8_t *data, size_t size)
{
  if (!data || size < 32 || memcmp(data, "BVA1", 4) ||
      product_benchmark_get32(data + 4) != 1 ||
      product_benchmark_get32(data + 28) > 1) return -EINVAL;
  bool preconnect = product_benchmark_get32(data + 28) == 1;
  uint64_t utc = ((uint64_t)product_benchmark_get32(data + 8) << 32) |
                 product_benchmark_get32(data + 12);
  size_t ca_size = product_benchmark_get32(data + 16);
  size_t key_size = product_benchmark_get32(data + 20);
  if (utc < UINT64_C(1704067200) || utc > UINT64_C(4133980799) ||
      !ca_size || ca_size > 4096 || key_size < 8 ||
      key_size > BKALIYUN_TTS_API_KEY_MAX ||
      size != 32 + ca_size + key_size) return -EINVAL;

  struct aliyun_config_work_s
  {
    struct bkprov_settings_s settings;
    uint8_t trust[BKVOICE_CONFIG_MAX_BYTES];
    char key[BKALIYUN_TTS_API_KEY_MAX + 1u];
  } *work = calloc(1, sizeof(*work));
  if (!work) return -ENOMEM;
  strcpy(work->settings.host, BKALIYUN_TTS_HOST);
  memcpy(work->settings.address, data + 24, 4);
  work->settings.port = BKALIYUN_TTS_PORT;
  work->settings.utc = utc;
  work->settings.ca = data + 32;
  work->settings.ca_size = ca_size;
  memcpy(work->key, data + 32 + ca_size, key_size);
  size_t trust_size = 0;
  int ret = bkprov_settings_voice(&work->settings,
    g_identity.record + 48, g_identity.certificate_size,
    g_identity.record + 48 + g_identity.certificate_size,
    g_identity.key_size, work->trust, sizeof(work->trust), &trust_size);
  if (!ret)
    ret = bkagent_aliyun_tts_configure(work->trust, trust_size,
                                       work->key, key_size, preconnect);
  mbedtls_platform_zeroize(work, sizeof(*work));
  free(work);
  return ret;
}
#endif

static void product_benchmark_step(void)
{
  if (atomic_load(&g_benchmark_clear) && voice_channel_is_idle())
    {
      int ret = 0;
#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS_CLAIMED_TEST
      ret = product_benchmark_restore_llm();
#endif
#ifdef CONFIG_BK7258_TTS_TENCENT_BKTEST
      if (!ret) ret = bkagent_tencent_tts_clear();
#endif
#ifdef CONFIG_BK7258_TTS_ALIYUN_BKTEST
      if (!ret) ret = bkagent_aliyun_tts_clear();
#endif
      if (!ret && g_benchmark_cloud) ret = bkagent_cloud_clear();
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
#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS_CLAIMED_TEST
  else if (!ret && input.mode == 6)
    ret = product_benchmark_persist_config(input.data, input.size);
#endif
#ifdef CONFIG_BK7258_TTS_TENCENT_BKTEST
  else if (!ret && input.mode == 4)
    ret = !g_configured || !g_cloud_loaded ? -ENOKEY :
          product_benchmark_tencent_config(input.data, input.size);
#endif
#ifdef CONFIG_BK7258_TTS_ALIYUN_BKTEST
  else if (!ret && input.mode == 5)
    ret = !g_configured || !g_cloud_loaded ? -ENOKEY :
          product_benchmark_aliyun_config(input.data, input.size);
#endif
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
  if (ret || input.mode == 0 || input.mode == 4 || input.mode == 5 ||
      input.mode == 6)
    {
      atomic_store(&g_benchmark_session, 0);
      bkengvoice_finish(input.session, 0, ret);
    }
  syslog(LOG_NOTICE, "BKVOICE benchmark session=%lu mode=%lu submitted=%d\n",
         (unsigned long)input.session, (unsigned long)input.mode, ret);
}
