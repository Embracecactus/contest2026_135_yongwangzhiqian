/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PC_GRANTS_H
#define __APP_BK7258_PC_GRANTS_H
#include <stdbool.h>
#include "bk7258_provision_store.h"

#ifdef CONFIG_BK7258_AP_CORE
#define BKPC_GRANTS_ROOT "/cpdata/shaniu/pc-grants"
#else
#define BKPC_GRANTS_ROOT "/data/shaniu/pc-grants"
#endif

#define BKPC_CAP_RESOURCES 1u
#define BKPC_CAP_SCENES 2u
#define BKPC_CAP_TASKS 4u
#define BKPC_CAP_DIAGNOSTICS 8u
#define BKPC_CAP_ALL 15u
#define BKPC_GRANT_RECORD_SIZE 88u

/* First version stores one independent computer principal. A serialized file
 * worker exclusively owns this object and its private internal-filesystem root.
 * This is not a wire parser, grant authority, permission dispatcher or key
 * generator. Callers must authenticate the phone owner before set, close live
 * PC sessions when grants change, and clear this store during factory reset.
 * PCG1: magic4, capsBE32, owner binding32, client id16, PC key32. Owner binding
 * is SHA256("SHANIU-PC-OWNER-v1" || current owner key). Revocation is a durable
 * caps=0 tombstone with zero client id/key, preserving the monotonic revision.
 */
struct bkpc_grants_s
{
  struct bkprov_store_s store;
  uint64_t revision;
  uint8_t transaction[16];
  uint8_t owner_binding[32];
  uint8_t record[BKPC_GRANT_RECORD_SIZE];
  bool ready;
  bool uncertain;
};
/* Zero-init once; no mount/format/mkdir. Uncertain publication cannot reopen
 * this owner in the same boot. Only a fresh process/boot reconciles storage.
 */
int bkpc_grants_open(struct bkpc_grants_s *, const char *root,
                     const uint8_t owner_key[32]);
int bkpc_grants_set(struct bkpc_grants_s *, uint64_t expected,
                    const uint8_t transaction[16], const uint8_t client_id[16],
                    const uint8_t pc_key[32], uint32_t capabilities);
/* Snapshot never contains a secret; an old owner's grant appears inactive.
 * key is a device-internal copy for TLS/SDC1 only, never a public readback.
 * Both clear outputs on failure. Auth callers must retain/check the revision
 * throughout a live session; copying a key alone does not implement revocation.
 */
int bkpc_grants_snapshot(const struct bkpc_grants_s *, uint64_t *revision,
                         uint8_t client_id[16], uint32_t *capabilities);
int bkpc_grants_key(const struct bkpc_grants_s *, uint64_t expected,
                    uint8_t key[32], uint32_t *capabilities);
#endif
