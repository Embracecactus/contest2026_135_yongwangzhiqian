/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/spinlock.h>

/* Only this fixed-size copy is protected by the snapshot lock. The rendering
 * lock may cover external I/O and must never be acquired by status readers.
 */
static spinlock_t g_bkdisplay_snapshot_lock = SP_UNLOCKED;
static struct bkdisplay_service_status_s g_bkdisplay_snapshot;

static void bkdisplay_unlock(struct bkdisplay_service_s *service)
{
  irqstate_t flags = spin_lock_irqsave(&g_bkdisplay_snapshot_lock);
  g_bkdisplay_snapshot = service->status;
  spin_unlock_irqrestore(&g_bkdisplay_snapshot_lock, flags);
  nxmutex_unlock(&service->lock);
}

int bk7258_display_get_status(struct bkdisplay_service_status_s *status)
{
  irqstate_t flags;

  if (status == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bkdisplay_snapshot_lock);
  *status = g_bkdisplay_snapshot;
  spin_unlock_irqrestore(&g_bkdisplay_snapshot_lock, flags);
  return 0;
}
