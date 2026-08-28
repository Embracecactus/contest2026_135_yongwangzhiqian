/*
 * BK7258 BL1 -> BL2 -> MCUboot ABI.
 *
 * The v3.1.1.9 flash stream stores 32 data bytes followed by two CRC bytes.
 * MCUboot sees the decoded logical XIP view, while BL1 copies the same
 * decoded view into BL2 SRAM.  Keep the conversion and the paired CP/AP
 * boundaries in one header so the flash map and handoff code cannot drift.
 */
#ifndef __BK7258_BL2_ABI_H
#define __BK7258_BL2_ABI_H

#include <bk7258_partitions.h>
#include <bk7258_image_layout.h>
#include <boot_bl2_contract.h>

#define BK7258_BL2_CRC_PHYSICAL_SIZE(logical_size) \
  ((logical_size) / BK7258_FLASH_CRC_DATA_SIZE * \
   BK7258_FLASH_CRC_TOTAL_SIZE)

#define BK7258_BL2_CRC_LOGICAL_OFFSET(physical_offset) \
  ((physical_offset) / BK7258_FLASH_CRC_TOTAL_SIZE * \
   BK7258_FLASH_CRC_DATA_SIZE)

#define BK7258_BL2_CP_RAW_SIZE \
  BK7258_BL2_CRC_PHYSICAL_SIZE(BK7258_ARTIFACT_CP_LOGICAL_SIZE)
#define BK7258_BL2_AP_RAW_SIZE \
  BK7258_BL2_CRC_PHYSICAL_SIZE(BK7258_ARTIFACT_AP_LOGICAL_SIZE)

#define BK7258_BL2_B_CP_RAW_OFFSET BK7258_AB_SECONDARY_CP_RAW_START
#define BK7258_BL2_B_AP_RAW_OFFSET BK7258_AB_SECONDARY_AP_RAW_START

#define BK7258_BL2_B_CP_XIP_START BK7258_AB_SECONDARY_CP_XIP_START
#define BK7258_BL2_B_AP_XIP_START BK7258_AB_SECONDARY_AP_XIP_START

/* The flash remapper presents the selected B physical pair through the
 * primary A CP/AP XIP window.  Keep the register addresses and offset
 * calculation beside the slot map so the final handoff cannot silently drift
 * from the board's reverse-engineered direct-XIP ABI. */
#define BK7258_BL2_FLASH_REMAP_BEGIN BK7258_FLASH_REMAP_BEGIN_REG
#define BK7258_BL2_FLASH_REMAP_END BK7258_FLASH_REMAP_END_REG
#define BK7258_BL2_FLASH_REMAP_OFFSET BK7258_FLASH_REMAP_OFFSET_REG
#define BK7258_BL2_FLASH_REMAP_ENABLE BK7258_FLASH_REMAP_ENABLE_REG
#define BK7258_BL2_REMAP_BEGIN BK7258_AB_REMAP_BEGIN
#define BK7258_BL2_REMAP_END BK7258_AB_REMAP_END
#define BK7258_BL2_REMAP_OFFSET BK7258_AB_REMAP_OFFSET

/* Limit the upstream multi-image scan to one physical CP/AP pair.  The
 * implementation lives in the board flash-map adapter; exposing the board
 * ABI here keeps the BL2 entry point free of a private extern declaration.
 * The BOTH value deliberately matches the normal MCUboot view. */
enum bk7258_bl2_slot_limit
{
  BK7258_BL2_SLOTS_BOTH = -1,
  BK7258_BL2_SLOT_PRIMARY = 0,
  BK7258_BL2_SLOT_SECONDARY = 1
};

#ifdef __cplusplus
extern "C"
{
#endif

void bk7258_bl2_set_slot_limit(int slot);

#ifdef __cplusplus
}
#endif

/* BL1 copies only the signed logical image length, but BL2 is linked inside
 * the same 128 KiB SRAM contract.  Keep this assertion in the BL2 build too;
 * otherwise a future BL2-only Make invocation could silently drift from the
 * BL1 copy/manifest contract. */
#if BK7258_BL2_LOGICAL_CAPACITY > BK7258_BL2_SRAM_CAPACITY
# error "BK7258 BL2 partition exceeds the SRAM execution window"
#endif
#if BK7258_BL2_COPY_SIZE > BK7258_BL2_SRAM_CAPACITY
# error "BK7258 BL2 active image exceeds the SRAM execution window"
#endif

/* These checks are the board ABI, not runtime validation.  A changed
 * generated partition table must fail the BL2 build until the handoff map is
 * reviewed again. */
#if (BK7258_ARTIFACT_CP_LOGICAL_SIZE % BK7258_FLASH_CRC_DATA_SIZE) != 0
# error "BK7258 CP logical size is not CRC-block aligned"
#endif
#if (BK7258_ARTIFACT_AP_LOGICAL_SIZE % BK7258_FLASH_CRC_DATA_SIZE) != 0
# error "BK7258 AP logical size is not CRC-block aligned"
#endif
#if BK7258_ARTIFACT_CP_OFFSET + BK7258_BL2_CP_RAW_SIZE != \
    BK7258_ARTIFACT_AP_OFFSET
# error "BK7258 CP raw span does not meet AP raw span"
#endif
#if BK7258_ARTIFACT_AP_OFFSET + BK7258_BL2_AP_RAW_SIZE != \
    BK7258_ARTIFACT_PAIR_OFFSET
# error "BK7258 AP raw span does not meet B pair"
#endif
#if BK7258_ARTIFACT_PAIR_SIZE != \
    (BK7258_BL2_CP_RAW_SIZE + BK7258_BL2_AP_RAW_SIZE)
# error "BK7258 B pair size is not a CP/AP pair"
#endif
#if BK7258_ARTIFACT_PAIR_OFFSET % BK7258_FLASH_CRC_TOTAL_SIZE != 0
# error "BK7258 B pair offset is not CRC-stream aligned"
#endif
#if BK7258_BL2_B_AP_RAW_OFFSET % BK7258_FLASH_CRC_TOTAL_SIZE != 0
# error "BK7258 B AP offset is not CRC-stream aligned"
#endif
#if BK7258_ARTIFACT_CP_XIP_START != \
    (BK7258_FLASH_XIP_BASE + \
     BK7258_BL2_CRC_LOGICAL_OFFSET(BK7258_ARTIFACT_CP_OFFSET))
# error "BK7258 CP XIP base disagrees with CRC conversion"
#endif
#if BK7258_ARTIFACT_AP_XIP_START != \
    (BK7258_FLASH_XIP_BASE + \
     BK7258_BL2_CRC_LOGICAL_OFFSET(BK7258_ARTIFACT_AP_OFFSET))
# error "BK7258 AP XIP base disagrees with CRC conversion"
#endif

#endif /* __BK7258_BL2_ABI_H */
