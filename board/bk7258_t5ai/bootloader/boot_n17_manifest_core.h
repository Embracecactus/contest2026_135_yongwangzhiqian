/*
 * boot_n17_manifest_core.h - portable N17 signed-Manifest parser.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_N17_MANIFEST_CORE_H
#define BK7258_BOOT_N17_MANIFEST_CORE_H

#include <stddef.h>
#include <stdint.h>

#define BK7258_BOOT_N17_MANIFEST_SIZE          512u
#define BK7258_BOOT_N17_SIGNED_SIZE            448u
#define BK7258_BOOT_N17_SIGNATURE_SIZE          64u
#define BK7258_BOOT_N17_SHA256_SIZE             32u

typedef int (*bk7258_boot_n17_sha256_t)(
  void *arg, const uint8_t *data, size_t len,
  uint8_t digest[BK7258_BOOT_N17_SHA256_SIZE]);

typedef int (*bk7258_boot_n17_signature_verify_t)(
  void *arg, uint32_t key_id,
  const uint8_t signed_data[BK7258_BOOT_N17_SIGNED_SIZE],
  const uint8_t signature[BK7258_BOOT_N17_SIGNATURE_SIZE]);

struct bk7258_boot_n17_manifest_ops_s
{
  void *arg;
  bk7258_boot_n17_sha256_t sha256;
  bk7258_boot_n17_signature_verify_t verify_signature;
};

struct bk7258_boot_n17_manifest_info_s
{
  uint32_t key_id;
  uint64_t security_counter;
  uint32_t pair_physical_size;
  uint32_t cp_logical_length;
  uint32_t ap_logical_length;
  uint8_t signed_sha256[BK7258_BOOT_N17_SHA256_SIZE];
  uint8_t pair_sha256[BK7258_BOOT_N17_SHA256_SIZE];
  uint8_t cp_sha256[BK7258_BOOT_N17_SHA256_SIZE];
  uint8_t ap_sha256[BK7258_BOOT_N17_SHA256_SIZE];
};

int bk7258_boot_n17_manifest_verify(
  const uint8_t *manifest, size_t manifest_size,
  uint64_t minimum_security_counter,
  const struct bk7258_boot_n17_manifest_ops_s *ops,
  struct bk7258_boot_n17_manifest_info_s *info);

#endif /* BK7258_BOOT_N17_MANIFEST_CORE_H */
