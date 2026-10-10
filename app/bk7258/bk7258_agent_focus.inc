/* SPDX-License-Identifier: Apache-2.0 */
/* Voice text is already recognized. This adapter has no recognizer, timer,
 * cloud client or worker of its own. The product owner still applies intents.
 */
static pthread_mutex_t g_focus_feedback_lock = PTHREAD_MUTEX_INITIALIZER;
static struct bkfocus_intent_status_s g_focus_feedback;
static uint64_t g_focus_feedback_until;
static unsigned int g_focus_feedback_state;
static atomic_bool g_focus_local_done;

static void product_focus_feedback(const struct bkfocus_intent_status_s *value,
                                   int result, uint64_t now)
{
  pthread_mutex_lock(&g_focus_feedback_lock);
  g_focus_feedback = *value;
  g_focus_feedback_state = result == -EINPROGRESS ? 6 :
                           result ? 5 : value->timer.state;
  g_focus_feedback_until = now + 3000;
  pthread_mutex_unlock(&g_focus_feedback_lock);
}

static unsigned int product_focus_visual(uint64_t now, bool idle,
                                         unsigned int ordinary)
{
  unsigned int visual = ordinary;
  pthread_mutex_lock(&g_focus_feedback_lock);
  if (idle && now < g_focus_feedback_until &&
      now >= g_focus_feedback.observed_ms && (ordinary >> 8) <= 3)
    {
      uint64_t remaining = g_focus_feedback.timer.remaining_ms;
      uint64_t age = now - g_focus_feedback.observed_ms;
      if (g_focus_feedback_state == 1)
        {
          remaining = age < remaining ? remaining - age : 0;
        }

      visual = BKFOCUS_STATUS_VISUAL | (g_focus_feedback_state << 24) |
               (unsigned int)((remaining + 999) / 1000);
    }

  pthread_mutex_unlock(&g_focus_feedback_lock);
  return visual;
}

static int product_focus_text(uint64_t request, const char *text,
                              int (*request_status)(uint64_t))
{
  struct bkfocus_intent_status_s status = {0};
  static const char *const names[] =
    {"status", "start", "pause", "resume", "cancel"};
  unsigned int action;
  uint64_t duration;
  uint64_t started = bkvoice_config_now_ms(NULL);
  char input[80];
  int ret;

  if (!bkfocus_text_parse(text, &action, &duration)) return 0;
  if (!request_status || !request) return -EINVAL;
  ret = request_status(request);
  if (ret) return ret;
  if (action == 1)
    snprintf(input, sizeof(input), "{\"action\":\"start\",\"seconds\":%llu}",
             (unsigned long long)(duration / 1000));
  else
    snprintf(input, sizeof(input), "{\"action\":\"%s\"}", names[action]);

  /* Keep the same tool enable/rate guard as the Agent's registry path. */
  if (tool_guard_check("focus_timer", input, strlen(input)) != GUARD_ALLOW)
    ret = -EACCES;
  else
    {
      ret = request_status(request);
      if (!ret)
        ret = bkfocus_intent_text(text, request,
                                  bkvoice_config_now_ms(NULL), &status);
      if (ret == 1)
        {
          uint32_t intent = status.id;
          tool_guard_record_call("focus_timer");
          sem_post(&g_product_wake);
          while (action && status.phase == 1)
            {
              ret = request_status(request);
              if (ret)
                {
                  /* Closing a voice turn can cancel a pending request, but
                   * never undo an already applied independent timer. */
                  (void)bkfocus_intent_cancel(intent);
                  break;
                }

              uint64_t now = bkvoice_config_now_ms(NULL);
              if (now - started >= 500)
                {
                  int canceled = bkfocus_intent_cancel(intent);

                  /* The owner may have applied this intent after the last
                   * observation. Confirm its receipt before declaring the
                   * result unknown; a successful cancel proves no apply.
                   */

                  bkfocus_intent_status(&status);
                  ret = canceled == 0 ? -ETIMEDOUT :
                        status.id != intent ? -EINPROGRESS : 0;
                  break;
                }

              nxsig_usleep(10000);
              bkfocus_intent_status(&status);
              if (status.id != intent)
                {
                  ret = -EINPROGRESS;
                  break;
                }
            }

          if (ret >= 0)
            {
              uint64_t now = bkvoice_config_now_ms(NULL);
              ret = !status.ready ? -ESHUTDOWN :
                    now < status.observed_ms ||
                    now - status.observed_ms > 250 ? -EINPROGRESS :
                    action && status.phase != 2 ?
                    (status.error ? status.error : -EINPROGRESS) : 0;
            }
        }
    }

  uint64_t finished = bkvoice_config_now_ms(NULL);
  if (ret && !status.observed_ms) status.observed_ms = finished;
  product_focus_feedback(&status, ret, finished);
  atomic_store(&g_focus_local_done, true);
  syslog(LOG_INFO,
    "BKFOCUS request=%llu action=%u result=%d phase=%u state=%u "
    "revision=%llu remaining_ms=%llu observed_ms=%llu confirm_ms=%llu "
    "llm=0 tts=0\n", (unsigned long long)request, action, ret, status.phase,
    status.timer.state, (unsigned long long)status.timer.revision,
    (unsigned long long)status.timer.remaining_ms,
    (unsigned long long)status.observed_ms,
    (unsigned long long)(finished - started));
  return ret ? ret : 1;
}
