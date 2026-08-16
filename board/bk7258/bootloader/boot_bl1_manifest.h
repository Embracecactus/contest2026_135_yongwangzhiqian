/* Board-owned BL1 authorization and official-tool-compatible Manifest
 * parsers for the SRAM MCUboot BL2. */
#ifndef BK7258_BOOT_BL1_MANIFEST_H
#define BK7258_BOOT_BL1_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "../chip/include/bk7258_memorymap.h"
#include <bk7258_partition_layout.h>
#include "boot_bl2_contract.h"

#define BK7258_BL1_MANIFEST_SIZE             256u
#define BK7258_BL1_MANIFEST_FORMAT           2u
#define BK7258_BL1_MANIFEST_SIGNATURE_ALG    1u /* ECDSA-P256 */
#define BK7258_BL1_MANIFEST_DIGEST_ALG       1u /* SHA-256 */
#define BK7258_BL1_MANIFEST_KEY_ID           1u
#define BK7258_BL1_MANIFEST_MIN_IMAGE_VERSION 1u
#define BK7258_BL1_MANIFEST_SIGNED_SIZE      0xb0u
#define BK7258_BL1_MANIFEST_SIGNATURE_SIZE   64u
#define BK7258_BL1_MANIFEST_SIGNATURE_OFFSET 0xb0u
#define BK7258_BL1_MANIFEST_DIGEST_OFFSET    0x30u
#define BK7258_BL1_MANIFEST_KEY_HASH_OFFSET  0x50u
#define BK7258_BL1_MANIFEST_PUBLIC_KEY_OFFSET 0x70u
#define BK7258_BL1_MANIFEST_RESERVED_OFFSET  0xf0u
#define BK7258_BL1_MANIFEST_PRIMARY_XIP_ADDRESS \
  (BK7258_ROLE_BOOT_XIP_END - BK7258_BL1_MANIFEST_SIZE)
#define BK7258_BL1_MANIFEST_SECONDARY_XIP_ADDRESS \
  (BK7258_ROLE_BOOT_XIP_END - BK7258_BL1_MANIFEST_TAIL_SIZE)
/* Compatibility name for callers that only know the original primary slot. */
#define BK7258_BL1_MANIFEST_XIP_ADDRESS \
  BK7258_BL1_MANIFEST_PRIMARY_XIP_ADDRESS

/*
 * One-image Beken Manifest emitted by the official release/v2.0.1 generic
 * secure_boot_tool.  The historic "candidate" API name is kept stable.  The
 * record layout is source/tool verified; BK7258 BootROM acceptance is not,
 * because the development board remains unprovisioned.
 *
 * The candidate is kept in the same 256-byte tail as BKBL1M2 so it can be
 * flashed and recovered without changing the boot partition geometry.
 */
#define BK7258_BEKEN_MANIFEST_MAGIC                 0xa1bc2fd8u
#define BK7258_BEKEN_MANIFEST_LAYOUT_VERSION        0x00010001u
#define BK7258_BEKEN_MANIFEST_HEADER_SIZE           0x18u
#define BK7258_BEKEN_MANIFEST_IMAGE_DESC_SIZE       0x20u
#define BK7258_BEKEN_MANIFEST_FLAG_EC256_SHA256     0x00030619u
#define BK7258_BEKEN_MANIFEST_IMAGE_COUNT           1u
#define BK7258_BEKEN_MANIFEST_IMAGE_FLAGS           0u
#define BK7258_BEKEN_MANIFEST_IMAGE_VERSION         0u
#define BK7258_BEKEN_MANIFEST_IMAGE_STATIC_OFFSET   0x20u
#define BK7258_BEKEN_MANIFEST_IMAGE_LOAD_OFFSET     0x24u
#define BK7258_BEKEN_MANIFEST_IMAGE_SIZE_OFFSET     0x28u
#define BK7258_BEKEN_MANIFEST_IMAGE_ENTRY_OFFSET    0x2cu
#define BK7258_BEKEN_MANIFEST_IMAGE_DIGEST_OFFSET   0x30u
#define BK7258_BEKEN_MANIFEST_RESERVED_OFFSET       0x50u
#define BK7258_BEKEN_MANIFEST_PUBLIC_KEY_OFFSET     0x54u
#define BK7258_BEKEN_MANIFEST_PUBLIC_KEY_SIZE       65u
#define BK7258_BEKEN_MANIFEST_SIGNATURE_OFFSET      0x95u
#define BK7258_BEKEN_MANIFEST_SIGNATURE_SIZE        64u
#define BK7258_BEKEN_MANIFEST_SIGNED_SIZE           \
  BK7258_BEKEN_MANIFEST_SIGNATURE_OFFSET
#define BK7258_BEKEN_MANIFEST_TOTAL_SIZE             0xd5u

/* Read-only Dubhe OTP shadow locations confirmed against the v3.1.1.9
 * dubhe_otp.h map and the BK7258 target.  The BL1 policy only reads the
 * public-key hash and lifecycle state; it never touches OTP_SET or performs
 * an OTP update. */
#define BK7258_DUBHE_OTP_SHADOW_BASE                 0x4b111000u
#define BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_OFFSET  0x28u
#define BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_SIZE    32u
#define BK7258_DUBHE_OTP_LCS_OFFSET                  0x68u
#define BK7258_DUBHE_OTP_LCS_CM                      0u
#define BK7258_DUBHE_OTP_BL1_SECURITY_COUNTER_OFFSET 0x88u

#ifndef BK7258_BL1_OTP_ROOT_POLICY
#  define BK7258_BL1_OTP_ROOT_POLICY                 1u
#endif

int bk7258_bl1_manifest_verify_at(uint32_t manifest_xip, uint32_t bl2_xip,
                                  size_t bl2_size, uint32_t bl2_load);

int bk7258_bl1_manifest_verify(uint32_t bl2_xip, size_t bl2_size,
                               uint32_t bl2_load);

/* Verify the official-tool-compatible Beken/Armino one-image format. */
int bk7258_beken_manifest_verify_at(uint32_t manifest_xip, uint32_t bl2_xip,
                                    size_t bl2_size, uint32_t bl2_load);

/* Same candidate verifier for a record read into SRAM from a raw data
 * partition.  Only the record bytes are consumed; the BL2 digest is still
 * calculated from its CRC-decoded XIP address. */
int bk7258_beken_manifest_verify_buffer(const uint8_t *manifest,
                                        uint32_t bl2_xip, size_t bl2_size,
                                        uint32_t bl2_load);

int bk7258_beken_manifest_verify(uint32_t bl2_xip, size_t bl2_size,
                                 uint32_t bl2_load);

#endif /* BK7258_BOOT_BL1_MANIFEST_H */
