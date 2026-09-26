/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_DISPLAY_TRIAL_CONTROL_H
#define BK7258_DISPLAY_TRIAL_CONTROL_H
#include "bk7258_control_session.h"
/* Serialized authenticated product owner only. Config kind 11, volatile.
 * ETC1 32 bytes, network byte order:
 *  0 magic; 4 action (1 trial, 2 cancel); 8 expected/current trial ID;
 * 12 duration ms; 16 nonzero operation ID (u64); 24 expression code;
 * 28 reserved zero. Cancel has duration/expression zero, nonzero target ID.
 * Expressions 1..9: neutral/happy/shy/sad/surprised/thinking/listening/speaking/sleepy.
 * Trial duration is positive u32; no default is chosen by this protocol.
 * ETS1 32 bytes: magic/state/id/error(u32 each), remaining ms(u64),
 * last accepted operation(u64, zero if latest trial belongs to another caller).
 * State uses bkdisplay_trial_state_e. Remaining UINT64_MAX means clock unknown.
 * READ offsets 0/16 only, never changes state. Client rereads header to reject
 * mixed snapshots. Same last accepted record is idempotent (even after expiry);
 * same operation with different bytes is rejected. Apply ACK is acceptance,
 * not render/cancel completion. CONFIG_CANCEL only discards staging bytes.
 */
int bkdisplay_trial_control(enum bkcontrol_command_e command, uint32_t offset,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status,
  uint64_t now_ms);
#endif
