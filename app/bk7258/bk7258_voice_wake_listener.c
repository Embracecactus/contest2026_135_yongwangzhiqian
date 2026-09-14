/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_voice_wake_listener.h"
#include "bk7258_voice_trigger_model.h"
#include "bk7258_voice_media.h"
#include <media_trigger.h>
#include <media_defs.h>
#include <errno.h>
#include <string.h>

static void on_event(void *cookie, int event, int result, const char *extra)
{
  struct bkvoice_wake_listener_s *l = cookie;
  int expected = BKVOICE_WAKE_LISTENER_RUNNING;
  (void)extra;
  if (result < 0 || event == 0)
    {
      __atomic_store_n(&l->result, result, __ATOMIC_RELEASE);
      if (__atomic_compare_exchange_n(&l->state, &expected,
            result < 0 ? BKVOICE_WAKE_LISTENER_FAULTED :
                         BKVOICE_WAKE_LISTENER_TRIGGERED,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        (void)sem_post(l->wake);
    }
}

int bkvoice_wake_listener_initialize(struct bkvoice_wake_listener_s *l,
  const unsigned char *model, size_t model_size,
  struct bkvoice_wake_window_s *window, sem_t *wake)
{
  if (l == NULL || model == NULL || model_size == 0 || window == NULL ||
      wake == NULL) return -EINVAL;
  memset(l, 0, sizeof(*l));
  l->model = model;
  l->model_size = model_size;
  l->window = window;
  l->wake = wake;
  l->initialized = true;
  return 0;
}

int bkvoice_wake_listener_stop(struct bkvoice_wake_listener_s *l)
{
  int ret;
  if (l == NULL || !l->initialized) return -EINVAL;
  int previous = __atomic_exchange_n(&l->state,
                                     BKVOICE_WAKE_LISTENER_STOPPING,
                                     __ATOMIC_ACQ_REL);
  if (l->recognition_started)
    {
      ret = media_trigger_stop_recognition(l->trigger);
      if (ret < 0) return ret;
      l->recognition_started = false;
    }
  if (l->policy_active)
    {
      ret = bkvoice_media_source_set_active(MEDIA_SOURCE_HOTWORD, false);
      if (ret < 0) return ret;
      l->policy_active = false;
    }
  if (l->model_bound)
    {
      ret = bkvoice_trigger_model_unbind();
      if (ret < 0) return ret;
      l->model_bound = false;
    }
  if (l->model_loaded)
    {
      ret = media_trigger_unload_sound_model(l->trigger);
      if (ret < 0) return ret;
      l->model_loaded = false;
    }
  if (l->trigger != NULL)
    {
      ret = media_trigger_close(l->trigger);
      if (ret < 0) return ret;
      l->trigger = NULL;
    }
  __atomic_store_n(&l->state,
    previous == BKVOICE_WAKE_LISTENER_TRIGGERED ||
    previous == BKVOICE_WAKE_LISTENER_FAULTED ? previous :
    BKVOICE_WAKE_LISTENER_IDLE, __ATOMIC_RELEASE);
  return 0;
}

int bkvoice_wake_listener_start(struct bkvoice_wake_listener_s *l,
                                uint64_t start_ms)
{
  int ret;
  if (l == NULL || !l->initialized || start_ms == 0) return -EINVAL;
  if (l->trigger != NULL || l->policy_active || l->model_bound) return -EBUSY;
  l->trigger = media_trigger_open("default");
  if (l->trigger == NULL) { ret = -ENODEV; goto fail; }
  ret = media_trigger_set_event_callback(l->trigger, l, on_event);
  if (ret < 0) goto fail;
  ret = media_trigger_load_sound_model(l->trigger, (void *)l->model,
                                       l->model_size);
  if (ret < 0) goto fail;
  l->model_loaded = true;
  ret = bkvoice_trigger_model_bind(l->window, start_ms);
  if (ret < 0) goto fail;
  l->model_bound = true;
  __atomic_store_n(&l->result, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_RUNNING, __ATOMIC_RELEASE);
  /* Enable the device route before the recorder negotiates its link. */
  ret = bkvoice_media_source_set_active(MEDIA_SOURCE_HOTWORD, true);
  if (ret < 0) goto fail;
  l->policy_active = true;
  ret = media_trigger_start_recognition(l->trigger);
  if (ret < 0) goto fail;
  l->recognition_started = true;
  return 0;
fail:
  (void)bkvoice_wake_listener_stop(l);
  __atomic_store_n(&l->result, ret, __ATOMIC_RELEASE);
  __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_FAULTED, __ATOMIC_RELEASE);
  return ret;
}

int bkvoice_wake_listener_uninitialize(struct bkvoice_wake_listener_s *l)
{
  if (l == NULL || !l->initialized) return -EINVAL;
  int ret = bkvoice_wake_listener_stop(l);
  if (ret < 0) return ret;
  memset(l, 0, sizeof(*l));
  return 0;
}

void bkvoice_wake_listener_status(const struct bkvoice_wake_listener_s *l,
                                  struct bkvoice_wake_listener_status_s *s)
{
  if (s == NULL) return;
  memset(s, 0, sizeof(*s));
  if (l == NULL) return;
  s->state = __atomic_load_n(&l->state, __ATOMIC_ACQUIRE);
  s->result = __atomic_load_n(&l->result, __ATOMIC_ACQUIRE);
}
