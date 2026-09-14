/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_VOICE_WAKE_OWNER_H
#define __APP_BK7258_VOICE_WAKE_OWNER_H

#include "bk7258_cloud_runtime.h"
#include "bk7258_voice_wake_listener.h"

#include <semaphore.h>

/* The AP runtime owns step(), stop(), and close().  The listener owns the
 * continuous-listen MIC until stop() succeeds; the cloud capture worker owns
 * it after auto_begin().  This object only borrows all three contexts. */
enum bkvoice_wake_owner_state_e
{
  BKVOICE_WAKE_OWNER_IDLE = 0,
  BKVOICE_WAKE_OWNER_LISTENING,
  BKVOICE_WAKE_OWNER_CAPTURE,
  BKVOICE_WAKE_OWNER_DRAINING,
  BKVOICE_WAKE_OWNER_CLOSED,
};

/* Stored atomically by the capture worker.  Window events retain their public
 * values; FAULT is private because the window API reports faults as errno. */
#define BKVOICE_WAKE_OWNER_EVENT_FAULT 0x80u

struct bkvoice_wake_owner_s
{
  struct bkvoice_wake_listener_s *listener;
  struct bkvoice_wake_window_s *window;
  struct bkcloud_runtime_s *cloud;
  sem_t *wake;
  volatile unsigned int event;
  volatile int observer_error;
  uint64_t live_next_ms;
  uint64_t retry_after_ms;
  int last_error;
  enum bkvoice_wake_owner_state_e state;
  bool initialized;
  bool listener_started;
  bool trigger_consumed;
  bool fault_consumed;
  bool automatic_started;
  bool automatic_owned;
  bool cancel_requested;
  volatile bool terminal_latched;
};

int bkvoice_wake_owner_initialize(struct bkvoice_wake_owner_s *owner,
                                  struct bkvoice_wake_listener_s *listener,
                                  struct bkvoice_wake_window_s *window,
                                  struct bkcloud_runtime_s *cloud,
                                  sem_t *wake);

/* allowed is the product owner's cloud/provision/OTA gate.  This module does
 * not authenticate, create a model, or infer that gate from GPIO. */
int bkvoice_wake_owner_step(struct bkvoice_wake_owner_s *owner, bool allowed,
                            uint64_t now_ms);
int bkvoice_wake_owner_cancel(struct bkvoice_wake_owner_s *owner);
/* Release only this owner's continuous listener.  It never cancels a cloud
 * turn, including a physical/debug turn started outside this owner. */
int bkvoice_wake_owner_suspend(struct bkvoice_wake_owner_s *owner);
int bkvoice_wake_owner_stop(struct bkvoice_wake_owner_s *owner);
int bkvoice_wake_owner_close(struct bkvoice_wake_owner_s *owner);

/* Installed in bkcloud_runtime_auto_begin().  It runs in the capture worker:
 * it only feeds the endpoint window and publishes a terminal event. */
int bkvoice_wake_owner_frame_filter(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES]);

#endif
