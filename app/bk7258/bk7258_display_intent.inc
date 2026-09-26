/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/spinlock.h>

static spinlock_t g_bkdisplay_intent_lock = SP_UNLOCKED;
static struct bkdisplay_expression_request_s g_bkdisplay_intent;
static char g_bkdisplay_intent_expression[BKDISPLAY_EXPRESSION_SIZE];
static bool g_bkdisplay_intent_allowed;

static struct bkdisplay_trial_status_s g_bkdisplay_trial;
static uint64_t g_bkdisplay_trial_started;
static uint64_t g_bkdisplay_trial_identity;
static char g_bkdisplay_trial_restore[BKDISPLAY_EXPRESSION_SIZE];

/* Called under intent lock. Terminals never reopen; new request gets a new ID. */
static bool bkdisplay_trial_busy(void)
{
  return g_bkdisplay_trial.state >= BKDISPLAY_TRIAL_PENDING &&
         g_bkdisplay_trial.state <= BKDISPLAY_TRIAL_RESTORING;
}
static void bkdisplay_trial_supersede(void)
{
  if (bkdisplay_trial_busy())
    {
      g_bkdisplay_trial.state = BKDISPLAY_TRIAL_SUPERSEDED;
      g_bkdisplay_trial.error = -ESTALE;
    }
}
int bk7258_display_trial_status(struct bkdisplay_trial_status_s *status)
{
  if (status == NULL) return -EINVAL;
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  *status = g_bkdisplay_trial;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return 0;
}
int bk7258_display_cancel_trial(uint32_t id)
{
  if (id == 0) return -EINVAL;
  int ret = 0;
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (id != g_bkdisplay_trial.id) ret = -ESTALE;
  else if (g_bkdisplay_trial.state == BKDISPLAY_TRIAL_PENDING)
    {
      g_bkdisplay_intent.state = BKDISPLAY_EXPRESSION_CANCELED;
      g_bkdisplay_intent.error = -ECANCELED;
      g_bkdisplay_trial.state = BKDISPLAY_TRIAL_CANCELED;
    }
  else if (g_bkdisplay_trial.state == BKDISPLAY_TRIAL_ACTIVE)
    g_bkdisplay_trial.state = BKDISPLAY_TRIAL_CANCEL_PENDING;
  else if (g_bkdisplay_trial.state == BKDISPLAY_TRIAL_RENDERING ||
           g_bkdisplay_trial.state == BKDISPLAY_TRIAL_RESTORING) ret = -EBUSY;
  else if (g_bkdisplay_trial.state != BKDISPLAY_TRIAL_CANCEL_PENDING &&
           g_bkdisplay_trial.state != BKDISPLAY_TRIAL_CANCELED) ret = -EALREADY;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return ret;
}

static void bkdisplay_intent_gate(bool allowed)
{
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  g_bkdisplay_intent_allowed = allowed;
  if (!allowed) bkdisplay_trial_supersede();
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
  bkdisplay_trial_supersede();
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
      if (g_bkdisplay_trial.id == id && g_bkdisplay_trial.state == BKDISPLAY_TRIAL_PENDING)
        g_bkdisplay_trial.state = BKDISPLAY_TRIAL_CANCELED;
    }
  else if (g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_RUNNING)
    ret = -EBUSY;
  else if (g_bkdisplay_intent.state != BKDISPLAY_EXPRESSION_CANCELED)
    ret = -EALREADY;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return ret;
}

static int bkdisplay_request(const char *expression, uint32_t *id,
                              uint32_t duration_ms, uint64_t now,
                              bool check_id, uint32_t expected_id)
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
  if (duration_ms && (now == 0 || now > UINT64_MAX - duration_ms))
    return now == 0 ? -EAGAIN : -EOVERFLOW;

  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (check_id && expected_id != g_bkdisplay_trial.id) ret = -ESTALE;
  else if ((duration_ms && bkdisplay_trial_busy()) || !g_bkdisplay_intent_allowed ||
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
      if (duration_ms)
        {
          g_bkdisplay_trial = (struct bkdisplay_trial_status_s) {
            .id = *id, .state = BKDISPLAY_TRIAL_PENDING,
            .deadline_ms = now + duration_ms
          };
          g_bkdisplay_trial_started = now;
          g_bkdisplay_trial_identity = 0;
          g_bkdisplay_trial_restore[0] = '\0';
        }
    }
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return ret;
}

int bk7258_display_request_expression(const char *expression, uint32_t *id)
{
  return bkdisplay_request(expression, id, 0, 0, false, 0);
}
int bk7258_display_trial(const char *expression, uint32_t duration_ms, uint32_t *id)
{
  if (duration_ms == 0) return -EINVAL;
  return bkdisplay_request(expression, id, duration_ms, bkdisplay_now_ms(), false, 0);
}

int bk7258_display_trial_checked(const char *expression, uint32_t duration_ms,
                                  uint32_t expected_id, uint32_t *id)
{
  if (duration_ms == 0) return -EINVAL;
  return bkdisplay_request(expression, id, duration_ms, bkdisplay_now_ms(), true, expected_id);
}

/* Existing worker, rendering mutex held; short intent lock never covers I/O. */
static bool bkdisplay_trial_step(struct bkdisplay_service_s *service,
                                 bool devices_ready)
{
  uint64_t now = bkdisplay_now_ms();
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (g_bkdisplay_trial.state != BKDISPLAY_TRIAL_ACTIVE &&
      g_bkdisplay_trial.state != BKDISPLAY_TRIAL_CANCEL_PENDING)
    { spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags); return false; }
  if (g_bkdisplay_trial_identity != g_bkdisplay_expression_identity ||
      g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_PENDING)
    {
      bkdisplay_trial_supersede();
      spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
      return true;
    }
  bool canceled = g_bkdisplay_trial.state == BKDISPLAY_TRIAL_CANCEL_PENDING;
  bool clock_error = now == 0 || now < g_bkdisplay_trial_started;
  if (!canceled && !clock_error && now < g_bkdisplay_trial.deadline_ms)
    { spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags); return false; }
  g_bkdisplay_trial.state = BKDISPLAY_TRIAL_RESTORING;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  int ret = devices_ready ? bkdisplay_render_locked(service, g_bkdisplay_trial_restore) : -ENODEV;
  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  g_bkdisplay_trial.error = ret < 0 ? ret : clock_error ? -EIO : 0;
  g_bkdisplay_trial.state = g_bkdisplay_trial.error ? BKDISPLAY_TRIAL_FAILED :
    canceled ? BKDISPLAY_TRIAL_CANCELED : BKDISPLAY_TRIAL_EXPIRED;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return true;
}

/* Called only by the existing display worker with its rendering lock held.
 * No lock used by request/status callers is held during storage or rendering.
 */
static bool bkdisplay_intent_step(struct bkdisplay_service_s *service,
                                  bool devices_ready)
{
  char expression[BKDISPLAY_EXPRESSION_SIZE];
  uint64_t now = bkdisplay_now_ms();
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (g_bkdisplay_intent.state != BKDISPLAY_EXPRESSION_PENDING)
    {
      spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
      return false;
    }
  bool trial = g_bkdisplay_trial.id == g_bkdisplay_intent.id &&
               g_bkdisplay_trial.state == BKDISPLAY_TRIAL_PENDING;
  if (trial)
    {
      if (now == 0 || now < g_bkdisplay_trial_started || now >= g_bkdisplay_trial.deadline_ms)
        {
          g_bkdisplay_intent.state = BKDISPLAY_EXPRESSION_CANCELED;
          g_bkdisplay_intent.error = now >= g_bkdisplay_trial.deadline_ms ? -ETIMEDOUT : -EIO;
          g_bkdisplay_trial.state = now >= g_bkdisplay_trial.deadline_ms ? BKDISPLAY_TRIAL_EXPIRED : BKDISPLAY_TRIAL_FAILED;
          g_bkdisplay_trial.error = g_bkdisplay_intent.error;
          spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
          return true;
        }
      memcpy(g_bkdisplay_trial_restore, service->status.expression, sizeof(g_bkdisplay_trial_restore));
      g_bkdisplay_trial.state = BKDISPLAY_TRIAL_RENDERING;
    }
  g_bkdisplay_intent.state = BKDISPLAY_EXPRESSION_RUNNING;
  memcpy(expression, g_bkdisplay_intent_expression, sizeof(expression));
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);

  int ret = !devices_ready ? -ENODEV : trial && !g_bkdisplay_trial_restore[0] ?
            -ENODATA : bkdisplay_render_locked(service, expression);
  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (trial)
    {
      g_bkdisplay_trial.error = ret;
      g_bkdisplay_trial.state = ret < 0 ? BKDISPLAY_TRIAL_FAILED : BKDISPLAY_TRIAL_ACTIVE;
      g_bkdisplay_trial_identity = g_bkdisplay_expression_identity;
    }
  g_bkdisplay_intent.error = ret;
  g_bkdisplay_intent.state = ret < 0 ? BKDISPLAY_EXPRESSION_FAILED : BKDISPLAY_EXPRESSION_DONE;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return true;
}
