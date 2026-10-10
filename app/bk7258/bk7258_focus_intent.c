/****************************************************************************
 * app/bk7258/bk7258_focus_intent.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_focus_intent.h"
#include "cJSON.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static spinlock_t g_lock = SP_UNLOCKED;
static struct bkfocus_intent_status_s g_status;
static struct bkfocus_request_s g_request;
static struct
{
  uint64_t request_id;
  unsigned int action;
  uint64_t duration;
  uint32_t intent_id;
  bool valid;
} g_text_receipt;
static uint64_t g_text_latest_request_id;

static int text_number(const char *text, size_t size, uint64_t *value)
{
  uint64_t parsed = 0;

  if (size == 0 || value == NULL)
    {
      return -EINVAL;
    }

  for (size_t i = 0; i < size; i++)
    {
      if (text[i] < '0' || text[i] > '9' ||
          parsed > (UINT64_MAX - (text[i] - '0')) / 10)
        {
          return -EINVAL;
        }

      parsed = parsed * 10 + (text[i] - '0');
    }

  *value = parsed;
  return 0;
}

static int text_start(const char *text, size_t size, unsigned int *action,
                      uint64_t *duration)
{
  static const char focused[] = "开始专注";
  static const char leading[] = "开始";
  static const char trailing[] = "专注";
  static const char seconds[] = "秒";
  static const char minutes[] = "分钟";
  const char *number = NULL;
  size_t number_size = 0;
  size_t unit_size = 0;
  uint64_t value;

  if (size > sizeof(focused) - 1 &&
      !memcmp(text, focused, sizeof(focused) - 1))
    {
      number = text + sizeof(focused) - 1;
      number_size = size - (sizeof(focused) - 1);
    }
  else if (size > sizeof(leading) - 1 + sizeof(trailing) - 1 &&
           !memcmp(text, leading, sizeof(leading) - 1) &&
           !memcmp(text + size - (sizeof(trailing) - 1), trailing,
                   sizeof(trailing) - 1))
    {
      number = text + sizeof(leading) - 1;
      number_size = size - (sizeof(leading) - 1) -
                    (sizeof(trailing) - 1);
    }
  else
    {
      return 0;
    }

  if (number_size >= sizeof(minutes) - 1 &&
      !memcmp(number + number_size - (sizeof(minutes) - 1), minutes,
              sizeof(minutes) - 1))
    {
      unit_size = sizeof(minutes) - 1;
    }
  else if (number_size >= sizeof(seconds) - 1 &&
           !memcmp(number + number_size - (sizeof(seconds) - 1), seconds,
                   sizeof(seconds) - 1))
    {
      unit_size = sizeof(seconds) - 1;
    }
  else
    {
      return 0;
    }

  number_size -= unit_size;
  if (text_number(number, number_size, &value) < 0 || value == 0)
    {
      return 0;
    }

  if (unit_size == sizeof(minutes) - 1)
    {
      if (value > 4294967u / 60u)
        {
          return 0;
        }

      value *= 60u;
    }

  if (value > 4294967u)
    {
      return 0;
    }

  *action = 1;
  *duration = value * 1000u;
  return 1;
}

int bkfocus_text_parse(const char *text, unsigned int *action,
                       uint64_t *duration)
{
  size_t begin = 0;
  size_t end;
  size_t size;
  bool question = false;
  int ret;

  if (text == NULL || action == NULL || duration == NULL)
    {
      return 0;
    }

  size = strlen(text);
  while (begin < size && (text[begin] == ' ' || text[begin] == '\t' ||
                          text[begin] == '\r' || text[begin] == '\n'))
    {
      begin++;
    }

  end = size;
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r' || text[end - 1] == '\n'))
    {
      end--;
    }

  if (end > begin && (text[end - 1] == '.' || text[end - 1] == '!' ||
                      text[end - 1] == '?'))
    {
      question = text[end - 1] == '?';
      end--;
    }
  else if (end - begin >= 3 &&
           (!memcmp(text + end - 3, "。", 3) ||
            !memcmp(text + end - 3, "！", 3) ||
            !memcmp(text + end - 3, "？", 3)))
    {
      question = !memcmp(text + end - 3, "？", 3);
      end -= 3;
    }

  if (end == begin)
    {
      return 0;
    }

  size = end - begin;
  text += begin;
  if (size == strlen("暂停专注") && !memcmp(text, "暂停专注", size))
    {
      if (question)
        {
          return 0;
        }

      *action = 2;
      *duration = 0;
      return 1;
    }

  if (size == strlen("继续专注") && !memcmp(text, "继续专注", size))
    {
      if (question)
        {
          return 0;
        }

      *action = 3;
      *duration = 0;
      return 1;
    }

  if ((size == strlen("取消当前专注计时") &&
       !memcmp(text, "取消当前专注计时", size)) ||
      (size == strlen("取消专注") && !memcmp(text, "取消专注", size)))
    {
      if (question)
        {
          return 0;
        }

      *action = 4;
      *duration = 0;
      return 1;
    }

  if (size == strlen("专注还剩多久") && !memcmp(text, "专注还剩多久", size))
    {
      *action = 0;
      *duration = 0;
      return 1;
    }

  ret = text_start(text, size, action, duration);
  return ret > 0 && !question ? ret : 0;
}

static int intent_submit_locked(unsigned int action, uint64_t duration,
                                uint32_t *id)
{
  if (!g_status.ready)
    {
      return -ESHUTDOWN;
    }

  if (g_status.phase == 1)
    {
      return -EBUSY;
    }

  if (g_status.id == UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  g_status.id++;
  g_status.phase = 1;
  g_status.error = 0;
  g_request.action = action;
  g_request.duration_ms = duration;
  g_request.revision = g_status.timer.revision;
  g_request.operation = ((uint64_t)0x464f4355 << 32) | g_status.id;
  *id = g_status.id;
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkfocus_intent_submit(unsigned int action, uint64_t duration,
                          uint32_t *id)
{
  int ret = 0;
  irqstate_t flags;

  if (id == NULL)
    {
      return -EINVAL;
    }

  *id = 0;
  if (action < 1 || action > 4 ||
      (action == 1 ? duration == 0 : duration != 0))
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_lock);
  ret = intent_submit_locked(action, duration, id);
  if (ret == 0)
    {
      g_text_receipt.valid = false;
    }

  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

int bkfocus_intent_cancel(uint32_t id)
{
  int ret = 0;
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  if (id == 0 || id != g_status.id)
    {
      ret = -ESTALE;
    }
  else if (g_status.phase != 1)
    {
      ret = -EALREADY;
    }
  else
    {
      g_status.phase = 4;
      g_status.error = -ECANCELED;
    }

  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

void bkfocus_intent_status(struct bkfocus_intent_status_s *status)
{
  irqstate_t flags;

  if (status == NULL) return;

  flags = spin_lock_irqsave(&g_lock);
  *status = g_status;
  spin_unlock_irqrestore(&g_lock, flags);
}

int bkfocus_intent_text(const char *text, uint64_t request_id, uint64_t now,
                        struct bkfocus_intent_status_s *status)
{
  unsigned int action = 0;
  uint64_t duration = 0;
  irqstate_t flags;
  int ret;

  ret = bkfocus_text_parse(text, &action, &duration);
  if (ret == 0)
    {
      return 0;
    }

  if (status == NULL || request_id == 0)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_lock);
  if (request_id < g_text_latest_request_id)
    {
      ret = -ESTALE;
    }
  else if (!g_status.ready)
    {
      ret = -ESHUTDOWN;
    }
  else if (g_status.observed_ms > now || now - g_status.observed_ms > 250)
    {
      ret = -ESTALE;
    }
  else if (request_id == g_text_latest_request_id)
    {
      ret = g_text_receipt.valid &&
            g_text_receipt.action == action &&
            g_text_receipt.duration == duration ? 1 : -ESTALE;
    }
  else if (action == 0)
    {
      ret = 1;
    }
  else
    {
      uint32_t id;
      ret = intent_submit_locked(action, duration, &id);
      if (ret == 0)
        {
          ret = 1;
        }
    }

  if (ret == 1)
    {
      g_text_latest_request_id = request_id;
      g_text_receipt.request_id = request_id;
      g_text_receipt.action = action;
      g_text_receipt.duration = duration;
      g_text_receipt.intent_id = g_status.id;
      g_text_receipt.valid = true;
    }

  *status = g_status;
  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

void bkfocus_intent_step(uint64_t now, bool admitted)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  g_status.ready = admitted;
  if (g_status.phase == 1)
    {
      if (!admitted)
        {
          g_status.phase = 4;
          g_status.error = -ESHUTDOWN;
        }
      else
        {
          /* Execute is bounded arithmetic only, on the product owner.
           * Holding this short lock makes cancellation versus apply exact.
           */

          g_status.error = bkfocus_execute(&g_request, now);
          g_status.phase = g_status.error == 0 ? 2 : 3;
        }
    }

  (void)bkfocus_snapshot(&g_status.timer, now);
  g_status.observed_ms = now;
  g_status.visual = bkfocus_visual(now);
  spin_unlock_irqrestore(&g_lock, flags);
}

int bkfocus_tool_execute(const cJSON *args, char *output,
                          size_t capacity, int (*check)(void *),
                          void *context)
{
  const cJSON *action;
  const cJSON *seconds;
  const cJSON *field;
  unsigned int command = 0;
  unsigned int fields = 0;
  uint64_t duration = 0;
  uint32_t id;
  int ret;
  int written;
  bool late = false;

  if (output == NULL || capacity < 320)
    {
      return -ENOSPC;
    }

  output[0] = '\0';
  if (!cJSON_IsObject(args))
    {
      return -EINVAL;
    }

  action = cJSON_GetObjectItemCaseSensitive(args, "action");
  seconds = cJSON_GetObjectItemCaseSensitive(args, "seconds");
  if (!cJSON_IsString(action))
    {
      return -EINVAL;
    }

  cJSON_ArrayForEach(field, args)
    {
      unsigned int bit;
      if (field->string == NULL)
        {
          return -EINVAL;
        }

      if (!strcmp(field->string, "action"))
        {
          bit = 1;
        }
      else if (!strcmp(field->string, "seconds"))
        {
          bit = 2;
        }
      else return -EINVAL;
      if (fields & bit)
        {
          return -EINVAL;
        }

      fields |= bit;
    }

  if (!strcmp(action->valuestring, "start"))
    {
      command = 1;
    }
  else if (!strcmp(action->valuestring, "pause"))
    {
      command = 2;
    }
  else if (!strcmp(action->valuestring, "resume"))
    {
      command = 3;
    }
  else if (!strcmp(action->valuestring, "cancel"))
    {
      command = 4;
    }
  else if (strcmp(action->valuestring, "status"))
    {
      return -EINVAL;
    }

  if (command == 1)
    {
      if (!cJSON_IsNumber(seconds) || !(seconds->valuedouble >= 1) ||
          !(seconds->valuedouble <= 4294967) ||
          seconds->valuedouble != (uint64_t)seconds->valuedouble)
        {
          return -EINVAL;
        }

      duration = (uint64_t)seconds->valuedouble * 1000;
    }
  else if (seconds != NULL)
    {
      return -EINVAL;
    }

  ret = check ? check(context) : 0;
  if (ret)
    {
      return ret < 0 ? ret : -ECANCELED;
    }

  if (command == 0)
    {
      struct bkfocus_intent_status_s status;
      bkfocus_intent_status(&status);
      written = snprintf(output, capacity,
        "{\"ready\":%s,\"request_id\":%lu,\"phase\":%u,\"error\":%d,"
        "\"timer_state\":%u,\"revision\":\"%llu\","
        "\"remaining_ms_at_observation\":%llu,\"observed_ms\":%llu}",
        status.ready ? "true" : "false", (unsigned long)status.id,
        status.phase, status.error, status.timer.state,
        (unsigned long long)status.timer.revision,
        (unsigned long long)status.timer.remaining_ms,
        (unsigned long long)status.observed_ms);
    }
  else
    {
      ret = bkfocus_intent_submit(command, duration, &id);
      if (ret)
        {
          return ret;
        }

      ret = check ? check(context) : 0;
      if (ret)
        {
          if (bkfocus_intent_cancel(id) == 0)
            {
              return -ECANCELED;
            }

          /* Applied actions are independent timers, not retractable by
           * closing a conversation. Report uncertainty instead of rollback.
           */

          late = true;
        }

      written = snprintf(output, capacity,
        "{\"state\":\"accepted\",\"request_id\":%lu,"
        "\"completed\":false,\"cancellation\":\"%s\"}",
        (unsigned long)id, late ? "too_late" : "not_requested");
    }

  return written < 0 || (size_t)written >= capacity ? -ENOSPC : 0;
}
