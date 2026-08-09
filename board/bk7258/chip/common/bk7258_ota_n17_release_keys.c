/*
 * bk7258_ota_n17_release_keys.c - shared OTA release-key registry for N17.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include "../include/bk7258_ota_n17_auth.h"

#define BK7258_OTA_N17_RELEASE_KEY_ID 1u

/* Development release key 1, SEC1 compressed secp256r1 form.  Its matching
 * private key is held only in the project owner's Windows key directory.
 * This is N17-S publisher authentication, not an OTP/ROM root. */

static const uint8_t g_bk7258_ota_n17_release_key_1[
  BK7258_OTA_N17_P256_COMPRESSED_PUBLIC_KEY_SIZE] =
{
  0x02, 0x01, 0xc2, 0x46, 0x09, 0x6d, 0x6f, 0x18,
  0x05, 0x07, 0xda, 0x2e, 0xbf, 0xe4, 0x1a, 0xcd,
  0x4c, 0x14, 0x82, 0xb0, 0xc9, 0x15, 0xa6, 0x12,
  0x4e, 0x6b, 0x7f, 0x90, 0x1f, 0xa0, 0x1f, 0x76,
  0x99
};

int bk7258_ota_n17_lookup_public_key(
  uint32_t key_id,
  uint8_t public_key[BK7258_OTA_N17_P256_COMPRESSED_PUBLIC_KEY_SIZE])
{
  uint32_t index;

  if (public_key == NULL || key_id != BK7258_OTA_N17_RELEASE_KEY_ID)
    {
      return -1;
    }

  for (index = 0; index < BK7258_OTA_N17_P256_COMPRESSED_PUBLIC_KEY_SIZE;
       index++)
    {
      public_key[index] = g_bk7258_ota_n17_release_key_1[index];
    }

  return 0;
}
