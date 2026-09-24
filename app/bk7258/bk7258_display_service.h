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

/* 107-char native QR or NULL; never exported in public status/RPC. */
int bk7258_display_onboarding(const char *qr);
/* 0 normal, 1 long-hold/release hint, 2 saving/shutdown in progress,
 * 3 shutdown failed (resources remain stopped; explicit retry required). */
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
