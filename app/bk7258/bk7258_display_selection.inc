/****************************************************************************
 * app/bk7258/bk7258_display_selection.inc
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Existing display mutex held.
 * Never hold the metadata spinlock during I/O.
 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Called before overlays and device readiness checks by the same owner.
 * Clearing a release latch requires actual close confirmation.
 * A timer cannot establish release.
 */

static bool bkdisplay_selection_recover_step(
  struct bkdisplay_service_s *service)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (!g_bkdisplay_selection.recovery_pending)
    {
      spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
      return false;
    }

  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  ret = bkdisplay_volume_close(service);

  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  g_bkdisplay_selection.release_error = ret;
  g_bkdisplay_selection.recovery_pending = false;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return true;
}

static bool bkdisplay_catalog_canceled(void *context)
{
  irqstate_t flags;
  bool canceled;

  (void)context;
  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  canceled = !g_bkdisplay_intent_allowed ||
             g_bkdisplay_selection.state == BKDISPLAY_SELECTION_CANCEL_PENDING;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return canceled;
}

static bool bkdisplay_selection_step(struct bkdisplay_service_s *service,
                                     bool devices_ready)
{
  struct bkdisplay_selection_status_s request;
  struct bkdisplay_selection_version_s version;
  irqstate_t flags;
  bool canceled;
  bool committed = false;
  bool attempted = false;
  int ret;
  int release_error;

  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if (g_bkdisplay_selection.release_error)
    {
      spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
      return true; /* Uncertain lease is not permission for another job. */
    }

  if (g_bkdisplay_selection.state != BKDISPLAY_SELECTION_PENDING)
    {
      spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
      return false;
    }

  request = g_bkdisplay_selection;
  g_bkdisplay_selection.state = BKDISPLAY_SELECTION_PREPARING;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);

  ret = bkdisplay_volume_open(service);
  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  canceled =
    g_bkdisplay_selection.state == BKDISPLAY_SELECTION_CANCEL_PENDING ||
             !g_bkdisplay_intent_allowed;
  if (ret == 0 && !canceled && !request.refresh && !request.catalog)
    g_bkdisplay_selection.state = BKDISPLAY_SELECTION_COMMITTING;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);

  if (ret == 0 && !canceled)
    {
      attempted = !request.refresh && !request.catalog;
      if (request.catalog)
        {
          ret = bkdisplay_store_catalog_page(BKDISPLAY_MOUNTPOINT,
            g_bkdisplay_catalog_after, bkdisplay_catalog_canceled, NULL,
            &g_bkdisplay_catalog_page);
        }
      else
        {
          ret = request.refresh ?
            bkdisplay_store_selection_version(BKDISPLAY_MOUNTPOINT, &version) :
            bkdisplay_store_activate_checked(BKDISPLAY_MOUNTPOINT,
              request.version.filename, request.expected_revision, &version);
        }

      committed = attempted && ret == 0;
    }

  release_error = bkdisplay_volume_close(service);

  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  if ((request.refresh || request.catalog) &&
      (g_bkdisplay_selection.state == BKDISPLAY_SELECTION_CANCEL_PENDING ||
       !g_bkdisplay_intent_allowed)) canceled = true;
  if (ret == 0 && !canceled && !request.catalog)
    {
      g_bkdisplay_selection.version = version;
      g_bkdisplay_selection.version_known = true;
      g_bkdisplay_selection.save_confirmed = committed;
    }

  if (committed) bkdisplay_trial_supersede();
  bool render = committed && release_error == 0 &&
                g_bkdisplay_intent_allowed;
  if (render) g_bkdisplay_selection.state = BKDISPLAY_SELECTION_RENDERING;
  if (committed && !render && !release_error) ret = -ECANCELED;
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);

  if (render)
    {
      ret = devices_ready ? bkdisplay_render_locked(service, "neutral") :
                            -ENODEV;
      if (service->volume_leased || service->volume_mounted)
        release_error = ret < 0 ? ret : -EBUSY;
    }

  flags = spin_lock_irqsave(&g_bkdisplay_intent_lock);
  g_bkdisplay_selection.release_error = release_error;
  g_bkdisplay_selection.error = ret < 0 ? ret : release_error;
  g_bkdisplay_selection.render_confirmed = render && ret == 0;
  if (release_error)
    g_bkdisplay_selection.state = BKDISPLAY_SELECTION_UNKNOWN;
  else if (canceled)
    {
      g_bkdisplay_selection.state = BKDISPLAY_SELECTION_CANCELED;
      g_bkdisplay_selection.error = -ECANCELED;
    }
  else if (ret == 0)
    {
      g_bkdisplay_selection.state = BKDISPLAY_SELECTION_DONE;
    }
  else if (attempted && !committed && ret != -ESTALE && ret != -EINVAL &&
           ret != -EOVERFLOW && ret != -EPROTO && ret != -EEXIST &&
           ret != -EBADMSG && ret != -ENOENT && ret != -ENAMETOOLONG)
    g_bkdisplay_selection.state = BKDISPLAY_SELECTION_UNKNOWN;
  else
    g_bkdisplay_selection.state = BKDISPLAY_SELECTION_FAILED;
  if (request.catalog &&
      g_bkdisplay_selection.state != BKDISPLAY_SELECTION_DONE)
    memset(&g_bkdisplay_catalog_page, 0, sizeof(g_bkdisplay_catalog_page));
  spin_unlock_irqrestore(&g_bkdisplay_intent_lock, flags);
  return true;
}
