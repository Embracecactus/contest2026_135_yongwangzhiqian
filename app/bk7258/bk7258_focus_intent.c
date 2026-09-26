/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_focus_intent.h"
#include "cJSON.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <nuttx/spinlock.h>

static spinlock_t g_lock = SP_UNLOCKED;
static struct bkfocus_intent_status_s g_status;
static struct bkfocus_request_s g_request;

int bkfocus_intent_submit(unsigned int action, uint64_t duration,
                          uint32_t *id)
{
  int ret = 0;
  irqstate_t flags;
  if (id == NULL) return -EINVAL;
  *id = 0;
  if (action < 1 || action > 4 ||
      (action == 1 ? duration == 0 : duration != 0)) return -EINVAL;
  flags = spin_lock_irqsave(&g_lock);
  if (!g_status.ready) ret = -ESHUTDOWN;
  else if (g_status.phase == 1) ret = -EBUSY;
  else if (g_status.id == UINT32_MAX) ret = -EOVERFLOW;
  else
    {
      g_status.id++;
      g_status.phase = 1;
      g_status.error = 0;
      g_request.action = action;
      g_request.duration_ms = duration;
      g_request.revision = g_status.timer.revision;
      g_request.operation = ((uint64_t)0x464f4355 << 32) | g_status.id;
      *id = g_status.id;
    }
  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

int bkfocus_intent_cancel(uint32_t id)
{
  int ret = 0;
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  if (id == 0 || id != g_status.id) ret = -ESTALE;
  else if (g_status.phase != 1) ret = -EALREADY;
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
  spin_unlock_irqrestore(&g_lock, flags);
}

int bkfocus_tool_execute(const cJSON *args, char *output,
                          size_t capacity, int (*check)(void *), void *context)
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

  if (output == NULL || capacity < 320) return -ENOSPC;
  output[0] = '\0';
  if (!cJSON_IsObject(args)) return -EINVAL;
  action = cJSON_GetObjectItemCaseSensitive(args, "action");
  seconds = cJSON_GetObjectItemCaseSensitive(args, "seconds");
  if (!cJSON_IsString(action)) return -EINVAL;
  cJSON_ArrayForEach(field, args)
    {
      unsigned int bit;
      if (field->string == NULL) return -EINVAL;
      if (!strcmp(field->string, "action")) bit = 1;
      else if (!strcmp(field->string, "seconds")) bit = 2;
      else return -EINVAL;
      if (fields & bit) return -EINVAL;
      fields |= bit;
    }
  if (!strcmp(action->valuestring, "start")) command = 1;
  else if (!strcmp(action->valuestring, "pause")) command = 2;
  else if (!strcmp(action->valuestring, "resume")) command = 3;
  else if (!strcmp(action->valuestring, "cancel")) command = 4;
  else if (strcmp(action->valuestring, "status")) return -EINVAL;
  if (command == 1)
    {
      if (!cJSON_IsNumber(seconds) || !(seconds->valuedouble >= 1) ||
          !(seconds->valuedouble <= 4294967) ||
          seconds->valuedouble != (uint64_t)seconds->valuedouble)
        return -EINVAL;
      duration = (uint64_t)seconds->valuedouble * 1000;
    }
  else if (seconds != NULL) return -EINVAL;
  ret = check ? check(context) : 0;
  if (ret) return ret < 0 ? ret : -ECANCELED;
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
      if (ret) return ret;
      ret = check ? check(context) : 0;
      if (ret)
        {
          if (bkfocus_intent_cancel(id) == 0) return -ECANCELED;
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
