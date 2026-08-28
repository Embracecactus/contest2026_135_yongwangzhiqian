/****************************************************************************
 * chips/bk7258/include/bk7258_flash_remap.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SoC Flash-remap register ABI shared by BL2 and the CP slot reader.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_REMAP_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_REMAP_H

#define BK7258_FLASH_REMAP_BEGIN_REG  0x44030058u
#define BK7258_FLASH_REMAP_END_REG    0x4403005cu
#define BK7258_FLASH_REMAP_OFFSET_REG 0x44030060u
#define BK7258_FLASH_REMAP_ENABLE_REG 0x44030064u
#define BK7258_FLASH_REMAP_ENABLE_BIT 0x00000001u

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_FLASH_REMAP_H */
