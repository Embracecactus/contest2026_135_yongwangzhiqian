/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_STORAGE_H
#define __APP_BK7258_PROVISION_STORAGE_H
#include <stddef.h>
#include <stdint.h>

/* A single process-lifetime filesystem worker. Calls below only copy bounded
 * memory under a mutex; all filesystem I/O stays in that worker. No detach,
 * timeout cancellation or freeing an in-flight RPMsgFS request is permitted.
 * root is created only beneath an existing private on-chip LittleFS mount
 * (AP via RPMsgFS); no filesystem is mounted or formatted here.
 */
int bkprov_storage_start(const char *root);
/* One product consumer; notification carries no private data, is invoked
 * outside the storage mutex, and must only schedule work. */
void bkprov_storage_set_notify(void (*notify)(void));
/* Immutable factory identity, separate from the mutable network record.
 * Install accepts only a caller-validated BPI1 record; exact repeated polls
 * are idempotent, and an existing different identity is never overwritten.
 */
int bkprov_storage_identity(void *record, size_t capacity, size_t *size);
int bkprov_storage_identity_install(const void *record, size_t size);
/* Poll current config; -EAGAIN while loading/committing, -ENOENT for a
 * positively read empty store. Output is untouched on failure. */
int bkprov_storage_snapshot(void *bundle, size_t capacity, size_t *size,
                            uint64_t *revision, uint8_t transaction[16]);
/* Copy once, then poll by the same transaction, expected revision and exact
 * bytes. A different in-flight operation returns -EBUSY. Disconnect does not
 * cancel the file job. Only a trusted owner may submit verified settings. */
int bkprov_storage_commit(uint64_t expected, const uint8_t transaction[16],
                          const void *bundle, size_t size);
/* Retry a missing mount or reload an idle store. An uncertain publication
 * stays blocked until restart; same-boot readback is not durability proof. */
int bkprov_storage_refresh(void);
/* 1 = matching durable receipt; 0 = positively empty store. A different
 * selected transaction is unknown, never proof that this one failed. */
int bkprov_storage_receipt(const uint8_t transaction[16]);
/* Authenticated product reset only. First publish a durable revocation marker
 * in the mutable record. A marker is never an empty/unclaimed configuration.
 * The product must quiesce all writers, then let this worker clean its exact
 * user-data replicas. Identity, hardware data and firmware trust are untouched.
 * A failed cleanup retains the marker; restart resumes from reset_pending=1.
 */
int bkprov_storage_reset_request(uint64_t expected, const uint8_t transaction[16]);
int bkprov_storage_reset_pending(void);
int bkprov_storage_reset_finish(int (*cleanup)(void));
/* Shutdown only an idle, determinate worker. -EBUSY/-EINPROGRESS leaves it
 * alive; stop/start cannot erase publication uncertainty. No I/O join. */
int bkprov_storage_stop(void);
#endif
