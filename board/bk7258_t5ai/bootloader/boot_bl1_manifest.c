/* BK7236-security-semantic, board-owned BL1 Manifest verifier.
 *
 * 0x00 magic "BKBL1M2\\0"
 * 0x08 format, 0x0c signature algorithm, 0x10 digest algorithm
 * 0x14 key id, 0x18 image version, 0x1c flags
 * 0x20 BL2 XIP, 0x24 BL2 size, 0x28 BL2 SRAM load, 0x2c reserved
 * 0x30 SHA-256(BL2), 0x50 SHA-256(public key), 0x70 public key X||Y
 * 0xb0 ECDSA-P256 r||s over SHA-256(bytes 0x00..0xaf)
 * 0xf0..0xff erased.  BL2, not this record, authenticates CP/AP via MCUboot.
 *
 * This mirrors the documented BL1 semantic checks.  It is not a claim that
 * these offsets are Beken's unpublished BK7258 Manifest ABI.
 */
#include <stddef.h>
#include <stdint.h>

#include <tinycrypt/ecc.h>
#include <tinycrypt/ecc_dsa.h>

#include "boot_bl1_manifest.h"
#include "boot_bl1_policy.h"
#include "boot_sha256.h"

extern const uint8_t bk7258_bl1_manifest_root_public_key_hash[32];
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

int bk7258_bl1_manifest_verify_at(uint32_t manifest_xip, uint32_t bl2_xip,
                                  size_t bl2_size, uint32_t bl2_load)
{
  static const uint8_t magic[8] =
    { 'B', 'K', 'B', 'L', '1', 'M', '2', 0 };
  const uint8_t *manifest = (const uint8_t *)(uintptr_t)manifest_xip;
  struct boot_sha256_context_s sha256;
  uint8_t manifest_digest[32];
  uint8_t bl2_digest[32];
  uint8_t public_key_digest[32];
  const uint8_t *public_key =
    manifest + BK7258_BL1_MANIFEST_PUBLIC_KEY_OFFSET;

  /* Keep the experimental Beken/Armino candidate parser behind the same
   * recoverable flash location.  A mismatching magic continues through the
   * established BKBL1M2 parser, so existing development images remain valid. */
  if (get_le32(manifest) == BK7258_BEKEN_MANIFEST_MAGIC)
    {
      return bk7258_beken_manifest_verify_at(manifest_xip, bl2_xip,
                                             bl2_size, bl2_load);
    }

  if (!bytes_equal(manifest, magic, sizeof(magic)) ||
      get_le32(manifest + 8u) != BK7258_BL1_MANIFEST_FORMAT ||
      get_le32(manifest + 12u) != BK7258_BL1_MANIFEST_SIGNATURE_ALG ||
      get_le32(manifest + 16u) != BK7258_BL1_MANIFEST_DIGEST_ALG ||
      get_le32(manifest + 20u) != BK7258_BL1_MANIFEST_KEY_ID ||
      !bk7258_bl1_manifest_version_allowed(
        get_le32(manifest + 24u),
        bk7258_bl1_manifest_version_floor_readonly()) ||
      get_le32(manifest + 28u) != 0u ||
      get_le32(manifest + 32u) != bl2_xip ||
      get_le32(manifest + 36u) != bl2_size ||
      get_le32(manifest + 40u) != bl2_load ||
      get_le32(manifest + 44u) != 0u ||
      !bytes_are_ff(manifest + BK7258_BL1_MANIFEST_RESERVED_OFFSET,
                    BK7258_BL1_MANIFEST_SIZE -
                    BK7258_BL1_MANIFEST_RESERVED_OFFSET))
    {
      return -1; /* Record format or fixed handoff fields. */
    }

  boot_sha256_init(&sha256);
  boot_sha256_update(&sha256, public_key, 64u);
  boot_sha256_final(&sha256, public_key_digest);
  if (!bytes_equal(public_key_digest,
                   manifest + BK7258_BL1_MANIFEST_KEY_HASH_OFFSET,
                   sizeof(public_key_digest)) ||
      !bytes_equal(public_key_digest,
                   bk7258_bl1_manifest_root_public_key_hash,
                   sizeof(public_key_digest)))
    {
      return -4; /* Manifest key is not anchored to the software root. */
    }

  boot_sha256_init(&sha256);
  boot_sha256_update(&sha256, (const uint8_t *)(uintptr_t)bl2_xip, bl2_size);
  boot_sha256_final(&sha256, bl2_digest);
  if (!bytes_equal(bl2_digest,
                   manifest + BK7258_BL1_MANIFEST_DIGEST_OFFSET,
                   sizeof(bl2_digest)))
    {
      return -2; /* The authorized BL2 digest did not match XIP. */
    }

  boot_sha256_init(&sha256);
  boot_sha256_update(&sha256, manifest, BK7258_BL1_MANIFEST_SIGNED_SIZE);
  boot_sha256_final(&sha256, manifest_digest);
  return uECC_verify(public_key, manifest_digest,
                     sizeof(manifest_digest),
                     manifest + BK7258_BL1_MANIFEST_SIGNATURE_OFFSET,
                     uECC_secp256r1()) == 1 ?
         0 : -3; /* P-256 signature rejected. */
}

int bk7258_bl1_manifest_verify(uint32_t bl2_xip, size_t bl2_size,
                               uint32_t bl2_load)
{
  return bk7258_bl1_manifest_verify_at(
    BK7258_BL1_MANIFEST_PRIMARY_XIP_ADDRESS, bl2_xip, bl2_size, bl2_load);
}

/*
 * Verify the one-image Beken/Armino manifest candidate.
 *
 * This layout is intentionally isolated from BKBL1M2.  It is a reversible
 * compatibility experiment matching the generic IPSS manifest sample found
 * beside the Armino security scaffolding: the descriptor records the raw
 * image length, while the board copy window may be larger and must have an
 * erased tail.  Acceptance here proves only that the board-owned BL1 can
 * consume the candidate.  It does not prove that BK7258 BootROM accepts it
 * or that OTP-backed Secure Boot is enabled.  Cipher/encrypted records are
 * rejected until BK7258 evidence is available.
 */
static int bk7258_beken_manifest_verify_bytes(const uint8_t *manifest,
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

int bk7258_beken_manifest_verify_at(uint32_t manifest_xip, uint32_t bl2_xip,
                                    size_t bl2_size, uint32_t bl2_load)
{
  return bk7258_beken_manifest_verify_bytes(
    (const uint8_t *)(uintptr_t)manifest_xip, bl2_xip, bl2_size, bl2_load);
}

int bk7258_beken_manifest_verify_buffer(const uint8_t *manifest,
                                        uint32_t bl2_xip, size_t bl2_size,
                                        uint32_t bl2_load)
{
  if (manifest == (const uint8_t *)0)
    {
      return -1;
    }

  return bk7258_beken_manifest_verify_bytes(manifest, bl2_xip, bl2_size,
                                            bl2_load);
}

int bk7258_beken_manifest_verify(uint32_t bl2_xip, size_t bl2_size,
                                 uint32_t bl2_load)
{
  return bk7258_beken_manifest_verify_at(
    BK7258_BL1_MANIFEST_PRIMARY_XIP_ADDRESS, bl2_xip, bl2_size, bl2_load);
}
