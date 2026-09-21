/****************************************************************************
 * chips/bk7258/include/bk7258_factory_record.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * First-use factory transaction record. The record lives in its own
 * board-declared Flash region, never in the filesystem a first-use deployment
 * is allowed to initialize, and it only ever authorizes that one
 * initialization: every later state keeps the device from re-entering a
 * destructive path after a storage fault.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FACTORY_RECORD_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FACTORY_RECORD_H

#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

#define BK7258_FACTORY_MAGIC        0x31524653u /* "SFR1" */
#define BK7258_FACTORY_VERSION      1u
#define BK7258_FACTORY_SLOT_SIZE    128u
#define BK7258_FACTORY_SLOT_COUNT   2u

enum bk7258_factory_state_e
{
  BK7258_FACTORY_EMPTY = 0,          /* No valid record: ordinary boot. */
  BK7258_FACTORY_PENDING = 1,        /* Factory deployment wants one init. */
  BK7258_FACTORY_STORAGE_READY = 2,  /* User storage initialized and mounted. */
  BK7258_FACTORY_IDENTITY_READY = 3, /* On-device identity is durable. */
  BK7258_FACTORY_UNCLAIMED_READY = 4,/* Claim window may open. */
  BK7258_FACTORY_DEPLOYED = 5,       /* First use finished; never re-init. */
  BK7258_FACTORY_FAULT = 6,          /* Conflicting or damaged state. */
};

struct bk7258_factory_record_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t generation;      /* Higher wins; a torn slot never wins. */
  uint32_t state;
  uint32_t layout_id;       /* Low 32 bits of the selected layout identity. */
  uint8_t transaction[16];  /* Operator transaction identifier. */
  uint8_t evidence[32];     /* Deployment evidence: board/unit digest. */
  uint8_t reserved[44];
  uint32_t crc;             /* CRC32 over the record with this field zero. */
};

/* Read the newest valid slot. *state is BK7258_FACTORY_EMPTY and *generation 0
 * when the region holds no valid record; a damaged region is reported as
 * BK7258_FACTORY_FAULT rather than silently treated as empty.
 */
int bk7258_factory_record_read(FAR struct bk7258_factory_record_s *record);

/* Publish the next generation of the record. Callers must have validated the
 * transition; this only owns serialization, torn-write detection and the
 * erasing of a single slot.
 */
int bk7258_factory_record_write(FAR const struct bk7258_factory_record_s *
                                record);

/* Convenience transition for the device's own state machine. It refuses to
 * move to an older, unknown or destructive state, and refuses any move after
 * BK7258_FACTORY_DEPLOYED.
 */
int bk7258_factory_record_advance(uint32_t state);

const char *bk7258_factory_state_name(uint32_t state);

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FACTORY_RECORD_H */
