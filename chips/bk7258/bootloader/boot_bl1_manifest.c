/* Board-owned verifier for the one maintained Beken-shaped BL1 Manifest.
 * BL2, not this record, authenticates CP/AP through MCUboot. This does not
 * claim that an unprovisioned BK7258 BootROM accepts the record directly.
 */
#include <stddef.h>
#include <stdint.h>

#include <tinycrypt/ecc.h>
#include <tinycrypt/ecc_dsa.h>

#include "boot_bl1_manifest.h"
#include "boot_bl1_policy.h"
#include "boot_sha256.h"

extern const uint8_t bk7258_beken_manifest_root_public_key_hash[32];

#define BK7258_BL1_OTP_REG32(addr) \
  (*(volatile uint32_t *)(uintptr_t)(addr))

static uint32_t get_le32(const uint8_t *value)
{
  return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
         ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int bytes_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
  uint8_t different = 0;
  size_t index;

  for (index = 0; index < size; index++)
    {
      different |= left[index] ^ right[index];
    }

  return different == 0;
}

static int bytes_are_ff(const uint8_t *data, size_t size)
{
  size_t index;

  for (index = 0; index < size; index++)
    {
      if (data[index] != 0xffu)
        {
          return 0;
        }
    }

  return 1;
}

/* BK7258 otp1.csv places the 32-bit BL1 security counter at physical OTP
 * 0x188.  The Dubhe shadow window starts at physical OTP 0x100, hence the
 * verified shadow offset is 0x88.  This function performs one volatile read;
 * no OTP controller command or programming register is touched. */
uint32_t bk7258_bl1_manifest_version_floor_readonly(void)
{
#if BK7258_BL1_OTP_ROOT_POLICY
  uint32_t bitmap = BK7258_BL1_OTP_REG32(
    BK7258_DUBHE_OTP_SHADOW_BASE +
    BK7258_DUBHE_OTP_BL1_SECURITY_COUNTER_OFFSET);

  return bk7258_bl1_security_counter_decode(bitmap);
#else
  return 0u;
#endif
}

/* Select the BL1 root without ever programming OTP.  v3.1.1.9 maps the
 * secure-boot public-key hash at OTP shadow +0x28 and LCS at +0x68; these
 * addresses were also read-only verified on the BK7258 target.  CM with an
 * all-zero hash is the recoverable development state, so it uses the
 * compiled software root.  Once a non-zero OTP hash exists, the software
 * root is not accepted.  An unexpected non-CM/empty state fails closed. */
static int bk7258_bl1_root_hash_matches(const uint8_t *manifest_hash,
                                        const uint8_t *software_hash)
{
#if BK7258_BL1_OTP_ROOT_POLICY
  uint8_t otp_hash[BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_SIZE];
  uint32_t lcs;
  uint32_t word;
  size_t index;
  int otp_hash_empty = 1;

  for (index = 0; index < BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_SIZE / 4u;
       index++)
    {
      word = BK7258_BL1_OTP_REG32(
        BK7258_DUBHE_OTP_SHADOW_BASE +
        BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_OFFSET + index * 4u);
      otp_hash[index * 4u + 0u] = (uint8_t)(word >> 0);
      otp_hash[index * 4u + 1u] = (uint8_t)(word >> 8);
      otp_hash[index * 4u + 2u] = (uint8_t)(word >> 16);
      otp_hash[index * 4u + 3u] = (uint8_t)(word >> 24);
      if (word != 0u)
        {
          otp_hash_empty = 0;
        }
    }

  lcs = BK7258_BL1_OTP_REG32(BK7258_DUBHE_OTP_SHADOW_BASE +
                             BK7258_DUBHE_OTP_LCS_OFFSET);
  if (otp_hash_empty)
    {
      return lcs == BK7258_DUBHE_OTP_LCS_CM &&
             bytes_equal(manifest_hash, software_hash,
                         BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_SIZE);
    }

  return bytes_equal(manifest_hash, otp_hash,
                     BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_SIZE);
#else
  return bytes_equal(manifest_hash, software_hash,
                     BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_SIZE);
#endif
}

/* Verify the one maintained Beken-shaped project Manifest format. */
int bk7258_bl1_manifest_verify_buffer(const uint8_t *manifest,
                                      uint32_t bl2_xip,
                                      size_t bl2_size,
                                      uint32_t bl2_load)
{
  const uint8_t *image_digest =
    manifest + BK7258_BEKEN_MANIFEST_IMAGE_DIGEST_OFFSET;
  const uint8_t *public_key =
    manifest + BK7258_BEKEN_MANIFEST_PUBLIC_KEY_OFFSET;
  const uint8_t *signature =
    manifest + BK7258_BEKEN_MANIFEST_SIGNATURE_OFFSET;
  struct boot_sha256_context_s sha256;
  uint8_t digest[32];
  uint32_t image_size;
  uint32_t total_size;

  if (manifest == (const uint8_t *)0)
    {
      return -1;
    }

  total_size = get_le32(manifest + 0x0cu);
  image_size = get_le32(manifest +
                        BK7258_BEKEN_MANIFEST_IMAGE_SIZE_OFFSET);
  if (get_le32(manifest + 0x00u) != BK7258_BEKEN_MANIFEST_MAGIC ||
      get_le32(manifest + 0x04u) != BK7258_BEKEN_MANIFEST_LAYOUT_VERSION ||
      !bk7258_bl1_manifest_version_allowed(
        get_le32(manifest + 0x08u),
        bk7258_bl1_manifest_version_floor_readonly()) ||
      total_size != BK7258_BEKEN_MANIFEST_TOTAL_SIZE ||
      get_le32(manifest + 0x10u) != BK7258_BEKEN_MANIFEST_FLAG_EC256_SHA256 ||
      get_le32(manifest + 0x14u) != BK7258_BEKEN_MANIFEST_IMAGE_COUNT ||
      get_le32(manifest + 0x18u) != BK7258_BEKEN_MANIFEST_IMAGE_FLAGS ||
      get_le32(manifest + 0x1cu) != BK7258_BEKEN_MANIFEST_IMAGE_VERSION ||
      get_le32(manifest + BK7258_BEKEN_MANIFEST_IMAGE_STATIC_OFFSET) !=
        bl2_xip ||
      get_le32(manifest + BK7258_BEKEN_MANIFEST_IMAGE_LOAD_OFFSET) !=
        bl2_load ||
      image_size == 0u || image_size > bl2_size ||
      get_le32(manifest + BK7258_BEKEN_MANIFEST_IMAGE_ENTRY_OFFSET) !=
        bl2_load ||
      get_le32(manifest + BK7258_BEKEN_MANIFEST_RESERVED_OFFSET) != 0u ||
      manifest[BK7258_BEKEN_MANIFEST_PUBLIC_KEY_OFFSET] != 0x04u ||
      !bytes_are_ff(manifest + total_size,
                    BK7258_BL1_MANIFEST_SIZE - total_size))
    {
      return -1;
    }

  boot_sha256_init(&sha256);
  boot_sha256_update(&sha256, public_key,
                     BK7258_BEKEN_MANIFEST_PUBLIC_KEY_SIZE);
  boot_sha256_final(&sha256, digest);
  if (!bk7258_bl1_root_hash_matches(
        digest, bk7258_beken_manifest_root_public_key_hash))
    {
      return -4;
  }

  boot_sha256_init(&sha256);
  boot_sha256_update(&sha256, (const uint8_t *)(uintptr_t)bl2_xip,
                     image_size);
  boot_sha256_final(&sha256, digest);
  if (!bytes_equal(digest, image_digest, sizeof(digest)) ||
      (image_size < bl2_size &&
       !bytes_are_ff((const uint8_t *)(uintptr_t)(bl2_xip + image_size),
                     bl2_size - image_size)))
    {
      return -2;
    }

  boot_sha256_init(&sha256);
  boot_sha256_update(&sha256, manifest,
                     BK7258_BEKEN_MANIFEST_SIGNED_SIZE);
  boot_sha256_final(&sha256, digest);
  return uECC_verify(public_key + 1u, digest, sizeof(digest), signature,
                     uECC_secp256r1()) == 1 ? 0 : -3;
}
