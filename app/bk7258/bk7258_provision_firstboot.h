/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_FIRSTBOOT_H
#define __APP_BK7258_PROVISION_FIRSTBOOT_H
#include <stddef.h>
#include <stdint.h>

/* Controlled first boot on the AP side.
 *
 * The CP owns the factory transaction record and mirrors it into the mounted
 * volume only after that volume really exists. This module reads that mirror,
 * lets the device create its own identity exactly once, and refuses to create
 * one for any device the factory transaction does not authorize - including a
 * device whose identity file disappeared after a completed deployment, which
 * is a fault and never a new authorization.
 */

enum bkprov_firstboot_state_e
{
  BKPROV_FIRSTBOOT_PENDING = 0,   /* Storage or authorization not read yet. */
  BKPROV_FIRSTBOOT_AUTHORIZED,    /* May create one identity now. */
  BKPROV_FIRSTBOOT_GENERATING,    /* Candidate submitted, publication open. */
  BKPROV_FIRSTBOOT_READY,         /* Identity present and durable. */
  BKPROV_FIRSTBOOT_UNAUTHORIZED,  /* No factory authorization: fail closed. */
  BKPROV_FIRSTBOOT_FAULT,         /* Contradiction or damaged evidence. */
};

/* Poll from the product owner only. Returns 0 with the identity record copied
 * into record[capacity] when an identity is present, -ENOENT when this device
 * is not authorized to have one, -EAGAIN while a generation, publication or
 * storage read is still in flight, or another negative errno.
 */
int bkprov_firstboot_identity(void *record, size_t capacity, size_t *size);

enum bkprov_firstboot_state_e bkprov_firstboot_state(void);
const char *bkprov_firstboot_state_name(enum bkprov_firstboot_state_e state);

#endif /* __APP_BK7258_PROVISION_FIRSTBOOT_H */
