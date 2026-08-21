/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/
 * bk7258_ota_manager.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP policy/state owner above transport-specific source backends.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_OTA_MANAGER

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_ota_manager.h>
#include <arch/chip/bk7258_ota_rpmsg.h>

struct bk7258_ota_manager_s
{
  mutex_t apply_lock;
  spinlock_t status_lock;
  bool initialized;
  bool active;
  const struct bk7258_ota_source_ops_s *source;
  void *source_context;
  struct bk7258_ota_manager_status_s status;
};

struct bk7258_ota_manager_source_s
{
  struct bk7258_ota_manager_s *manager;
  const struct bk7258_ota_source_ops_s *source;
  void *context;
};

static struct bk7258_ota_manager_s g_bk7258_ota_manager =
{
  .apply_lock = NXMUTEX_INITIALIZER,
};

static void bk7258_ota_manager_status(
  struct bk7258_ota_manager_s *manager,
  enum bk7258_ota_manager_state_e state,
  const struct bk7258_ota_progress_s *progress, int error)
{
  irqstate_t flags = spin_lock_irqsave(&manager->status_lock);

  manager->status.state = state;
  manager->status.last_error = error;
  if (progress != NULL)
    {
      manager->status.phase = progress->phase;
      manager->status.image = progress->image;
      manager->status.completed = progress->completed;
      manager->status.total = progress->total;
    }

  spin_unlock_irqrestore(&manager->status_lock, flags);
}

static int bk7258_ota_manager_open(
  void *context, struct bk7258_ota_manifest_s *manifest)
{
  struct bk7258_ota_manager_source_s *wrapper = context;
  int ret;

  bk7258_ota_manager_status(wrapper->manager,
                            BK7258_OTA_MANAGER_CHECKING, NULL, 0);
  ret = wrapper->source->open(wrapper->context, manifest);
  if (ret == 0)
    {
      bk7258_ota_manager_status(wrapper->manager,
                                BK7258_OTA_MANAGER_PACKAGE_VERIFIED,
                                NULL, 0);
    }

  return ret;
}

static int bk7258_ota_manager_read(
  void *context, enum bk7258_ota_image_e image, uint32_t offset,
  uint8_t *buffer, size_t nbytes)
{
  struct bk7258_ota_manager_source_s *wrapper = context;

  return wrapper->source->read_at(wrapper->context, image, offset,
                                  buffer, nbytes);
}

static int bk7258_ota_manager_checkpoint(
  void *context, const struct bk7258_ota_progress_s *progress)
{
  struct bk7258_ota_manager_source_s *wrapper = context;
  enum bk7258_ota_manager_state_e state;
  int ret = 0;

  switch (progress->phase)
    {
      case BK7258_OTA_PHASE_WRITE_AP:
        state = BK7258_OTA_MANAGER_STAGING_AP;
        break;
      case BK7258_OTA_PHASE_WRITE_CP:
        state = BK7258_OTA_MANAGER_STAGING_CP;
        break;
      case BK7258_OTA_PHASE_COMMIT_CP:
        state = progress->completed == progress->total ?
                BK7258_OTA_MANAGER_PAIR_VERIFIED :
                BK7258_OTA_MANAGER_STAGING_CP;
        break;
      case BK7258_OTA_PHASE_COMPLETE:
        state = BK7258_OTA_MANAGER_READY_TO_REBOOT;
        break;
      default:
        state = BK7258_OTA_MANAGER_PACKAGE_VERIFIED;
        break;
    }

  bk7258_ota_manager_status(wrapper->manager, state, progress, 0);
  if (wrapper->source->checkpoint != NULL)
    {
      ret = wrapper->source->checkpoint(wrapper->context, progress);
    }

  return ret;
}

static int bk7258_ota_manager_source_cancel(void *context)
{
  struct bk7258_ota_manager_source_s *wrapper = context;

  if (wrapper->source->cancel == NULL)
    {
      return 0;
    }

  return wrapper->source->cancel(wrapper->context);
}

static void bk7258_ota_manager_close(void *context)
{
  struct bk7258_ota_manager_source_s *wrapper = context;

  if (wrapper->source->close != NULL)
    {
      wrapper->source->close(wrapper->context);
    }
}

static const struct bk7258_ota_source_ops_s g_bk7258_ota_manager_ops =
{
  .open = bk7258_ota_manager_open,
  .read_at = bk7258_ota_manager_read,
  .checkpoint = bk7258_ota_manager_checkpoint,
  .cancel = bk7258_ota_manager_source_cancel,
  .close = bk7258_ota_manager_close,
};

int bk7258_ota_manager_initialize(void)
{
  struct bk7258_ota_manager_s *manager = &g_bk7258_ota_manager;
  struct bk7258_ota_progress_s progress;

  if (manager->initialized)
    {
      return 0;
    }

  memset(&progress, 0, sizeof(progress));
  bk7258_ota_manager_status(manager, BK7258_OTA_MANAGER_IDLE,
                            &progress, 0);
  manager->initialized = true;
  return 0;
}

int bk7258_ota_manager_apply(const struct bk7258_ota_source_ops_s *source,
                             void *context, uint32_t timeout_ms)
{
  struct bk7258_ota_manager_s *manager = &g_bk7258_ota_manager;
  struct bk7258_ota_manager_source_s wrapper;
  int ret;

  if (!manager->initialized || source == NULL || source->open == NULL ||
      source->read_at == NULL || timeout_ms == 0u)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&manager->apply_lock);
  if (ret < 0)
    {
      return ret;
    }

  __atomic_store_n(&manager->source, source, __ATOMIC_RELAXED);
  __atomic_store_n(&manager->source_context, context, __ATOMIC_RELAXED);
  __atomic_store_n(&manager->active, true, __ATOMIC_RELEASE);
  wrapper.manager = manager;
  wrapper.source = source;
  wrapper.context = context;
  ret = bk7258_ota_rpmsg_stage(&g_bk7258_ota_manager_ops,
                               &wrapper, timeout_ms);
  __atomic_store_n(&manager->active, false, __ATOMIC_RELEASE);
  __atomic_store_n(&manager->source, NULL, __ATOMIC_RELAXED);
  __atomic_store_n(&manager->source_context, NULL, __ATOMIC_RELAXED);

  if (ret < 0)
    {
      bk7258_ota_manager_status(
        manager, ret == -ECANCELED ? BK7258_OTA_MANAGER_CANCELED :
        BK7258_OTA_MANAGER_FAILED, NULL, ret);
    }
  else
    {
      bk7258_ota_manager_status(manager,
                                BK7258_OTA_MANAGER_READY_TO_REBOOT,
                                NULL, 0);
    }

  nxmutex_unlock(&manager->apply_lock);
  return ret;
}

int bk7258_ota_manager_cancel(void)
{
  const struct bk7258_ota_source_ops_s *source;
  void *context;
  int source_ret = 0;
  int stage_ret;

  if (!__atomic_load_n(&g_bk7258_ota_manager.active, __ATOMIC_ACQUIRE))
    {
      return -ENOENT;
    }

  source = __atomic_load_n(&g_bk7258_ota_manager.source,
                           __ATOMIC_ACQUIRE);
  context = __atomic_load_n(&g_bk7258_ota_manager.source_context,
                            __ATOMIC_ACQUIRE);
  if (source != NULL && source->cancel != NULL)
    {
      source_ret = source->cancel(context);
    }

  stage_ret = bk7258_ota_rpmsg_cancel();
  return source_ret < 0 ? source_ret : stage_ret;
}

int bk7258_ota_manager_report_failure(int error)
{
  struct bk7258_ota_progress_s progress;

  if (!g_bk7258_ota_manager.initialized || error >= 0)
    {
      return -EINVAL;
    }
  if (__atomic_load_n(&g_bk7258_ota_manager.active, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  memset(&progress, 0, sizeof(progress));
  bk7258_ota_manager_status(&g_bk7258_ota_manager,
                            error == -ECANCELED ?
                            BK7258_OTA_MANAGER_CANCELED :
                            BK7258_OTA_MANAGER_FAILED,
                            &progress, error);
  return 0;
}

int bk7258_ota_manager_get_status(
  struct bk7258_ota_manager_status_s *status)
{
  irqstate_t flags;

  if (status == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bk7258_ota_manager.status_lock);
  memcpy(status, &g_bk7258_ota_manager.status, sizeof(*status));
  spin_unlock_irqrestore(&g_bk7258_ota_manager.status_lock, flags);
  return 0;
}

#endif /* CONFIG_BK7258_OTA_MANAGER */
