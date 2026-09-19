/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_AGENT_MEMORY_CODEC_H
#define __APP_BK7258_AGENT_MEMORY_CODEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BKMEMORY_MAX 131072u
#define BKMEMORY_OVERHEAD 52u
struct bkmemory_policy_s
{
  uint8_t key[32];
  uint64_t revision;
  bool enabled;
};
typedef int (*bkmemory_random_t)(void *, unsigned char *, size_t);
/* Caller serializes all operations and wipes borrowed policy keys. Policy
 * owner is the current committed SCB3 owner key, not a caller-selected value.
 * root is private on-chip LittleFS, not removable media. Missing policy is
 * disabled; a changed owner cannot resume another owner's retained memory.
 */
int bkmemory_policy_load(const char *root, const uint8_t owner[32],
                               struct bkmemory_policy_s *policy);
/* Set is explicit user intent. rotate cryptographically deletes old SD copies.
 * EINPROGRESS means publication is uncertain: reload before further actions.
 * No plaintext or key is returned through a device control response.
 */
int bkmemory_policy_set(const char *root, const uint8_t owner[32],
                              bool enabled, bool rotate,
                              bkmemory_random_t random, void *context);
/* AEAD envelope for an SD payload; header, key generation and length are
 * authenticated. Fresh 96-bit random nonce per seal. Open wipes output on
 * authentication/validation error for a bounded destination. Input/output
 * buffers must be disjoint. These functions do no I/O.
 */
int bkmemory_seal(const struct bkmemory_policy_s *policy,
                        const void *plain, size_t size, void *sealed,
                        size_t capacity, size_t *used,
                        bkmemory_random_t random, void *context);
int bkmemory_open(const struct bkmemory_policy_s *policy,
                        const void *sealed, size_t size, void *plain,
                        size_t capacity, size_t *used);
/* Legacy SD ciphertext read only. Caller holds the existing media lease.
 * A missing snapshot returns ENOENT; all failures wipe the destination.
 * New encrypted snapshots use the existing private transactional store.
 */
int bkmemory_restore(const char *root,
                           const struct bkmemory_policy_s *policy,
                           void *plain, size_t capacity, size_t *used);
#endif
