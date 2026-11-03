/****************************************************************************
 * app/bk7258/bk7258_pc_approval.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_PC_APPROVAL_H
#define __APP_BK7258_PC_APPROVAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bk7258_control_session.h"

/* One volatile approval request from the authorized PC principal, decided
 * only by the authenticated phone owner. All integers are big-endian.
 *
 * PC kind PC_APPROVAL (TASKS capability):
 *   APR1 request: magic4, request ID16 (nonzero), TTL ms BE32 (5000..300000),
 *     agent/target/operation byte lengths BE16 x3, reserved BE16 zero, then
 *     the three UTF-8 strings in that order. Agent 1..32, target 0..256 and
 *     operation 1..768 bytes. Controls other than TAB/LF, DEL and Unicode
 *     bidirectional overrides are rejected so the phone shows exact text.
 *   APC1 cancel: magic4, request ID16 of the current pending request.
 *   READ returns APS1 (96 B): magic4, state4, request ID16, digest32,
 *     remaining ms4, reserved4, tag32. Digest is SHA-256 of the exact APR1
 *     bytes. For APPROVED/REJECTED the tag is HMAC-SHA256(PC key,
 *     "SHANIU-PC-APPROVAL-v1" || request ID || digest || state BE32);
 *     otherwise it is zero.
 *
 * Phone owner kind PC_APPROVAL_DECISION:
 *   READ returns APV1: magic4, state4, APR1 size4, remaining ms4, then the
 *     exact APR1 bytes of the current request.
 *   APD1 decision (56 B): magic4, decision BE32 (1 approve, 2 reject),
 *     request ID16, digest32 computed by the phone over the APR1 it read.
 *
 * Only one request exists. A decision is single-use and must match the
 * pending request ID and digest before its deadline. A changed PC principal
 * or grant wipes the request; the next PC cannot read the old receipt.
 */

#define BKPC_APPROVAL_HEADER 32u
#define BKPC_APPROVAL_AGENT_MAX 32u
#define BKPC_APPROVAL_TARGET_MAX 256u
#define BKPC_APPROVAL_OPERATION_MAX 768u
#define BKPC_APPROVAL_RECORD_MAX (BKPC_APPROVAL_HEADER + \
  BKPC_APPROVAL_AGENT_MAX + BKPC_APPROVAL_TARGET_MAX + \
  BKPC_APPROVAL_OPERATION_MAX)
#define BKPC_APPROVAL_TTL_MIN 5000u
#define BKPC_APPROVAL_TTL_MAX 300000u
#define BKPC_APPROVAL_CANCEL_SIZE 20u
#define BKPC_APPROVAL_DECISION_SIZE 56u
#define BKPC_APPROVAL_RECEIPT_SIZE 96u
#define BKPC_APPROVAL_VIEW_HEADER 16u
#define BKPC_APPROVAL_REMIND_MS 15000u

enum bkpc_approval_state_e
{
  BKPC_APPROVAL_NONE = 0,
  BKPC_APPROVAL_PENDING,
  BKPC_APPROVAL_APPROVED,
  BKPC_APPROVAL_REJECTED,
  BKPC_APPROVAL_EXPIRED,
  BKPC_APPROVAL_CANCELED
};

struct bkpc_approval_s
{
  uint64_t binding;
  uint64_t grant;
  uint8_t client[16];
  uint64_t deadline;
  uint64_t ttl;
  uint64_t alerted_at;
  uint32_t state;
  uint8_t digest[32];
  size_t size;
  uint8_t record[BKPC_APPROVAL_RECORD_MAX];
  bool alerted;
};

/* Serialized product owner only. A different principal wipes all state;
 * binding zero means no admitted PC and cancels any pending request.
 */

void bkpc_approval_bind(struct bkpc_approval_s *state, uint64_t binding,
                        uint64_t grant, const uint8_t client[16]);
void bkpc_approval_step(struct bkpc_approval_s *state, uint64_t now);
void bkpc_approval_cancel(struct bkpc_approval_s *state);
bool bkpc_approval_pending(const struct bkpc_approval_s *state);

int bkpc_approval_request(struct bkpc_approval_s *state, const uint8_t *record,
                          size_t size, uint64_t now);
int bkpc_approval_decide(struct bkpc_approval_s *state, const uint8_t *record,
                         size_t size, uint64_t now);
int bkpc_approval_receipt(const struct bkpc_approval_s *state,
                          const uint8_t key[32],
                          uint8_t out[BKPC_APPROVAL_RECEIPT_SIZE],
                          uint64_t now);

/* Config adapters. The PC adapter requires the current grant key; the owner
 * adapter must only be reachable from the authenticated phone session.
 */

int bkpc_approval_pc_control(struct bkpc_approval_s *state,
                             enum bkcontrol_command_e command,
                             uint32_t offset, const uint8_t *record,
                             size_t size, struct bkcontrol_status_s *status,
                             const uint8_t key[32], uint64_t now);
int bkpc_approval_owner_control(struct bkpc_approval_s *state,
                                enum bkcontrol_command_e command,
                                uint32_t offset, const uint8_t *record,
                                size_t size, struct bkcontrol_status_s *status,
                                uint64_t now);

/* Focus-display visual: (7 << 8) | remaining 0..32 ring segments, or 0. */

unsigned int bkpc_approval_visual(const struct bkpc_approval_s *state,
                                  uint64_t now, bool available);

/* True once when a request becomes pending, then every REMIND_MS while it
 * remains pending. Not consumed while unavailable.
 */

bool bkpc_approval_take_alert(struct bkpc_approval_s *state, uint64_t now,
                              bool available);

#endif
