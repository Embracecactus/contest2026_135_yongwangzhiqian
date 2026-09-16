/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_CLAIM_H
#define __APP_BK7258_PROVISION_CLAIM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BKPROV_BUNDLE_MAX 16384u
#define BKPROV_CHUNK_MAX 1024u

enum bkprov_claim_state_e
{
  BKPROV_CLOSED, BKPROV_AUTH, BKPROV_LOCAL, BKPROV_READY,
  BKPROV_RECEIVING, BKPROV_CHECKING, BKPROV_COMMITTED,
  BKPROV_FAILED, BKPROV_UNCERTAIN, BKPROV_NOT_COMMITTED
};

/* These operations run on the serialized product owner, never in ATT callbacks.
 * begin checks the entire candidate before attempting Wi-Fi/Gateway. poll
 * returns 0 pending, 1 both verified, or a negative error. commit publishes
 * the same candidate durably: 0 confirmed, -EAGAIN pending, -EINPROGRESS
 * uncertain, otherwise old configuration remains selected. An asynchronous
 * commit must copy bundle/transaction before returning and retain its own
 * context until completion even if the BLE owner closes. Repeated commit
 * calls poll that same operation, never start another publication.
 * abort requests pre-commit cleanup. An asynchronous backend must retain
 * its context and quarantine voice, identity mutation and new claim windows
 * until Wi-Fi rollback and runtime cleanup have completed. No candidate is persisted
 * by begin/poll. All callbacks must obey the owner's bounded I/O deadlines.
 */
struct bkprov_claim_ops_s
{
  int (*begin)(void *context, const uint8_t *bundle, size_t size);
  int (*poll)(void *context);
  int (*commit)(void *context, const uint8_t transaction[16],
                const uint8_t *bundle, size_t size);
  void (*abort)(void *context);
};

struct bkprov_claim_s
{
  const struct bkprov_claim_ops_s *ops;
  void *context;
  uint64_t opened_ms;
  uint64_t last_ms;
  uint32_t generation;
  uint32_t sequence;
  uint8_t transaction[16];
  uint8_t secret[32];
  uint8_t bundle[BKPROV_BUNDLE_MAX];
  size_t size;
  size_t received;
  enum bkprov_claim_state_e state;
  int error;
  bool trial;
  bool commit_pending;
};

/* Zero-initialize; open requires an actual local action and unclaimed device.
 * Both permissions are supplied by the trusted product owner, never a packet.
 * One failed secret closes the attempt; no in-window password guessing.
 */
int bkprov_claim_open(struct bkprov_claim_s *claim, uint32_t generation,
                      const uint8_t secret[32], bool local_action,
                      bool already_claimed, uint64_t now_ms,
                      const struct bkprov_claim_ops_s *ops, void *context);
int bkprov_claim_auth(struct bkprov_claim_s *claim, uint32_t generation,
                      const uint8_t transaction[16], const uint8_t proof[32]);
int bkprov_claim_confirm(struct bkprov_claim_s *claim, uint32_t generation);
int bkprov_claim_begin(struct bkprov_claim_s *claim, uint32_t sequence,
                       size_t size);
int bkprov_claim_data(struct bkprov_claim_s *claim, uint32_t sequence,
                      size_t offset, const void *data, size_t size);
int bkprov_claim_apply(struct bkprov_claim_s *claim, uint32_t sequence);
int bkprov_claim_step(struct bkprov_claim_s *claim, uint32_t generation,
                      uint64_t now_ms);
void bkprov_claim_close(struct bkprov_claim_s *claim);
#endif
