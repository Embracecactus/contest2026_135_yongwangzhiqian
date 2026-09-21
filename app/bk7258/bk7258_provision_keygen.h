/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_KEYGEN_H
#define __APP_BK7258_PROVISION_KEYGEN_H
#include <stddef.h>
#include <stdint.h>

/* Device-owned identity generation.
 *
 * The candidate is created only after storage and the entropy source are
 * ready, and only for a device the factory transaction authorizes. It reuses
 * the existing BPI1 record and the current algorithm (EC P-256, SHA-256,
 * EKU serverAuth + clientAuth, KU digitalSignature), so the claim channel,
 * the identity loader and the phone's pinned trust need no new format.
 *
 * The private key never leaves this buffer except into the identity store the
 * device already owns: no RPC, log, status record or claim code carries it.
 */

/* Validity template from the signed release metadata. It is not a clock
 * reading: a device without a trusted time source must never mint a
 * 1970-dated certificate, and the phone checks the validity window against its
 * own clock. Update both values with an explicit release decision.
 */
#ifndef BKPROV_IDENTITY_NOT_BEFORE
#  define BKPROV_IDENTITY_NOT_BEFORE "20260101000000"
#endif
#ifndef BKPROV_IDENTITY_NOT_AFTER
#  define BKPROV_IDENTITY_NOT_AFTER  "20360101000000"
#endif

/* Generate one BPI1 record into record[capacity]. Returns 0 with *size set.
 * -ENOSYS when the build has no key or certificate writer, -EIO when the
 * entropy source cannot be seeded, -ENOSPC when a certificate or key exceeds
 * the BPI1 limits.
 */
int bkprov_identity_generate(void *record, size_t capacity, size_t *size);

#endif /* __APP_BK7258_PROVISION_KEYGEN_H */
