/****************************************************************************
 * app/bk7258/bk7258_display_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Product-facing Shaniu dual-eye display service.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_DISPLAY_SERVICE_H
#define __APP_BK7258_BK7258_DISPLAY_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "bk7258_display_pack.h"
#include "bk7258_display_store.h"

enum bkdisplay_service_state_e
{
  BKDISPLAY_SERVICE_STOPPED = 0,
  BKDISPLAY_SERVICE_WAITING_DEVICES,
  BKDISPLAY_SERVICE_WAITING_ASSET,
  BKDISPLAY_SERVICE_READY,
  BKDISPLAY_SERVICE_ERROR,
};

struct bkdisplay_service_status_s
{
  enum bkdisplay_service_state_e state;
  int last_error;
  bool physical_mapping_verified;
  uint8_t screen_count;
  uint32_t render_sequence;
  char expression[BKDISPLAY_EXPRESSION_SIZE];
  char pack_id[BKDISPLAY_PACK_ID_SIZE];
  uint32_t pack_revision;
  uint8_t source_sha256[32];
};

/* 107-char native QR or NULL; never exported in public status/RPC. Opening
 * retains rendered-before-success semantics. NULL is a metadata-only close:
 * the worker clears the QR later and ordinary display admission remains closed
 * until then; it never waits for the render mutex, storage or framebuffer. */
int bk7258_display_onboarding(const char *qr);
/* 0 normal, 1 long-hold/release hint, 2 saving/shutdown in progress,
 * 3 shutdown failed (resources remain stopped; explicit retry required).
 * Metadata-only admission; returns before the display worker renders. Latest
 * phase coalesces, nonzero closes ordinary admission immediately, and zero
 * reopens only after the worker has applied it and checked other overlays.
 * Never waits for the render mutex, storage or framebuffer.
 */
int bk7258_display_power(unsigned int phase);
/* Nonblocking notification; only the display worker touches framebuffer. */
void bk7258_display_speaking(bool active);
/* Atomic visual intent only; the existing display worker owns all I/O. */
void bk7258_display_focus(unsigned visual);
int bk7258_display_service_prepare(void);
int bk7258_display_service_start(void);

/* These transport-neutral calls are the future phone/Gateway adapter seam.
 * install() consumes an already-uploaded file from display/staging and also
 * activates it.  Neither call owns a network protocol.
 */

/* Bounded asynchronous expression intent. One pending/running request;
 * -EBUSY when occupied or blocked by a power/claim overlay. IDs never wrap.
 * Acceptance does not mean rendered. Only the latest request/result is kept;
 * callers must compare IDs, not attribute a newer result to an older request.
 * These requests never change the persistent default selection.
 */
enum bkdisplay_expression_request_state_e
{
  BKDISPLAY_EXPRESSION_IDLE = 0,
  BKDISPLAY_EXPRESSION_PENDING,
  BKDISPLAY_EXPRESSION_RUNNING,
  BKDISPLAY_EXPRESSION_DONE,
  BKDISPLAY_EXPRESSION_FAILED,
  BKDISPLAY_EXPRESSION_CANCELED
};
struct bkdisplay_expression_request_s
{
  uint32_t id;
  enum bkdisplay_expression_request_state_e state;
  int error;
};
int bk7258_display_request_expression(const char *expression, uint32_t *id);
int bk7258_display_expression_status(struct bkdisplay_expression_request_s *status);
/* Exact-ID cancellation only. Pending -> canceled; same canceled ID is
 * idempotent. Running returns EBUSY; completed/failed returns EALREADY;
 * a different/latest ID returns ESTALE. No render callback is interrupted.
 */
int bk7258_display_cancel_expression(uint32_t id);

/* Volatile, non-persistent trial on the same render worker. TTL starts at
 * acceptance (monotonic clock), includes queue time, and is supplied by caller.
 * Cancel returns acceptance; ACTIVE cancellation is confirmed only after restore.
 * New explicit renders/claim/power supersede the trial without restoring over
 * them. A failed restore is FAILED, not a successful cancellation/expiry.
 * One trial; status retains its latest identity, independently of render jobs.
 */
enum bkdisplay_trial_state_e
{
  BKDISPLAY_TRIAL_IDLE = 0,
  BKDISPLAY_TRIAL_PENDING,
  BKDISPLAY_TRIAL_RENDERING,
  BKDISPLAY_TRIAL_ACTIVE,
  BKDISPLAY_TRIAL_CANCEL_PENDING,
  BKDISPLAY_TRIAL_RESTORING,
  BKDISPLAY_TRIAL_EXPIRED,
  BKDISPLAY_TRIAL_CANCELED,
  BKDISPLAY_TRIAL_SUPERSEDED,
  BKDISPLAY_TRIAL_FAILED
};
struct bkdisplay_trial_status_s
{
  uint32_t id;
  enum bkdisplay_trial_state_e state;
  int error;
  uint64_t deadline_ms;
};
int bk7258_display_trial(const char *expression, uint32_t duration_ms, uint32_t *id);
/* Atomic expected-ID admission for independently authenticated clients. */
int bk7258_display_trial_checked(const char *expression, uint32_t duration_ms,
                                  uint32_t expected_id, uint32_t *id);
/* Volatile explicit installed-pack trial on the same worker and ID space.
 * Admission copies a bounded filename without I/O. Failed validation never
 * falls back or selects a default. Restore resolves the current default;
 * a newer explicit render supersedes the trial using its render identity.
 */

int bk7258_display_trial_pack_checked(const char *filename,
                                      const char *expression,
                                      uint32_t duration_ms,
                                      uint32_t expected_id, uint32_t *id);
int bk7258_display_trial_status(struct bkdisplay_trial_status_s *status);
int bk7258_display_cancel_trial(uint32_t id);

/* One asynchronous default-selection job on the existing display worker.
 * Status is a metadata-only read. Refresh explicitly queues storage I/O.
 * Store confirmation and renderer completion are separate outcomes.
 */

enum bkdisplay_selection_state_e
{
  BKDISPLAY_SELECTION_IDLE = 0,
  BKDISPLAY_SELECTION_PENDING,
  BKDISPLAY_SELECTION_PREPARING,
  BKDISPLAY_SELECTION_COMMITTING,
  BKDISPLAY_SELECTION_RENDERING,
  BKDISPLAY_SELECTION_CANCEL_PENDING,
  BKDISPLAY_SELECTION_DONE,
  BKDISPLAY_SELECTION_CANCELED,
  BKDISPLAY_SELECTION_FAILED,
  BKDISPLAY_SELECTION_UNKNOWN
};

struct bkdisplay_selection_status_s
{
  uint32_t id;
  enum bkdisplay_selection_state_e state;
  int error;
  int release_error;
  bool refresh;
  bool catalog;
  bool version_known;
  bool save_confirmed;
  bool render_confirmed;
  bool recovery_pending;
  uint64_t expected_revision;
  struct bkdisplay_selection_version_s version;
};

int bk7258_display_selection_request(const char *filename,
  uint64_t expected_revision, uint32_t expected_id, uint32_t *id);
int bk7258_display_selection_refresh(uint32_t expected_id, uint32_t *id);
int bk7258_display_selection_status(
  struct bkdisplay_selection_status_s *status);
int bk7258_display_selection_cancel(uint32_t id);

/* Explicit read-only catalog job on the same worker, admission and ID space
 * as default selection. NULL/empty cursor starts a page. Status and page reads
 * perform no I/O. Only DONE for this exact catalog ID returns a page; errors
 * zero the output. Cancel/recover use the selection APIs above/below.
 * No framebuffer is required and no render/default side effect is permitted.
 * Each page is an independent observation, not a cross-page atomic snapshot.
 */

int bk7258_display_catalog_request(const char *after,
                                    uint32_t expected_id, uint32_t *id);
int bk7258_display_catalog_page(uint32_t id,
                                 struct bkdisplay_catalog_page_s *page);


/* Explicit close-only retry for this job's uncertain volume release. The
 * original outcome stays UNKNOWN; release success does not confirm rendering
 * or durability. Duplicate pending requests coalesce. Cleanup remains allowed
 * with the business gate closed, and cannot be canceled once requested.
 */

int bk7258_display_selection_recover(uint32_t id);

int bk7258_display_set_expression(const char *expression);
/* Atomic acquisition/conditional update under the rendering mutex. A nonzero
 * identity owns the attempted render even on I/O failure. Zero means no lease.
 * New renders/overlays invalidate older leases; animation does not. Replacement
 * updates the token after its attempt and rejects stale or pending new intents.
 * IDs do not wrap. Tokens are volatile and are not persistence receipts. */
int bk7258_display_set_expression_owned(const char *expression, uint64_t *identity);
int bk7258_display_replace_expression(uint64_t *identity,
                                      const char *replacement);
int bk7258_display_show_mapping_test(void);
int bk7258_display_install(const char *filename);
int bk7258_display_activate(const char *filename);
int bk7258_display_import(const void *data, size_t size);
/* Reset the persisted user selection but retain installed packs. */
int bk7258_display_reset_selection(void);
/* Last completed service update; no render-lock wait, storage or hardware I/O.
 * In-progress rendering is not reported as a completed frame. */
int bk7258_display_get_status(struct bkdisplay_service_status_s *status);

#endif /* __APP_BK7258_BK7258_DISPLAY_SERVICE_H */
