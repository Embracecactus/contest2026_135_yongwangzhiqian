/* Board-owned BL1/BL2 size contract.
 *
 * The partition reserves the same 128 KiB logical capacity described by the
 * BK7236 security reference.  A particular BL2 build may be smaller; its
 * signed/CRC-padded logical image length is supplied independently at build
 * time.  Keeping those values separate prevents an erased CRC tail from being
 * mistaken for executable BL2 bytes.
 */
#ifndef BK7258_BOOT_BL2_CONTRACT_H
#define BK7258_BOOT_BL2_CONTRACT_H

#include "../chip/include/bk7258_memorymap.h"
#include <bk7258_partition_layout.h>

#define BK7258_BL2_LOGICAL_CAPACITY       BK7258_ROLE_BL2_LOGICAL_SIZE
#define BK7258_BL2_PHYSICAL_CAPACITY      BK7258_ROLE_BL2_SIZE
#define BK7258_BL2_SRAM_BASE              0x28020000u
#define BK7258_BL2_SRAM_CAPACITY          0x00020000u
#define BK7258_BL2_SRAM_END               \
  (BK7258_BL2_SRAM_BASE + BK7258_BL2_SRAM_CAPACITY)

/* BL1 publishes only the fixed Primary -> Secondary order, while BL2 remains
 * the sole component that accepts and launches a CP/AP pair.  The CP image
 * may overwrite this SRAM record after handoff. */
#define BK7258_BL2_BOOT_POLICY_ADDRESS     0x2801ffd0u
#define BK7258_BL2_BOOT_POLICY_MAGIC       0x4232504cu /* "LP2B" */
#define BK7258_BL2_BOOT_POLICY_VERSION     1u
#define BK7258_BL2_BOOT_POLICY_SLOT_PRIMARY 0u
#define BK7258_BL2_BOOT_POLICY_SLOT_SECONDARY 1u
#define BK7258_BL2_BOOT_POLICY_SLOT_NONE   0xffffffffu
#define BK7258_BL2_BOOT_POLICY_SOURCE_FIXED 0u
#define BK7258_BL2_BOOT_POLICY_CHECK_SEED  0xa5a55a5au

#ifndef __LINKER__
struct bk7258_bl2_boot_policy_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t preferred_slot;
  uint32_t fallback_slot;
  uint32_t source;
  uint32_t state;
  uint32_t generation_low;
  uint32_t generation_high;
  uint32_t check;
};

static inline uint32_t bk7258_bl2_boot_policy_check(
  const struct bk7258_bl2_boot_policy_s *policy)
{
  return BK7258_BL2_BOOT_POLICY_CHECK_SEED ^ policy->magic ^
         policy->version ^ policy->preferred_slot ^ policy->fallback_slot ^
         policy->source ^ policy->state ^ policy->generation_low ^
         policy->generation_high;
}
#endif

/* The current board-owned MCUboot image occupies 0x3000 logical bytes after
 * FF padding.  The Makefiles can override this with the exact same value when
 * building a larger image, up to the 128 KiB contract. */
#define BK7258_BL2_DEFAULT_IMAGE_SIZE     0x00003000u
#ifndef BK7258_BL2_COPY_SIZE
#  define BK7258_BL2_COPY_SIZE            BK7258_BL2_DEFAULT_IMAGE_SIZE
#endif

/*
 * The CSV keeps the original 128 KiB logical ``bl2`` envelope.  The unused
 * pre-LittleFS gap immediately following that envelope is large enough for a
 * second, equally sized development slot.  This is the board-owned
 * primary/secondary fallback used while the BK7258 BootROM Manifest ABI is
 * unavailable; it does not change the official SDK partition rows.
 */
#define BK7258_BL2_PRIMARY_XIP            BK7258_ROLE_BL2_XIP_START
#define BK7258_BL2_SECONDARY_LOGICAL_OFFSET \
  (BK7258_ROLE_BL2_LOGICAL_OFFSET + BK7258_BL2_LOGICAL_CAPACITY)
#define BK7258_BL2_SECONDARY_XIP          \
  (BK7258_FLASH_XIP_BASE + BK7258_BL2_SECONDARY_LOGICAL_OFFSET)
#define BK7258_BL2_SECONDARY_RAW_OFFSET   \
  (BK7258_ROLE_BL2_OFFSET + BK7258_BL2_PHYSICAL_CAPACITY)
#define BK7258_BL2_SECONDARY_RAW_END     \
  (BK7258_BL2_SECONDARY_RAW_OFFSET + BK7258_BL2_PHYSICAL_CAPACITY)

/* The two 256-byte records share the fixed bootloader tail.  The primary
 * address remains the historical address so old development images still
 * fail/boot in the same place when enforcement is disabled. */
#define BK7258_BL1_MANIFEST_SLOT_SIZE     0x00000100u
#define BK7258_BL1_MANIFEST_TAIL_SIZE     \
  (2 * 0x00000100)

#if BK7258_BL2_LOGICAL_CAPACITY != 0x00020000u
#  error "BK7258 BL2 partition must reserve 128 KiB logical capacity"
#endif
#if BK7258_BL2_PHYSICAL_CAPACITY != 0x00022000u
#  error "BK7258 BL2 partition must reserve 136 KiB CRC physical span"
#endif
#if BK7258_BL2_COPY_SIZE == 0u || \
    BK7258_BL2_COPY_SIZE > BK7258_BL2_LOGICAL_CAPACITY || \
    (BK7258_BL2_COPY_SIZE & 31u) != 0u
#  error "BK7258 BL2 image size must be CRC-block aligned and within capacity"
#endif
#if BK7258_BL2_SECONDARY_RAW_END > BK7258_ROLE_LITTLEFS_OFFSET
#  error "secondary BL2 slot overlaps LittleFS"
#endif

#endif /* BK7258_BOOT_BL2_CONTRACT_H */
