/*
 * bk7258_ota_n17_auth.c - CP-side N17 signed-OTA Manifest verification.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <crypto/ecc.h>
#include <crypto/sha2.h>

#include <arch/chip/bk7258_ota_n17_auth.h>

#include "../../bootloader/boot_n17_manifest_core.h"

static int bk7258_ota_n17_sha256(void *arg, const uint8_t *data, size_t len,
                                 uint8_t digest[32])
{
  SHA2_CTX context;

  (void)arg;
  sha256init(&context);
  sha256update(&context, data, len);
  sha256final(digest, &context);
  return 0;
}

static int bk7258_ota_n17_verify_signature(void *arg, uint32_t key_id,
                                            const uint8_t signed_data[448],
                                            const uint8_t signature[64])
{
  SHA2_CTX context;
  uint8_t digest[32];
  uint8_t public_key[BK7258_OTA_N17_P256_COMPRESSED_PUBLIC_KEY_SIZE];

  (void)arg;
  if (bk7258_ota_n17_lookup_public_key(key_id, public_key) < 0)
    {
      return -EKEYREJECTED;
    }

  sha256init(&context);
  sha256update(&context, signed_data, 448u);
  sha256final(digest, &context);
  return ecdsa_verify(public_key, digest, signature) == 1 ? 0 :
         -EKEYREJECTED;
}

int bk7258_ota_n17_verify_manifest_info(
  const uint8_t manifest[BK7258_OTA_N17_MANIFEST_SIZE],
  struct bk7258_ota_n17_manifest_info_s *info)
{
  static const struct bk7258_boot_n17_manifest_ops_s ops =
  {
    .arg = NULL,
    .sha256 = bk7258_ota_n17_sha256,
    .verify_signature = bk7258_ota_n17_verify_signature
  };
  struct bk7258_boot_n17_manifest_info_s parsed;

  if (bk7258_boot_n17_manifest_verify(
        manifest, BK7258_OTA_N17_MANIFEST_SIZE, 1u, &ops, &parsed) < 0)
    {
      return -EKEYREJECTED;
    }

  if (info != NULL)
    {
      info->security_counter = parsed.security_counter;
      info->pair_physical_size = parsed.pair_physical_size;
      for (size_t index = 0; index < BK7258_OTA_N17_SHA256_SIZE; index++)
        {
          info->signed_sha256[index] = parsed.signed_sha256[index];
          info->pair_sha256[index] = parsed.pair_sha256[index];
        }
    }

  return 0;
}

int bk7258_ota_n17_verify_manifest(
  const uint8_t manifest[BK7258_OTA_N17_MANIFEST_SIZE])
{
  return bk7258_ota_n17_verify_manifest_info(manifest, NULL);
}

/*
 * Keep exactly one canonical Format-3 parser for CP and Bootloader.  The
 * NuttX arch build resolves VPATH from arch/arm/src, where the board
 * Bootloader source is not a reliable standalone object location.  Including
 * this portable, dependency-free implementation here makes it part of the
 * correctly named CP authentication translation unit without copying it.
 */

#include "../../bootloader/boot_n17_manifest_core.c"
