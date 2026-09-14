/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_VOICE_WAKE_LISTENER_H
#define __APP_BK7258_VOICE_WAKE_LISTENER_H

#include "bk7258_voice_wake_window.h"
#include <semaphore.h>

enum bkvoice_wake_listener_state_e
{
  BKVOICE_WAKE_LISTENER_IDLE = 0,
  BKVOICE_WAKE_LISTENER_RUNNING,
  BKVOICE_WAKE_LISTENER_STOPPING,
  BKVOICE_WAKE_LISTENER_TRIGGERED,
  BKVOICE_WAKE_LISTENER_FAULTED,
};

struct bkvoice_wake_listener_status_s
{
  enum bkvoice_wake_listener_state_e state;
  int result;
};

struct bkvoice_wake_listener_s
{
  void *trigger;
  const unsigned char *model;
  size_t model_size;
  struct bkvoice_wake_window_s *window;
  sem_t *wake;
  volatile int state;
  volatile int result;
  bool model_loaded;
  bool model_bound;
  bool recognition_started;
  bool policy_active;
  bool initialized;
};

/* The product owner serializes lifecycle calls. Official Media Trigger owns
 * the recorder, PCM polling and model execution. stop() closes and joins its
 * notification channel before a subsequent start, so old callbacks cannot
 * enter a new listening generation. Model bytes and window remain borrowed
 * until successful uninitialize(). No MIC is opened by initialize(). */
int bkvoice_wake_listener_initialize(struct bkvoice_wake_listener_s *listener,
  const unsigned char *model, size_t model_size,
  struct bkvoice_wake_window_s *window, sem_t *wake);
int bkvoice_wake_listener_start(struct bkvoice_wake_listener_s *listener,
                                uint64_t start_ms);
int bkvoice_wake_listener_stop(struct bkvoice_wake_listener_s *listener);
int bkvoice_wake_listener_uninitialize(struct bkvoice_wake_listener_s *listener);
void bkvoice_wake_listener_status(const struct bkvoice_wake_listener_s *listener,
                                  struct bkvoice_wake_listener_status_s *status);
#endif
