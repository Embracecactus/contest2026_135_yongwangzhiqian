/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/spinlock.h>

static spinlock_t g_bkdisplay_intent_lock = SP_UNLOCKED;
static struct bkdisplay_expression_request_s g_bkdisplay_intent;
static char g_bkdisplay_intent_expression[BKDISPLAY_EXPRESSION_SIZE];
static bool g_bkdisplay_intent_allowed;

static void bkdisplay_intent_gate(bool allowed)
{
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  g_bkdisplay_intent_allowed = allowed;
  if (!allowed && g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_PENDING)
    {
      g_bkdisplay_intent.state = BKDISPLAY_EXPRESSION_CANCELED;
      g_bkdisplay_intent.error = -ECANCELED;
    }
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
}

/* A later explicit synchronous expression must not be overwritten by an
 * earlier queued request. A running request already owns the rendering lock.
 */
static bool bkdisplay_intent_pending(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  bool pending = g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_PENDING ||
                 g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_RUNNING;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return pending;
}

static void bkdisplay_intent_supersede(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_PENDING)
    {
      g_bkdisplay_intent.state = BKDISPLAY_EXPRESSION_CANCELED;
      g_bkdisplay_intent.error = -ECANCELED;
    }
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
}

int bk7258_display_expression_status(struct bkdisplay_expression_request_s *status)
{
  if (status == NULL) return -EINVAL;
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  *status = g_bkdisplay_intent;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return 0;
}

int bk7258_display_cancel_expression(uint32_t id)
{
  int ret = 0;
  if (id == 0) return -EINVAL;
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (id != g_bkdisplay_intent.id) ret = -ESTALE;
  else if (g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_PENDING)
    {
      g_bkdisplay_intent.state = BKDISPLAY_EXPRESSION_CANCELED;
      g_bkdisplay_intent.error = -ECANCELED;
    }
  else if (g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_RUNNING)
    ret = -EBUSY;
  else if (g_bkdisplay_intent.state != BKDISPLAY_EXPRESSION_CANCELED)
    ret = -EALREADY;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return ret;
}

int bk7258_display_request_expression(const char *expression, uint32_t *id)
{
  static const char *const allowed[] = {
    "neutral", "happy", "shy", "sad", "surprised", "thinking",
    "listening", "speaking", "sleepy"
  };
  bool valid = false;
  int ret = 0;
  if (expression == NULL || id == NULL) return -EINVAL;
  for (unsigned i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
    if (strcmp(expression, allowed[i]) == 0) valid = true;
  if (!valid) return -EINVAL;

  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (!g_bkdisplay_intent_allowed ||
      g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_PENDING ||
      g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_RUNNING)
    ret = -EBUSY;
  else if (g_bkdisplay_intent.id == UINT32_MAX)
    ret = -EOVERFLOW;
  else
    {
      g_bkdisplay_intent.id++;
      g_bkdisplay_intent.state = BKDISPLAY_EXPRESSION_PENDING;
      g_bkdisplay_intent.error = 0;
      strcpy(g_bkdisplay_intent_expression, expression);
      *id = g_bkdisplay_intent.id;
    }
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return ret;
}

/* Called only by the existing display worker with its rendering lock held.
 * No lock used by request/status callers is held during storage or rendering.
 */
static bool bkdisplay_intent_step(struct bkdisplay_service_s *service,
                                  bool devices_ready)
{
  char expression[BKDISPLAY_EXPRESSION_SIZE];
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (g_bkdisplay_intent.state != BKDISPLAY_EXPRESSION_PENDING)
    {
      spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
      return false;
    }
  g_bkdisplay_intent.state = BKDISPLAY_EXPRESSION_RUNNING;
  memcpy(expression, g_bkdisplay_intent_expression, sizeof(expression));
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);

  int ret = devices_ready ? bkdisplay_render_locked(service, expression) : -ENODEV;
  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  g_bkdisplay_intent.error = ret;
  g_bkdisplay_intent.state = ret < 0 ? BKDISPLAY_EXPRESSION_FAILED : BKDISPLAY_EXPRESSION_DONE;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return true;
}
