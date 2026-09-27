/****************************************************************************
 * app/bk7258/bk7258_display_selection_request.inc
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Included under the existing display metadata owner.
 * No storage calls belong in the request path.
 */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bkdisplay_selection_status_s g_bkdisplay_selection;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline bool bkdisplay_selection_storage_blocked(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  bool blocked = g_bkdisplay_selection.release_error != 0;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return blocked;
}

static bool bkdisplay_selection_busy_locked(void)
{
  return g_bkdisplay_selection.release_error != 0 ||
    (g_bkdisplay_selection.state >= BKDISPLAY_SELECTION_PENDING &&
     g_bkdisplay_selection.state <= BKDISPLAY_SELECTION_CANCEL_PENDING);
}

static void bkdisplay_selection_cancel_locked(void)
{
  if (g_bkdisplay_selection.state == BKDISPLAY_SELECTION_PENDING)
    {
      g_bkdisplay_selection.state = BKDISPLAY_SELECTION_CANCELED;
      g_bkdisplay_selection.error = -ECANCELED;
    }
  else if (g_bkdisplay_selection.state == BKDISPLAY_SELECTION_PREPARING)
    {
      g_bkdisplay_selection.state = BKDISPLAY_SELECTION_CANCEL_PENDING;
    }
}

static int bkdisplay_selection_request(const char *filename,
                                       uint64_t revision,
                                       uint32_t expected_id, uint32_t *id)
{
  size_t length = 0;
  irqstate_t flags;
  int ret = 0;

  if (id == NULL) return -EINVAL;
  if (filename != NULL)
    {
      length = strnlen(filename, BKDISPLAY_STORE_FILENAME_SIZE);
      if (length <= 5 || length >= BKDISPLAY_STORE_FILENAME_SIZE ||
          filename[0] < 'a' || filename[0] > 'z' ||
          strcmp(filename + length - 5, ".bkep")) return -EINVAL;
      for (size_t i = 0; i < length; i++)
        {
          char c = filename[i];
          if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '-' || c == '_')) return -EINVAL;
        }

      if (revision == UINT64_MAX) return -EOVERFLOW;
    }

  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (expected_id != g_bkdisplay_selection.id) ret = -ESTALE;
  else if (!g_bkdisplay_intent_allowed ||
           bkdisplay_selection_busy_locked() ||
           g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_PENDING ||
           g_bkdisplay_intent.state == BKDISPLAY_EXPRESSION_RUNNING)
    ret = -EBUSY;
  else if (g_bkdisplay_selection.id == UINT32_MAX) ret = -EOVERFLOW;
  else
    {
      uint32_t next = g_bkdisplay_selection.id + 1;
      memset(&g_bkdisplay_selection, 0, sizeof(g_bkdisplay_selection));
      g_bkdisplay_selection.id = next;
      g_bkdisplay_selection.state = BKDISPLAY_SELECTION_PENDING;
      g_bkdisplay_selection.refresh = filename == NULL;
      g_bkdisplay_selection.expected_revision = revision;
      if (filename != NULL)
        memcpy(g_bkdisplay_selection.version.filename, filename, length + 1);
      *id = next;
    }

  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_display_selection_request(const char *filename,
  uint64_t expected_revision, uint32_t expected_id, uint32_t *id)
{
  if (filename == NULL) return -EINVAL;
  return bkdisplay_selection_request(filename, expected_revision,
                                      expected_id, id);
}

int bk7258_display_selection_refresh(uint32_t expected_id, uint32_t *id)
{
  return bkdisplay_selection_request(NULL, 0, expected_id, id);
}

int bk7258_display_selection_status(
  struct bkdisplay_selection_status_s *status)
{
  irqstate_t flags;
  if (status == NULL) return -EINVAL;
  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  *status = g_bkdisplay_selection;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return 0;
}

int bk7258_display_selection_cancel(uint32_t id)
{
  irqstate_t flags;
  int ret = 0;
  if (id == 0) return -EINVAL;
  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (id != g_bkdisplay_selection.id) ret = -ESTALE;
  else if (g_bkdisplay_selection.state == BKDISPLAY_SELECTION_COMMITTING ||
           g_bkdisplay_selection.state == BKDISPLAY_SELECTION_RENDERING)
    ret = -EBUSY;
  else if (g_bkdisplay_selection.state == BKDISPLAY_SELECTION_PENDING ||
           g_bkdisplay_selection.state == BKDISPLAY_SELECTION_PREPARING)
    bkdisplay_selection_cancel_locked();
  else if (g_bkdisplay_selection.state != BKDISPLAY_SELECTION_CANCELED &&
           g_bkdisplay_selection.state != BKDISPLAY_SELECTION_CANCEL_PENDING)
    ret = -EALREADY;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return ret;
}
