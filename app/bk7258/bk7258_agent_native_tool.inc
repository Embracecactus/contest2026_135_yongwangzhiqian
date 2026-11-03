/* SPDX-License-Identifier: Apache-2.0 */
/* Agent tool and recognized-text route for installed native apps.
 *
 * Both run on the voice/Agent thread, which may not be CPU0, so neither
 * enters the native manager.  Reads use the metadata catalog published by
 * the manager; every side effect is one request in the shared native intent
 * slot, applied by the product loop on CPU0 under its own admission.
 */

#ifdef CONFIG_BK7258_NATIVE_APPS

#define PRODUCT_NATIVE_WAIT_MS 1500u
#define PRODUCT_NATIVE_CONTROL_MS 2000u

static const char *const g_native_repeat_names[] =
{
  "daily", "weekdays", "weekends"
};

static const uint32_t g_native_repeat_days[] =
{
  BKNATIVE_DAYS_ALL, 0x3e, 0x41
};

/* Exact installed ID or exact display name; never a fuzzy guess. */

static int product_native_resolve(const struct bknative_catalog_s *catalog,
                                  const cJSON *app, unsigned int *index)
{
  if (!catalog->valid)
    {
      return -ENODEV;
    }

  if (!cJSON_IsString(app) || !app->valuestring[0])
    {
      return -EINVAL;
    }

  for (unsigned int i = 0; i < catalog->count &&
       i < BKNATIVE_CATALOG_APPS; i++)
    {
      if (!strcmp(app->valuestring, catalog->apps[i].id) ||
          !strcmp(app->valuestring, catalog->apps[i].name))
        {
          *index = i;
          return 0;
        }
    }

  return -ENOENT;
}

static const struct bknative_catalog_app_s *
product_native_app(const struct bknative_catalog_s *catalog, uint32_t hash)
{
  for (unsigned int i = 0; i < catalog->count &&
       i < BKNATIVE_CATALOG_APPS; i++)
    {
      if (catalog->apps[i].hash == hash)
        {
          return &catalog->apps[i];
        }
    }

  return NULL;
}

/* Control-only requests are applied by the next product-loop pass; wait a
 * bounded time for that pass so the reply reports the actual outcome.
 */

static int product_native_wait(uint32_t id, int (*check)(void *),
                               void *context)
{
  for (unsigned int waited = 0; waited < PRODUCT_NATIVE_WAIT_MS;
       waited += 50)
    {
      struct bknative_intent_status_s status;
      int ret = check ? check(context) : 0;
      if (ret)
        {
          (void)bknative_intent_cancel(id);
          return ret;
        }

      bknative_intent_status(&status);
      if (status.id != id)
        {
          return -ESTALE;
        }

      if (status.phase >= 2)
        {
          return status.phase == 2 ? 0 : status.error ? status.error : -EIO;
        }

      usleep(50000);
    }

  (void)bknative_intent_cancel(id);
  return -ETIMEDOUT;
}

static int product_native_time(const cJSON *value, uint32_t *minute)
{
  const char *text = cJSON_IsString(value) ? value->valuestring : NULL;
  if (!text || strlen(text) != 5 || text[2] != ':' ||
      text[0] < '0' || text[0] > '2' || text[1] < '0' || text[1] > '9' ||
      text[3] < '0' || text[3] > '5' || text[4] < '0' || text[4] > '9')
    {
      return -EINVAL;
    }

  uint32_t hour = (uint32_t)(text[0] - '0') * 10u + (uint32_t)(text[1] - '0');
  if (hour > 23)
    {
      return -EINVAL;
    }

  *minute = hour * 60u + (uint32_t)(text[3] - '0') * 10u +
            (uint32_t)(text[4] - '0');
  return 0;
}

static cJSON *product_native_catalog_json(
  const struct bknative_catalog_s *catalog)
{
  cJSON *root = cJSON_CreateObject();
  cJSON *apps = root ? cJSON_AddArrayToObject(root, "apps") : NULL;
  cJSON *schedule = apps ? cJSON_AddArrayToObject(root, "schedule") : NULL;
  bool ok = schedule != NULL;

  for (unsigned int i = 0; ok && i < catalog->count &&
       i < BKNATIVE_CATALOG_APPS; i++)
    {
      const struct bknative_catalog_app_s *app = &catalog->apps[i];
      cJSON *row = cJSON_CreateObject();
      ok = row && cJSON_AddItemToArray(apps, row);
      if (!ok)
        {
          cJSON_Delete(row);
          break;
        }

      cJSON *labels = cJSON_AddArrayToObject(row, "results");
      ok = cJSON_AddStringToObject(row, "id", app->id) &&
           cJSON_AddStringToObject(row, "name", app->name) &&
           cJSON_AddBoolToObject(row, "blocked", app->blocked) && labels;
      for (unsigned int label = 0; ok && label < app->result_count; label++)
        {
          ok = cJSON_AddItemToArray(labels,
                 cJSON_CreateString(app->results[label]));
        }
    }

  for (unsigned int slot = 0; ok && slot < BKNATIVE_SCHEDULE_MAX; slot++)
    {
      const struct bknative_catalog_schedule_s *entry =
        &catalog->schedule[slot];
      const struct bknative_catalog_app_s *app;
      const char *repeat = "custom";
      char time[8];
      cJSON *row;

      if (!entry->app_hash)
        {
          continue;
        }

      for (unsigned int r = 0; r < 3; r++)
        {
          if (entry->days == g_native_repeat_days[r])
            {
              repeat = g_native_repeat_names[r];
            }
        }

      app = product_native_app(catalog, entry->app_hash);
      snprintf(time, sizeof(time), "%02u:%02u",
               (unsigned int)((entry->minute % 1440u) / 60u),
               (unsigned int)(entry->minute % 60u));
      row = cJSON_CreateObject();
      ok = row && cJSON_AddItemToArray(schedule, row);
      if (!ok)
        {
          cJSON_Delete(row);
          break;
        }

      ok = cJSON_AddStringToObject(row, "app", app ? app->id : "") &&
           cJSON_AddStringToObject(row, "time", time) &&
           cJSON_AddStringToObject(row, "repeat", repeat) &&
           cJSON_AddNumberToObject(row, "seconds", entry->seconds) &&
           cJSON_AddStringToObject(row, "last",
             entry->last_result == 1 ? "not_yet" :
             entry->last_result == 0 ? "started" : "skipped");
    }

  if (ok)
    {
      const struct bknative_catalog_app_s *last = NULL;
      for (unsigned int i = 0; i < catalog->count &&
           i < BKNATIVE_CATALOG_APPS; i++)
        {
          if (!strcmp(catalog->apps[i].id, catalog->last_app))
            {
              last = &catalog->apps[i];
            }
        }

      const char *label = last && catalog->last_result == 0 &&
        catalog->last_value && catalog->last_value <= last->result_count ?
        last->results[catalog->last_value - 1] : "";
      ok = cJSON_AddStringToObject(root, "running", catalog->running) &&
           cJSON_AddStringToObject(root, "last_app", catalog->last_app) &&
           cJSON_AddNumberToObject(root, "last_error",
             catalog->last_result < 0 ? catalog->last_result : 0) &&
           cJSON_AddStringToObject(root, "last_result", label) &&
           cJSON_AddBoolToObject(root, "clock_valid", catalog->clock_valid);
    }

  if (!ok)
    {
      cJSON_Delete(root);
      root = NULL;
    }

  return root;
}

static int product_native_tool(const cJSON *args, char *output,
                               size_t capacity, int (*check)(void *),
                               void *context)
{
  struct bknative_catalog_s *catalog;
  struct bknative_request_s request;
  cJSON *action = cJSON_GetObjectItemCaseSensitive(args, "action");
  cJSON *reply = NULL;
  char *text = NULL;
  unsigned int index = 0;
  uint32_t id = 0;
  int ret = check ? check(context) : 0;

  if (ret)
    {
      return ret;
    }

  if (!cJSON_IsString(action))
    {
      return -EINVAL;
    }

  catalog = malloc(sizeof(*catalog));
  if (!catalog)
    {
      return -ENOMEM;
    }

  bknative_catalog_read(catalog);
  memset(&request, 0, sizeof(request));
  if (!strcmp(action->valuestring, "list"))
    {
      reply = catalog->valid ? product_native_catalog_json(catalog) : NULL;
      ret = !catalog->valid ? -ENODEV : reply ? 0 : -ENOMEM;
      goto out;
    }

  ret = product_native_resolve(catalog,
          cJSON_GetObjectItemCaseSensitive(args, "app"), &index);
  if (ret)
    {
      goto out;
    }

  request.app_hash = catalog->apps[index].hash;
  request.window_ms = PRODUCT_NATIVE_CONTROL_MS;
  if (!strcmp(action->valuestring, "start"))
    {
      cJSON *seconds = cJSON_GetObjectItemCaseSensitive(args, "seconds");
      if (seconds && (!cJSON_IsNumber(seconds) ||
          seconds->valuedouble != seconds->valueint ||
          seconds->valueint < 0 ||
          seconds->valueint > (int)BKNATIVE_INTENT_SECONDS_MAX))
        {
          ret = -EINVAL;
          goto out;
        }

      /* Apps run only while voice is idle: this start waits for the end of
       * the current reply and is not running when the tool returns.
       */

      request.kind = BKNATIVE_INTENT_START;
      request.seconds = seconds ? (uint32_t)seconds->valueint : 0;
      request.window_ms = BKNATIVE_INTENT_VOICE_MS;
      ret = catalog->apps[index].blocked ? -EPERM :
            bknative_intent_request(&request, &id);
      if (!ret)
        {
          reply = cJSON_CreateObject();
          ret = reply &&
            cJSON_AddBoolToObject(reply, "ok", true) &&
            cJSON_AddStringToObject(reply, "state", "accepted") &&
            cJSON_AddStringToObject(reply, "app", catalog->apps[index].id) &&
            cJSON_AddNumberToObject(reply, "request_id", id) &&
            cJSON_AddStringToObject(reply, "starts",
                                    "after_this_reply_finishes") ? 0 :
            -ENOMEM;
        }

      goto out;
    }

  if (!strcmp(action->valuestring, "stop"))
    {
      request.kind = BKNATIVE_INTENT_STOP;
    }
  else if (!strcmp(action->valuestring, "unschedule"))
    {
      request.kind = BKNATIVE_INTENT_SCHEDULE_CLEAR;
    }
  else if (!strcmp(action->valuestring, "schedule"))
    {
      cJSON *repeat = cJSON_GetObjectItemCaseSensitive(args, "repeat");
      cJSON *seconds = cJSON_GetObjectItemCaseSensitive(args, "seconds");
      request.kind = BKNATIVE_INTENT_SCHEDULE_SET;
      request.days = repeat ? 0 : BKNATIVE_DAYS_ALL;
      for (unsigned int r = 0; repeat && r < 3; r++)
        {
          if (cJSON_IsString(repeat) &&
              !strcmp(repeat->valuestring, g_native_repeat_names[r]))
            {
              request.days = g_native_repeat_days[r];
            }
        }

      ret = product_native_time(cJSON_GetObjectItemCaseSensitive(args,
                                "time"), &request.minute);
      if (!ret && (!request.days || (seconds &&
          (!cJSON_IsNumber(seconds) ||
           seconds->valuedouble != seconds->valueint ||
           seconds->valueint < 0 ||
           seconds->valueint > (int)BKNATIVE_INTENT_SECONDS_MAX))))
        {
          ret = -EINVAL;
        }

      request.seconds = seconds && !ret ? (uint32_t)seconds->valueint : 0;
      if (ret)
        {
          goto out;
        }
    }
  else
    {
      ret = -EINVAL;
      goto out;
    }

  ret = bknative_intent_request(&request, &id);
  if (!ret)
    {
      ret = product_native_wait(id, check, context);
    }

  /* A voice turn already stops a running app (apps need voice idle), so a
   * spoken "stop" usually finds nothing running: that is not a failure.
   */

  bool idle = request.kind == BKNATIVE_INTENT_STOP && ret == -EALREADY;
  if (!ret || idle)
    {
      reply = cJSON_CreateObject();
      ret = reply && cJSON_AddBoolToObject(reply, "ok", true) &&
            cJSON_AddStringToObject(reply, "action", action->valuestring) &&
            cJSON_AddStringToObject(reply, "app",
                                    catalog->apps[index].id) &&
            (!idle || cJSON_AddStringToObject(reply, "state",
                                              "not_running")) ? 0 : -ENOMEM;
    }

out:
  if (!ret)
    {
      text = cJSON_PrintUnformatted(reply);
      ret = !text ? -ENOMEM : strlen(text) >= capacity ? -ENOSPC : 0;
      if (!ret)
        {
          strcpy(output, text);
        }
    }

  free(text);
  cJSON_Delete(reply);
  free(catalog);
  return ret;
}

/* Recognized text such as "帮我做个决定" or "打开喝水提醒": a matched
 * phrase is handled locally (no LLM/TTS) and never falls through.
 */

static pthread_mutex_t g_native_text_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_native_text_request;
static int g_native_text_result;

static int product_native_text(uint64_t request, const char *text,
                               int (*check)(void *), void *context)
{
  struct bknative_catalog_s *catalog = malloc(sizeof(*catalog));
  struct bknative_request_s start;
  uint32_t hash = 0;
  uint32_t id;
  int ret;

  if (!catalog)
    {
      return 0;
    }

  bknative_catalog_read(catalog);
  bool matched = bknative_text_match(text, catalog, &hash);
  const struct bknative_catalog_app_s *app =
    matched ? product_native_app(catalog, hash) : NULL;
  bool blocked = app && app->blocked;
  free(catalog);
  if (!matched)
    {
      return 0;
    }

  /* Matched: from here the phrase is owned locally, even on failure. */

  ret = check ? check(context) : 0;
  if (ret)
    {
      return ret;
    }

  pthread_mutex_lock(&g_native_text_lock);
  if (request && request == g_native_text_request)
    {
      ret = g_native_text_result;   /* replay: never submit twice */
    }
  else
    {
      memset(&start, 0, sizeof(start));
      start.kind = BKNATIVE_INTENT_START;
      start.app_hash = hash;
      start.window_ms = BKNATIVE_INTENT_VOICE_MS;
      ret = blocked ? -EPERM : bknative_intent_request(&start, &id);
      g_native_text_request = request;
      g_native_text_result = ret;
    }

  pthread_mutex_unlock(&g_native_text_lock);
  return ret ? ret : 1;
}

#endif
