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

#include <bk7258_partitions.h>

#define BK7258_BL2_LOGICAL_CAPACITY       BK7258_ARTIFACT_BL2_A_LOGICAL_SIZE
#define BK7258_BL2_PHYSICAL_CAPACITY      BK7258_ARTIFACT_BL2_A_SIZE
#define BK7258_BL2_SRAM_BASE              0x28020000
#define BK7258_BL2_SRAM_CAPACITY          0x00020000
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

#ifndef BK7258_BL2_COPY_SIZE
#  error "BK7258_BL2_COPY_SIZE must be generated from the selected build"
#endif

#define BK7258_BL2_PRIMARY_XIP            BK7258_ARTIFACT_BL2_A_XIP_START
#define BK7258_BL2_SECONDARY_XIP          BK7258_ARTIFACT_BL2_B_XIP_START
#define BK7258_BL2_SECONDARY_LOGICAL_OFFSET \
  BK7258_ARTIFACT_BL2_B_LOGICAL_OFFSET
#define BK7258_BL2_SECONDARY_RAW_OFFSET   BK7258_ARTIFACT_BL2_B_OFFSET
#define BK7258_BL2_SECONDARY_RAW_END      BK7258_ARTIFACT_BL2_B_END

#if BK7258_ARTIFACT_BL2_B_LOGICAL_SIZE != BK7258_BL2_LOGICAL_CAPACITY || \
    BK7258_ARTIFACT_BL2_B_SIZE != BK7258_BL2_PHYSICAL_CAPACITY
#  error "BK7258 BL2 A/B capacities must match"
#endif
#if BK7258_BL2_COPY_SIZE == 0u || \
    BK7258_BL2_COPY_SIZE > BK7258_BL2_LOGICAL_CAPACITY || \
    BK7258_BL2_COPY_SIZE > BK7258_BL2_SRAM_CAPACITY || \
    (BK7258_BL2_COPY_SIZE & 31u) != 0u
#  error "BK7258 BL2 image size must be CRC-block aligned and within capacity"
#endif
#endif /* BK7258_BOOT_BL2_CONTRACT_H */
