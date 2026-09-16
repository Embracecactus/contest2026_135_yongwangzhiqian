/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_PAIR_H
#define __APP_BK7258_PROVISION_PAIR_H
#include "bk7258_provision_tls.h"
#include "bk7258_provision_claim.h"
#include "bk7258_provision_scan.h"

/* TLS plaintext frames: SPV1 | type:u8 | reserved:3 | sequence:be32 |
 * transaction:16 | payload_size:be32 | payload. AUTH=1 seq0/secret32,
 * BEGIN=2 size:be32, DATA=3 offset:be32+up to1020 bytes, APPLY=4 empty.
 * STATUS=128 contains state:be32,error:signed-be32. Local confirmation is
 * deliberately absent from the wire. Disconnect cancels uncommitted work.
 * Read-only receipt QUERY=5 seq1/empty is available after AUTH and READY
 * only when the owner supplies a receipt callback. Result is COMMITTED=6,
 * NOT_COMMITTED=9 or UNCERTAIN=8. SCAN=6 seq1/empty is a normal unclaimed
 * READY-window-only read-only request. Its STATUS payload is status:be32,
 * count:u8,truncated:u8,reserved:2 followed by fixed 36-byte AP records.
 */
struct bkprov_pair_s
{
  struct bkprov_tls_s tls;
  struct bkprov_claim_s claim;
  uint8_t input[1056];
  /* Heap-owned by the owner for the bounded outbound scan response. Keep it
   * separate from fragmented TLS input, which may be partially populated. */
  uint8_t output[904];
  size_t input_size;
  size_t expected;
  uint8_t transaction[16];
  uint32_t request_sequence;
  enum bkprov_claim_state_e reported_state;
  bool report;
  bool recovery;
  bool query_pending;
  bool scan_pending;
  bool scan_report;
  bool scan_session;
  struct bkprov_scan_result_s scan;
  int (*receipt)(const uint8_t transaction[16]);
};
int bkprov_pair_start(struct bkprov_pair_s *pair, uint32_t generation,
                      mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                      const uint8_t secret[32], bool local_action,
                      bool already_claimed, uint64_t (*now_ms)(void *),
                      void *clock_context, const struct bkprov_claim_ops_s *ops,
                      void *context);
int bkprov_pair_step(struct bkprov_pair_s *pair);
/* A distinct read-only window also usable on an already claimed device.
 * It still requires possession proof and trusted physical confirmation.
 * receipt: 1 matched, 0 positively absent, -EAGAIN pending, other negative
 * means unknown. It may not perform blocking filesystem I/O. */
int bkprov_pair_start_recovery(struct bkprov_pair_s *pair, uint32_t generation,
                               mbedtls_x509_crt *certificate,
                               mbedtls_pk_context *key,
                               const uint8_t secret[32], bool local_action,
                               uint64_t (*now_ms)(void *), void *clock_context,
                               int (*receipt)(const uint8_t transaction[16]));
int bkprov_pair_confirm(struct bkprov_pair_s *pair, uint32_t generation);
void bkprov_pair_close(struct bkprov_pair_s *pair);
#endif
