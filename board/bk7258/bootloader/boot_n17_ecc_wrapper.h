/*
 * boot_n17_ecc_wrapper.h - freestanding P-256 signature adapter for N17.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_N17_ECC_WRAPPER_H
#define BK7258_BOOT_N17_ECC_WRAPPER_H

#include <stdint.h>

#include <bk7258_ota_n17_auth.h>

#define BK7258_BOOT_N17_P256_COMPRESSED_PUBLIC_KEY_SIZE \
  BK7258_OTA_N17_P256_COMPRESSED_PUBLIC_KEY_SIZE

int bk7258_boot_n17_verify_signature(
  uint32_t key_id, const uint8_t signed_data[448],
  const uint8_t signature[64]);

int bk7258_boot_n17_ecc_vector_selftest(void);

#endif /* BK7258_BOOT_N17_ECC_WRAPPER_H */
