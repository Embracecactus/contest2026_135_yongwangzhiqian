/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_BOOT_BL1_MANIFEST_H
#define BK7258_BOOT_BL1_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define BK7258_BL1_MANIFEST_SIZE                     256u
#define BK7258_BEKEN_MANIFEST_MAGIC                  0xa1bc2fd8u
#define BK7258_BEKEN_MANIFEST_LAYOUT_VERSION         0x00010001u
#define BK7258_BEKEN_MANIFEST_FLAG_EC256_SHA256      0x00030619u
#define BK7258_BEKEN_MANIFEST_IMAGE_COUNT            1u
#define BK7258_BEKEN_MANIFEST_IMAGE_FLAGS            0u
#define BK7258_BEKEN_MANIFEST_IMAGE_VERSION          0u
#define BK7258_BEKEN_MANIFEST_IMAGE_STATIC_OFFSET    0x20u
#define BK7258_BEKEN_MANIFEST_IMAGE_LOAD_OFFSET      0x24u
#define BK7258_BEKEN_MANIFEST_IMAGE_SIZE_OFFSET      0x28u
#define BK7258_BEKEN_MANIFEST_IMAGE_ENTRY_OFFSET     0x2cu
#define BK7258_BEKEN_MANIFEST_IMAGE_DIGEST_OFFSET    0x30u
#define BK7258_BEKEN_MANIFEST_RESERVED_OFFSET        0x50u
#define BK7258_BEKEN_MANIFEST_PUBLIC_KEY_OFFSET      0x54u
#define BK7258_BEKEN_MANIFEST_PUBLIC_KEY_SIZE        65u
#define BK7258_BEKEN_MANIFEST_SIGNATURE_OFFSET       0x95u
#define BK7258_BEKEN_MANIFEST_SIGNATURE_SIZE         64u
#define BK7258_BEKEN_MANIFEST_SIGNED_SIZE            0x95u
#define BK7258_BEKEN_MANIFEST_TOTAL_SIZE             0xd5u

#define BK7258_DUBHE_OTP_SHADOW_BASE                 0x4b111000u
#define BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_OFFSET  0x28u
#define BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_SIZE    32u
#define BK7258_DUBHE_OTP_LCS_OFFSET                  0x68u
#define BK7258_DUBHE_OTP_LCS_CM                      0u
#define BK7258_DUBHE_OTP_BL1_SECURITY_COUNTER_OFFSET 0x88u

#ifndef BK7258_BL1_OTP_ROOT_POLICY
#  error "BK7258_BL1_OTP_ROOT_POLICY must be generated for BL1"
#endif

int bk7258_bl1_manifest_verify_buffer(const uint8_t *manifest,
                                      uint32_t bl2_xip, size_t bl2_size,
                                      uint32_t bl2_load);

#endif /* BK7258_BOOT_BL1_MANIFEST_H */
