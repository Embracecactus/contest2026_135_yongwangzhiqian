/****************************************************************************
 * app/bk7258/bk7258_display_job_control.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_DISPLAY_JOB_CONTROL_H
#define __APP_BK7258_DISPLAY_JOB_CONTROL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_control_session.h"

/* Serialized authenticated product owner only. No filesystem I/O here.
 * The owner supplies a fresh cryptographic epoch after boot/revocation.
 * See acceptance/contracts.md RJI1/RJS1. Only the latest volatile receipt
 * is retained; reconnect does not renew the job or its deadline.
 */

struct bkpack_control_s
{
  uint64_t binding;
  uint64_t grant;
  uint64_t id;
  uint64_t previous;
  uint64_t revision;
  uint8_t client[16];
  uint8_t epoch[16];
  uint8_t nonce[16];
  uint8_t digest[32];
  uint8_t query[16];
  uint8_t snapshot[128];
  uint32_t total;
  uint32_t ttl;
  uint32_t offset;
  uint32_t size;
  int begin_error;
  bool bound;
  bool captured;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bkpack_control_bind(struct bkpack_control_s *state, uint64_t binding,
                        uint64_t grant, const uint8_t client[16],
                        const uint8_t epoch[16]);
void bkpack_control_invalidate(struct bkpack_control_s *state);
int bkpack_control_apply(struct bkpack_control_s *state,
                         const uint8_t *record, size_t size, uint64_t now);
int bkpack_control_read(struct bkpack_control_s *state, uint32_t offset,
                        const uint8_t *query, size_t size,
                        struct bkcontrol_status_s *status, uint64_t now);
#endif
