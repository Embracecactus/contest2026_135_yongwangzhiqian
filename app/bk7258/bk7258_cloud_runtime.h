/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_RUNTIME_H
#define __APP_BK7258_CLOUD_RUNTIME_H
#include "bk7258_voice_config.h"
#include "bk7258_voice_ptt.h"
struct bkcloud_runtime_s;
/* Owner-thread snapshot. While the cloud worker owns the turn, turn_state is
 * UINT32_MAX (unavailable), never a fabricated IDLE or a racing turn read.
 */
struct bkcloud_runtime_status_s
{
  bool ready;
  bool armed;
  bool pressed;
  bool busy;
  bool worker_active;
  /* Automatic capture has stopped or faulted; its owner must still end or
   * cancel the turn to join the worker and release borrowed audio resources.
   */
  bool capture_finished;
  uint32_t turn_state;
  int last_error;
  bool memory_supported;
  bool memory_known;
  bool memory_enabled;
  bool memory_pending;
  bool memory_failed;
};
void bkcloud_runtime_status(const struct bkcloud_runtime_s *runtime,
                            struct bkcloud_runtime_status_s *status);
int bkcloud_runtime_create(struct bkcloud_runtime_s **runtime,
                            const void *record, size_t size,
                            struct bkvoice_config_s *trust,
                            struct bkvoice_ptt_s *ptt, sem_t *wake);
int bkcloud_runtime_connect(struct bkcloud_runtime_s *runtime);
int bkcloud_runtime_ready(struct bkcloud_runtime_s *runtime);
/* Clear returns EAGAIN while a cancelled worker is still using borrowed
 * identity/audio resources. Caller retries; never frees an in-flight job.
 */
int bkcloud_runtime_clear(struct bkcloud_runtime_s **runtime);
void bkcloud_runtime_step(struct bkcloud_runtime_s *runtime,
                          bool link, bool pressed, uint32_t epoch);
/* Serialized AP-owner automatic-capture entry points.  The caller must first
 * stop and release its continuous listening MIC path, and must enforce any
 * product OTA/provisioning gate before begin().  These functions do not
 * inspect board GPIO state and never synthesize a physical button level.
 *
 * begin() starts the existing live capture worker with the product frame
 * filter, without sending wake history. end() is retriable while joining; only
 * a successful end with captured PCM starts the cloud worker.
 */
int bkcloud_runtime_auto_begin(
  struct bkcloud_runtime_s *runtime,
  bkvoice_capture_frame_filter_t frame_filter,
  void *live_context);
int bkcloud_runtime_auto_end(struct bkcloud_runtime_s *runtime);
bool bkcloud_runtime_busy(const struct bkcloud_runtime_s *runtime);
/* Owner-thread request. Success acknowledges cancellation, not completed
 * capture/cloud/DAC teardown. step keeps busy until all borrowed work joins;
 * the connected cloud session remains usable for the next physical press.
 */
int bkcloud_runtime_cancel(struct bkcloud_runtime_s *runtime);
/* Owner-thread cancellation pump. It publishes cancellation once, advances
 * the ordinary step cleanup with a neutral disconnected input, and returns
 * EAGAIN while capture/cloud/DAC resources are still borrowed. */
int bkcloud_runtime_cancel_drain(struct bkcloud_runtime_s *runtime);
/* Committed SCB3 owner only, never candidate provisioning input. Idle-only
 * change; identical periodic refresh is safe while a worker is active.
 */
int bkcloud_runtime_memory_owner(struct bkcloud_runtime_s *runtime,
                                 const uint8_t owner[32]);
/* Success means accepted, not durable. Poll memory_pending/known/failed.
 * Erase rotates the data key and disables persistence, clearing RAM only
 * after durable policy publication. Uncertain publication requires restart.
 */
int bkcloud_runtime_memory_set(struct bkcloud_runtime_s *runtime, bool enabled, bool erase);
/* Owner-thread, idle-only deletion of RAM conversation context. Never writes
 * storage or changes credentials/persona; does not delete provider-side logs.
 */
int bkcloud_runtime_clear_history(struct bkcloud_runtime_s *runtime);
#endif
