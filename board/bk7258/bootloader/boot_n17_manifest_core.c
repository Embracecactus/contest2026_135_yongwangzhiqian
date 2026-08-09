/*
 * boot_n17_manifest_core.c - portable N17 signed-Manifest parser.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * No NuttX, SDK, MMIO, heap or libc dependency.  The caller owns SHA-256,
 * public-key lookup/validation and ECDSA-P256 verification.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_n17_manifest_core.h"
#include "../chip/include/bk7258_partition_layout.h"

#define MANIFEST_VERSION_OFFSET       0x008u
#define MANIFEST_SIZE_OFFSET          0x00au
#define SIGNED_SIZE_OFFSET            0x00cu
#define SIGNATURE_SIZE_OFFSET         0x00eu
#define FLAGS_OFFSET                  0x010u
#define SIGNATURE_ALGORITHM_OFFSET    0x014u
#define DIGEST_ALGORITHM_OFFSET       0x016u
#define IMAGE_ENCODING_OFFSET         0x018u
#define COMPONENT_COUNT_OFFSET        0x01au
#define KEY_ID_OFFSET                 0x01cu
#define SECURITY_COUNTER_OFFSET       0x020u
#define PRODUCT_ID_OFFSET             0x028u
#define BOARD_ID_OFFSET               0x038u
#define CHIP_ID_OFFSET                0x048u
#define LAYOUT_SHA256_OFFSET          0x058u
#define RELEASE_VERSION_OFFSET        0x078u
#define PAIR_PHYSICAL_SIZE_OFFSET     0x090u
#define CP_LOGICAL_LENGTH_OFFSET      0x094u
#define AP_LOGICAL_LENGTH_OFFSET      0x098u
#define RESERVED0_OFFSET              0x09cu
#define PAIR_SHA256_OFFSET            0x0a0u
#define CP_SHA256_OFFSET              0x0c0u
#define AP_SHA256_OFFSET              0x0e0u
#define RESERVED_SIGNED_OFFSET        0x100u
#define SIGNATURE_OFFSET              0x1c0u

#define MANIFEST_VERSION              1u
#define SIGNATURE_ALGORITHM_ECDSA_P256_SHA256 1u
#define DIGEST_ALGORITHM_SHA256       1u
#define IMAGE_ENCODING_BEKEN_CRC      1u
#define COMPONENT_COUNT               2u

static const uint8_t g_manifest_magic[8] =
  {'B', 'K', 'O', 'T', 'A', '1', '7', 'S'};
static const uint8_t g_layout_sha256[32] =
  BK7258_PARTITION_LAYOUT_SHA256_BYTES;
static const uint8_t g_p256_order[32] =
{
  0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
  0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51
};
static const uint8_t g_p256_half_order[32] =
{
  0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
  0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b, 0xcf, 0x42,
  0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8
};

static uint16_t getle16(const uint8_t *value)
{
  return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t getle32(const uint8_t *value)
{
  return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
         ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static uint64_t getle64(const uint8_t *value)
{
  return (uint64_t)getle32(value) | ((uint64_t)getle32(value + 4) << 32);
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right,
                        size_t len)
{
  while (len-- != 0)
    {
      if (*left++ != *right++)
        {
          return false;
        }
    }

  return true;
}

static bool bytes_value(const uint8_t *data, size_t len, uint8_t value)
{
  while (len-- != 0)
    {
      if (*data++ != value)
        {
          return false;
        }
    }

  return true;
}

static void bytes_copy(uint8_t *destination, const uint8_t *source,
                       size_t len)
{
  while (len-- != 0)
    {
      *destination++ = *source++;
    }
}

static bool exact_text(const uint8_t *field, size_t field_size,
                       const char *expected, size_t expected_size)
{
  size_t index;

  if (expected_size >= field_size ||
      !bytes_equal(field, (const uint8_t *)expected, expected_size) ||
      field[expected_size] != 0)
    {
      return false;
    }

  for (index = expected_size + 1; index < field_size; index++)
    {
      if (field[index] != 0)
        {
          return false;
        }
    }

  return true;
}

static bool release_character(uint8_t value, bool first)
{
  bool alphanumeric =
    (value >= 'A' && value <= 'Z') ||
    (value >= 'a' && value <= 'z') ||
    (value >= '0' && value <= '9');

  return alphanumeric || (!first &&
    (value == '.' || value == '_' || value == '+' || value == '-'));
}

static bool canonical_release(const uint8_t *field)
{
  size_t index;
  bool terminated = false;

  for (index = 0; index < 24; index++)
    {
      uint8_t value = field[index];

      if (terminated)
        {
          if (value != 0)
            {
              return false;
            }
        }
      else if (value == 0)
        {
          if (index == 0)
            {
              return false;
            }

          terminated = true;
        }
      else if (!release_character(value, index == 0))
        {
          return false;
        }
    }

  return terminated;
}

static int big_compare(const uint8_t *left, const uint8_t *right, size_t len)
{
  while (len-- != 0)
    {
      if (*left < *right)
        {
          return -1;
        }

      if (*left > *right)
        {
          return 1;
        }

      left++;
      right++;
    }

  return 0;
}

static bool scalar_valid(const uint8_t scalar[32])
{
  return !bytes_value(scalar, 32, 0) &&
         big_compare(scalar, g_p256_order, 32) < 0;
}

static void clear_info(struct bk7258_boot_n17_manifest_info_s *info)
{
  uint8_t *bytes = (uint8_t *)info;
  size_t index;

  for (index = 0; index < sizeof(*info); index++)
    {
      bytes[index] = 0;
    }
}

int bk7258_boot_n17_manifest_verify(
  const uint8_t *manifest, size_t manifest_size,
  uint64_t minimum_security_counter,
  const struct bk7258_boot_n17_manifest_ops_s *ops,
  struct bk7258_boot_n17_manifest_info_s *info)
{
  const uint8_t *signature;
  uint64_t security_counter;
  uint32_t cp_length;
  uint32_t ap_length;
  uint32_t key_id;
  int ret;

  if (manifest == NULL || manifest_size != BK7258_BOOT_N17_MANIFEST_SIZE ||
      minimum_security_counter == 0 || ops == NULL || ops->sha256 == NULL ||
      ops->verify_signature == NULL || info == NULL)
    {
      return -EINVAL;
    }

  clear_info(info);
  key_id = getle32(manifest + KEY_ID_OFFSET);
  security_counter = getle64(manifest + SECURITY_COUNTER_OFFSET);
  cp_length = getle32(manifest + CP_LOGICAL_LENGTH_OFFSET);
  ap_length = getle32(manifest + AP_LOGICAL_LENGTH_OFFSET);
  signature = manifest + SIGNATURE_OFFSET;

  if (!bytes_equal(manifest, g_manifest_magic, sizeof(g_manifest_magic)) ||
      getle16(manifest + MANIFEST_VERSION_OFFSET) != MANIFEST_VERSION ||
      getle16(manifest + MANIFEST_SIZE_OFFSET) !=
        BK7258_BOOT_N17_MANIFEST_SIZE ||
      getle16(manifest + SIGNED_SIZE_OFFSET) !=
        BK7258_BOOT_N17_SIGNED_SIZE ||
      getle16(manifest + SIGNATURE_SIZE_OFFSET) !=
        BK7258_BOOT_N17_SIGNATURE_SIZE ||
      getle32(manifest + FLAGS_OFFSET) != 0 ||
      getle16(manifest + SIGNATURE_ALGORITHM_OFFSET) !=
        SIGNATURE_ALGORITHM_ECDSA_P256_SHA256 ||
      getle16(manifest + DIGEST_ALGORITHM_OFFSET) !=
        DIGEST_ALGORITHM_SHA256 ||
      getle16(manifest + IMAGE_ENCODING_OFFSET) !=
        IMAGE_ENCODING_BEKEN_CRC ||
      getle16(manifest + COMPONENT_COUNT_OFFSET) != COMPONENT_COUNT ||
      key_id == 0 || security_counter == 0 ||
      !exact_text(manifest + PRODUCT_ID_OFFSET, 16,
                  "openvela-bk7258", 15) ||
      !exact_text(manifest + BOARD_ID_OFFSET, 16, "bk7258-t5ai", 11) ||
      !exact_text(manifest + CHIP_ID_OFFSET, 16, "bk7258", 6) ||
      !bytes_equal(manifest + LAYOUT_SHA256_OFFSET, g_layout_sha256, 32) ||
      !canonical_release(manifest + RELEASE_VERSION_OFFSET) ||
      getle32(manifest + PAIR_PHYSICAL_SIZE_OFFSET) !=
        BK7258_ROLE_SLOT_B_PAIR_SIZE ||
      cp_length == 0 || cp_length > BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE ||
      ap_length == 0 || ap_length > BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE ||
      !bytes_value(manifest + RESERVED0_OFFSET, 4, 0) ||
      !bytes_value(manifest + RESERVED_SIGNED_OFFSET, 192, 0) ||
      !scalar_valid(signature) || !scalar_valid(signature + 32) ||
      big_compare(signature + 32, g_p256_half_order, 32) > 0)
    {
      return -EBADMSG;
    }

  if (security_counter < minimum_security_counter)
    {
      return -ESTALE;
    }

  ret = ops->verify_signature(ops->arg, key_id, manifest, signature);
  if (ret != 0)
    {
      return ret < 0 ? ret : -EPERM;
    }

  ret = ops->sha256(ops->arg, manifest, BK7258_BOOT_N17_SIGNED_SIZE,
                    info->signed_sha256);
  if (ret != 0)
    {
      clear_info(info);
      return ret < 0 ? ret : -EIO;
    }

  info->key_id = key_id;
  info->security_counter = security_counter;
  info->pair_physical_size = getle32(manifest + PAIR_PHYSICAL_SIZE_OFFSET);
  info->cp_logical_length = cp_length;
  info->ap_logical_length = ap_length;
  bytes_copy(info->pair_sha256, manifest + PAIR_SHA256_OFFSET, 32);
  bytes_copy(info->cp_sha256, manifest + CP_SHA256_OFFSET, 32);
  bytes_copy(info->ap_sha256, manifest + AP_SHA256_OFFSET, 32);
  return 0;
}
