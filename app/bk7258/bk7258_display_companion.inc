/* SPDX-License-Identifier: Apache-2.0 */

void bk7258_display_activity(unsigned state)
{
  if (state <= 2) atomic_store(&g_activity, state);
}

int bk7258_display_companion(unsigned visual, unsigned duration_ms)
{
  uint64_t now = bkdisplay_now_ms();
  int ret = 0;

  if (visual > 5 || (visual && (duration_ms == 0 || duration_ms > 1000)))
    {
      return -EINVAL;
    }

  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (visual && (!g_bkdisplay_intent_allowed ||
      bkdisplay_selection_busy_locked() || bkdisplay_trial_busy() ||
      g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_PENDING ||
      g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_RUNNING ||
      atomic_load(&g_speaking) || atomic_load(&g_activity)))
    {
      ret = -EBUSY;
    }
  else if (visual && atomic_load(&g_focus_visual))
    {
      ret = (atomic_load(&g_focus_visual) >> 8) >= 3 ? -EALREADY : -EBUSY;
    }
  else
    {
      g_companion_visual = visual;
      g_companion_started = now;
      g_companion_deadline = now + duration_ms;
    }

  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return ret;
}

/* A higher priority renderer has already taken the screen. Do not restore
 * an old base over it, or replay a discarded action when it exits.
 */

static void bkdisplay_companion_preempt(struct bkdisplay_service_s *service)
{
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  g_companion_visual = 0;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  service->companion_painted = 0;
}

static bool bkdisplay_companion_step(struct bkdisplay_service_s *service,
                                     uint64_t now)
{
  static const uint8_t frames[] = {0, 3, 1, 4, 4, 1, 3, 1};
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (now < g_companion_started || now >= g_companion_deadline)
    {
      g_companion_visual = 0;
    }

  unsigned visual = g_companion_visual;
  unsigned activity = atomic_load(&g_activity);
  if (activity)
    {
      g_companion_visual = 0;
      visual = 5 + activity;
    }
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  if (service->companion_painted && service->companion_identity !=
      g_bkdisplay_expression_identity)
    {
      bkdisplay_companion_preempt(service);
      return false;
    }

  if (service->status.state != BKDISPLAY_SERVICE_READY ||
      (visual == 0 && service->companion_painted == 0))
    {
      return false;
    }

  if (service->companion_painted != visual)
    {
      uint16_t *pixels = service->frames[frames[visual]];
      if (pixels == NULL) pixels = service->frames[0];
      int ret = pixels ? bkdisplay_framebuffer_write(BKDISPLAY_FB0, pixels) :
                         -ENODATA;
      if (ret == 0) ret = bkdisplay_framebuffer_write(BKDISPLAY_FB1, pixels);
      if (ret == 0)
        {
          service->companion_painted = visual;
          service->companion_identity = g_bkdisplay_expression_identity;
          service->status.render_sequence++;
          syslog(LOG_INFO, "BKDISPLAY companion=%u rendered=1\n", visual);
        }
      else
        {
          service->status.last_error = ret;
          bkdisplay_companion_preempt(service);
        }
    }

  return true;
}
