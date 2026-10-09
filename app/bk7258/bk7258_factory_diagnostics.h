/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_BK7258_FACTORY_DIAGNOSTICS_H
#define __APP_BK7258_BK7258_FACTORY_DIAGNOSTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bk7258_provision_storage.h"

#define BKFACTORY_DIAGNOSTICS_RECORD_SIZE 52u
#define BKFACTORY_DIAGNOSTICS_TTL_MS 600000u

#define BKFACTORY_DIAGNOSTICS_ELIGIBLE 1u
#define BKFACTORY_DIAGNOSTICS_ACTIVE   2u
#define BKFACTORY_DIAGNOSTICS_USED     4u

/* BKD1: magic4, independent client16, independent principal32.  The record is
 * received only by the existing echo-disabled physical factory channel.  It is
 * never stored in PCG1 or returned by a read API.
 */
int bkfactory_diagnostics_gate(bool eligible,
                               const uint8_t certificate_sha256[32],
                               uint64_t now_ms);
int bkfactory_diagnostics_enable(const uint8_t *record, size_t size,
                                 uint64_t now_ms);
int bkfactory_diagnostics_revoke(uint64_t now_ms);
int bkfactory_diagnostics_status(uint64_t now_ms, uint32_t *flags,
                                 uint32_t *remaining_ms);
int bkfactory_diagnostics_certificate(uint32_t offset, uint32_t *word);
int bkfactory_diagnostics_snapshot(void *context, uint64_t *binding,
                                   struct bkprov_pc_snapshot_s *view);

#endif /* __APP_BK7258_BK7258_FACTORY_DIAGNOSTICS_H */
