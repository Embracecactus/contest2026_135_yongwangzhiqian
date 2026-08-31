/****************************************************************************
 * chips/bk7258/include/bk7258_flash.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 on-chip Flash controller lifecycle and raw I/O mechanics.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_H

#include <nuttx/compiler.h>

#include <stddef.h>
#include <stdint.h>

#define BK7258_FLASH_SECTOR_SIZE 4096u

struct bk7258_flash_partition_info_s
{
  uint32_t start;
  uint32_t size;
};

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_flash_initialize(void);

/* CP task-context raw I/O.  The service lazily initializes and validates the
 * controller, serializes physical commands, and restores the pinned hardware
 * protection state after every mutation.  Callers must still hold the
 * chip-owned storage guard configured from board topology.  Zero-length I/O
 * is rejected.
 */

int bk7258_flash_read(uint32_t address, FAR void *buffer, size_t nbytes);
int bk7258_flash_erase_sector(uint32_t address);
int bk7258_flash_write(uint32_t address, FAR const void *buffer,
                       size_t nbytes);

/* Resolve one selected-layout partition by its generated row identifier.
 * This exposes topology data, never SDK-private types or SDK row ordering.
 */

int bk7258_flash_partition_get_info(
  uint32_t partition, FAR struct bk7258_flash_partition_info_s *info);

/* Preserve the rest of a generated logical partition while updating one
 * range.  The chip translates the generated identifier to the pinned SDK's
 * semantic identifier and owns the physical erase/rewrite transaction.
 */

int bk7258_flash_partition_update(uint32_t partition, uint32_t offset,
                                  FAR const void *buffer, size_t nbytes);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_H */
