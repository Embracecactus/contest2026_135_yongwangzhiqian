/* SPDX-License-Identifier: Apache-2.0 */
/* Shared by the Agent tool adapter and the ASR text shortcut. Keep actual
 * operation errors available to local callers; tool text is presentation.
 */

static int product_device_control(const cJSON *args, char *output,
                                  size_t capacity, int (*check)(void *),
                                  void *context)
{
  int ret = check ? check(context) : 0;
  int written = 0;
  if (ret)
    {
      return ret;
    }

  cJSON *action = cJSON_GetObjectItemCaseSensitive(args, "action");
  cJSON *value = cJSON_GetObjectItemCaseSensitive(args, "value");
  cJSON *mood = cJSON_GetObjectItemCaseSensitive(args, "mood");
  unsigned int observed = 0;
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  uint32_t eye_request_id = 0;
#endif
  if (bkagent_ota_busy())
    {
      ret = -EBUSY;
      return ret;
    }

  ret = -EINVAL;
  if (!cJSON_IsString(action))
    {
      return ret;
    }

  /* Both callers have stopped capture and have not started playback. Reuse
   * the existing product owner and keep the App's busy-turn restriction.
   */

  if (!strcmp(action->valuestring, "mood") && cJSON_IsString(mood))
    {
      for (int i = 0; i <= BK7258_PERSONA_TSUNDERE_LITE; i++)
        {
          if (!strcmp(mood->valuestring,
                      bk7258_preferences_persona_name(i)))
            {
              ret = product_apply_persona(i);
              break;
            }
        }
    }
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  else if (!strcmp(action->valuestring, "eyes"))
    {
      static const char *const expressions[] =
        {
          "neutral", "happy", "shy", "sad", "surprised", "thinking",
          "listening", "speaking", "sleepy"
        };

      cJSON *expression =
        cJSON_GetObjectItemCaseSensitive(args, "expression");
      if (cJSON_IsString(expression))
        {
          for (unsigned int i = 0;
               i < sizeof(expressions) / sizeof(expressions[0]); i++)
            {
              if (!strcmp(expression->valuestring, expressions[i]))
                {
                  /* Reserve output space before accepting a side effect. */
                  ret = capacity < 128 ? -ENOSPC :
                    product_expression_request(expressions[i], &eye_request_id,
                                               check, context);
                  break;
                }
            }
        }
    }
#endif
  else if (cJSON_IsNumber(value) &&
           value->valuedouble == value->valueint &&
           value->valueint >= 0)
    {
      if (!strcmp(action->valuestring, "volume") &&
          value->valueint <= BK7258_TOOL_VALUE_MAX)
        {
          ret = bkvoice_media_volume(true, value->valueint, &observed);
          if (!ret)
            {
              ret = bkvoice_volume_store_set(observed);
            }
        }
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
      else if (!strcmp(action->valuestring, "vibrate") &&
               value->valueint > 0 &&
               value->valueint <= BK7258_TOOL_VALUE_MAX)
        {
          ret = bkhaptic_service_pulse_wait(value->valueint);
        }
#endif
    }

  if (ret)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  if (eye_request_id)
    {
      written = snprintf(output, capacity,
        "{\"ok\":true,\"action\":\"eyes\",\"state\":\"accepted\","
        "\"request_id\":%lu,\"rendered\":false}", (unsigned long)eye_request_id);
    }
  else
#endif
  if (!strcmp(action->valuestring, "volume"))
    {
      written = snprintf(output, capacity,
                         "{\"ok\":true,\"volume_percent\":%u}",
                         observed);
    }
  else
    {
      written = snprintf(output, capacity,
                         "{\"ok\":true,\"action\":\"%s\"}",
                         action->valuestring);
    }

  return written < 0 || (size_t)written >= capacity ? -ENOSPC : 0;
}
