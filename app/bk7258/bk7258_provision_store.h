/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_STORE_H
#define __APP_BK7258_PROVISION_STORE_H
#include <stddef.h>
#include <stdint.h>

/* The store record capacity is independent of the 16 KiB claim protocol and
 * holds three bounded encrypted history rounds. Each protocol still bounds
 * its own input, and BLE configuration messages are not enlarged.
 */
#define BKPROV_STORE_RECORD_MAX 32768u

/* One filesystem worker owns this store (AP may use CP RPMsgFS). root is a private
 * directory on the on-chip LittleFS, never an AP MSC/removable volume.
 * No mount, format, identity generation or network side effects occur here.
 */
struct bkprov_store_s
{
  char active[192];
  char pending[192];
  char directory[160];
};
int bkprov_store_open(struct bkprov_store_s *store, const char *root);
int bkprov_store_check_filesystem(const char *root);
/* Finish a rename on storage accepted by check_filesystem. The NuttX
 * LittleFS commit is synchronous; host POSIX also requires directory fsync.
 * A host barrier failure returns -EINPROGRESS for caller reconciliation. */
int bkprov_store_sync_directory(const char *directory);
int bkprov_store_load(struct bkprov_store_s *store, void *bundle,
                      size_t capacity, size_t *size, uint64_t *revision,
                      uint8_t transaction[16]);
/* Entire validated bundle replaces the previous one, with an expected
 * revision guard. 0 = durable publication; -EINPROGRESS = publication outcome
 * unknown and caller must reconcile by load/reboot before further mutation.
 * Other errors occur before publication and leave the old active file intact.
 */
int bkprov_store_commit(struct bkprov_store_s *store, uint64_t expected,
                        const uint8_t transaction[16], const void *bundle, size_t size);
#endif
