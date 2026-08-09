/*
 * bk7258_ota_n17_auth.h - shared N17-S OTA release-key interface.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_N17_AUTH_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_N17_AUTH_H

#include <stdint.h>

#define BK7258_OTA_N17_P256_COMPRESSED_PUBLIC_KEY_SIZE 33u
#define BK7258_OTA_N17_MANIFEST_SIZE 512u
#define BK7258_OTA_N17_SHA256_SIZE   32u

struct bk7258_ota_n17_manifest_info_s
{
  uint64_t security_counter;
  uint32_t pair_physical_size;
  uint8_t signed_sha256[BK7258_OTA_N17_SHA256_SIZE];
  uint8_t pair_sha256[BK7258_OTA_N17_SHA256_SIZE];
};

/* No private key is present in the firmware tree. */
int bk7258_ota_n17_lookup_public_key(
  uint32_t key_id,
  uint8_t public_key[BK7258_OTA_N17_P256_COMPRESSED_PUBLIC_KEY_SIZE]);

/* Verify the fixed 512-byte N17 OTA Manifest before it can be published. */
int bk7258_ota_n17_verify_manifest(
  const uint8_t manifest[BK7258_OTA_N17_MANIFEST_SIZE]);

int bk7258_ota_n17_verify_manifest_info(
  const uint8_t manifest[BK7258_OTA_N17_MANIFEST_SIZE],
  struct bk7258_ota_n17_manifest_info_s *info);

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_N17_AUTH_H */
