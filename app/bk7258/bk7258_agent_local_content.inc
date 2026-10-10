/* SPDX-License-Identifier: Apache-2.0 */

/* The config task remains the sole local capture lifecycle owner. Outbound
 * playback performs its own bounded I/O after this owner releases capture.
 */

static bool g_content_capture_paused;
static uint32_t g_content_feedback;
static uint64_t g_content_resume_at;

static bool product_content_step(uint64_t now, bool admitted, bool *completed)
{
  struct bkcontent_status_s status;
  bool busy;
  int ret;

  *completed = false;
  if (!g_trigger_started) g_content_capture_paused = false;
  bkcontent_step(admitted);
  bkcontent_status(&status);
  if (status.phase == BKCONTENT_PENDING && admitted)
    {
      ret = g_trigger_started ? bk7258_agent_trigger_pause_local() : 0;
      if (!ret)
        {
          g_content_capture_paused = g_trigger_started;
          (void)bkcontent_ready(status.id);
        }
      else
        {
          (void)bkcontent_cancel(status.id);
          g_product_error = ret;
        }
    }

  busy = bkcontent_busy();
  if (!busy && g_content_capture_paused && admitted &&
      now >= g_content_resume_at)
    {
      ret = bkcontent_quiesce();
      bkcontent_step(admitted);
      if (!ret) ret = bk7258_agent_trigger_rearm();
      if (!ret) g_content_capture_paused = false;
      else
        {
          g_product_error = ret;
          g_content_resume_at = now + 1000;
        }
    }

  if (status.phase >= BKCONTENT_DONE && status.id != g_content_feedback)
    {
      g_content_feedback = status.id;
      *completed = status.phase == BKCONTENT_DONE;
      syslog(LOG_INFO, "BKCONTENT id=%lu phase=%u result=%d\n",
             (unsigned long)status.id, status.phase, status.error);
    }

  return busy || g_content_capture_paused;
}
