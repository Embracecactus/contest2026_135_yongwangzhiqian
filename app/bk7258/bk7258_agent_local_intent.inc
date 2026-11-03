/* SPDX-License-Identifier: Apache-2.0 */
/* Route recognized text through existing owners, without another voice state
 * machine or worker. A matched failure must never fall through to the Agent.
 */

struct product_local_request_s
{
  uint64_t id;
  int (*status)(uint64_t);
};

static pthread_mutex_t g_local_intent_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_local_intent_request;
static struct bklocal_intent_s g_local_intent_receipt;
static int g_local_intent_result;

static int product_local_check(void *context)
{
  struct product_local_request_s *request = context;
  return request->status(request->id);
}

static int product_local_apply(const struct bklocal_intent_s *intent,
                               struct product_local_request_s *request)
{
  char input[96];
  char output[160];
  unsigned int observed;
  cJSON *args;
  int value = intent->value;
  int ret;

  if (intent->kind == BKLOCAL_STOP)
    {
      /* This is a recognized-text cancellation, not acoustic barge-in. */

      return product_cancel();
    }

  if (intent->kind == BKLOCAL_VOLUME)
    {
      if (intent->relative)
        {
          ret = bkvoice_media_volume(false, 0, &observed);
          if (ret)
            {
              return ret;
            }

          value += (int)observed;
          value = value < 0 ? 0 : value > 100 ? 100 : value;
        }

      snprintf(input, sizeof(input),
               "{\"action\":\"volume\",\"value\":%d}", value);
    }
  else
    {
      snprintf(input, sizeof(input),
               "{\"action\":\"eyes\",\"expression\":\"%s\"}",
               intent->expression);
    }

  if (tool_guard_check("device_control", input, strlen(input)) != GUARD_ALLOW)
    {
      return -EACCES;
    }

  ret = product_local_check(request);
  if (ret)
    {
      return ret;
    }

  args = cJSON_Parse(input);
  if (!args)
    {
      return -ENOMEM;
    }

  tool_guard_record_call("device_control");
  ret = product_device_control(args, output, sizeof(output),
                               product_local_check, request);
  cJSON_Delete(args);
  return ret;
}

static int product_local_text(uint64_t request, const char *text,
                              int (*request_status)(uint64_t))
{
  struct bklocal_intent_s intent;
  struct product_local_request_s context = {request, request_status};
  uint64_t started = bkvoice_config_now_ms(NULL);
  int ret = product_profile_begin(request, text, request_status);
  bool replay = false;

  if (!ret)
    {
      ret = product_focus_text(request, text, request_status);
    }

  if (ret)
    {
      return ret;
    }

  if (!bklocal_intent_parse(text, &intent))
    {
#ifdef CONFIG_BK7258_NATIVE_APPS
      /* Installed-app phrases come after the built-in vocabulary, so an
       * app name can never shadow focus, volume, eyes or stop.
       */

      if (request && request_status)
        {
          ret = product_native_text(request, text, product_local_check,
                                    &context);
          if (ret)
            {
              atomic_store(&g_local_text_done, true);
              syslog(LOG_INFO,
                "BKLOCAL request=%llu kind=app result=%d elapsed_ms=%llu "
                "llm=0 tts=0\n", (unsigned long long)request,
                ret < 0 ? ret : 0,
                (unsigned long long)(bkvoice_config_now_ms(NULL) - started));
              return ret;
            }
        }

#endif
      syslog(LOG_INFO,
        "BKLOCAL request=%llu kind=0 route=agent elapsed_ms=%llu\n",
        (unsigned long long)request,
        (unsigned long long)(bkvoice_config_now_ms(NULL) - started));
      return 0;
    }

  if (!request || !request_status)
    {
      return -EINVAL;
    }

  ret = product_local_check(&context);
  if (ret)
    {
      return ret;
    }

  pthread_mutex_lock(&g_local_intent_lock);
  ret = product_local_check(&context);
  if (!ret && request <= g_local_intent_request)
    {
      /* Retain even failed receipts: persistence may fail after the physical
       * volume changed. Retrying that relative operation would apply twice.
       * Expression strings come from the parser's static vocabulary.
       */

      replay = true;
      ret = request == g_local_intent_request &&
            intent.kind == g_local_intent_receipt.kind &&
            intent.value == g_local_intent_receipt.value &&
            intent.relative == g_local_intent_receipt.relative &&
            intent.expression == g_local_intent_receipt.expression ?
            g_local_intent_result : -ESTALE;
    }
  else if (!ret)
    {
      ret = product_local_apply(&intent, &context);
      g_local_intent_request = request;
      g_local_intent_receipt = intent;
      g_local_intent_result = ret;
    }

  pthread_mutex_unlock(&g_local_intent_lock);
  atomic_store(&g_local_text_done, true);
  syslog(LOG_INFO,
    "BKLOCAL request=%llu kind=%u result=%d replay=%u elapsed_ms=%llu "
    "llm=0 tts=0\n", (unsigned long long)request, (unsigned int)intent.kind,
    ret, (unsigned int)replay,
    (unsigned long long)(bkvoice_config_now_ms(NULL) - started));
  return ret ? ret : 1;
}
