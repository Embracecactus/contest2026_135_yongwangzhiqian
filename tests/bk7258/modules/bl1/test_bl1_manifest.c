/*
 * test_bl1_manifest.c - host unit tests for boot_bl1_manifest.c.
 *
 * The record is built in host RAM and passed as the "XIP" address; the BL2
 * digest is computed from bytes planted in the mock flash window.  The
 * ECDSA verifier is mocked (verify result + captured arguments).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "mock_reg32.h"
#include "mock_flash.h"
#include "mock_tinycrypt.h"
#include "boot_bl1_manifest.h"
#include "boot_sha256.h"
#include "boot_bl1_policy.h"

#define BL2_XIP   0x02010000u
#define BL2_LOAD  0x28010000u
#define BL2_SIZE  0x40u

/* The manifest record lives in the XIP window, where verify_at() expects
 * it: the mock flash map is placed at the BK7258 XIP base, and the records
 * are written there directly. */
#define MANIFEST_XIP 0x02000000u

#define DUBHE_SHADOW  0x4b111000u
#define OTP_COUNTER   (DUBHE_SHADOW + 0x88u)
#define OTP_PK_HASH   (DUBHE_SHADOW + 0x28u)
#define OTP_LCS       (DUBHE_SHADOW + 0x68u)

static uint8_t *g_manifest;
static uint8_t g_bl2_data[BL2_SIZE];

extern const uint8_t bk7258_bl1_manifest_root_public_key[64];
extern const uint8_t bk7258_bl1_manifest_root_public_key_hash[32];

static void put_le32(uint8_t *dst, uint32_t word)
{
  dst[0] = (uint8_t)word;
  dst[1] = (uint8_t)(word >> 8);
  dst[2] = (uint8_t)(word >> 16);
  dst[3] = (uint8_t)(word >> 24);
}

static uint32_t get_le32(const uint8_t *src)
{
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static void sha256_of(const uint8_t *data, size_t len, uint8_t out[32])
{
  struct boot_sha256_context_s context;

  boot_sha256_init(&context);
  boot_sha256_update(&context, data, len);
  boot_sha256_final(&context, out);
}

/* The Beken software root is a fixed 32-byte hash whose preimage is not
 * linked into the test.  Anchor the record's key through the OTP branch
 * instead: program the OTP hash slot with SHA-256(0x04 || root key) so
 * bk7258_bl1_root_hash_matches() compares against it (non-empty OTP hash
 * path). */
static void seed_beken_otp_root(void)
{
  uint8_t key_blob[BK7258_BEKEN_MANIFEST_PUBLIC_KEY_SIZE];
  uint8_t key_digest[32];
  uint32_t word;
  size_t index;

  key_blob[0] = 0x04u;
  memcpy(key_blob + 1u, bk7258_bl1_manifest_root_public_key, 64u);
  sha256_of(key_blob, sizeof(key_blob), key_digest);

  for (index = 0; index < BK7258_DUBHE_OTP_SECURE_BOOT_PK_HASH_SIZE / 4u;
       index++)
    {
      word = (uint32_t)key_digest[index * 4u + 0u] |
             ((uint32_t)key_digest[index * 4u + 1u] << 8) |
             ((uint32_t)key_digest[index * 4u + 2u] << 16) |
             ((uint32_t)key_digest[index * 4u + 3u] << 24);
      mock_reg32_write(OTP_PK_HASH + index * 4u, word);
    }
}

/* Build a valid one-image Beken candidate record in `record`.  The record
 * area is BK7258_BL1_MANIFEST_SIZE bytes so the erased-tail check never
 * reads past the array. */
static void build_beken_record(uint8_t *record)
{
  memset(record, 0xff, BK7258_BL1_MANIFEST_SIZE);
  put_le32(record, BK7258_BEKEN_MANIFEST_MAGIC);
  put_le32(record + 4u, BK7258_BEKEN_MANIFEST_LAYOUT_VERSION);
  put_le32(record + 8u, 1u); /* image version */
  put_le32(record + 12u, BK7258_BEKEN_MANIFEST_TOTAL_SIZE);
  put_le32(record + 16u, BK7258_BEKEN_MANIFEST_FLAG_EC256_SHA256);
  put_le32(record + 20u, BK7258_BEKEN_MANIFEST_IMAGE_COUNT);
  put_le32(record + 24u, BK7258_BEKEN_MANIFEST_IMAGE_FLAGS);
  put_le32(record + 28u, BK7258_BEKEN_MANIFEST_IMAGE_VERSION);
  put_le32(record + 32u, BL2_XIP); /* static offset */
  put_le32(record + 36u, BL2_LOAD); /* load offset */
  put_le32(record + 40u, BL2_SIZE); /* image size */
  put_le32(record + 44u, BL2_LOAD); /* entry */
  put_le32(record + BK7258_BEKEN_MANIFEST_RESERVED_OFFSET, 0u);
  record[BK7258_BEKEN_MANIFEST_PUBLIC_KEY_OFFSET] = 0x04u;
  memcpy(record + BK7258_BEKEN_MANIFEST_PUBLIC_KEY_OFFSET + 1u,
         bk7258_bl1_manifest_root_public_key, 64u);
  sha256_of((const uint8_t *)BL2_XIP, BL2_SIZE,
            record + BK7258_BEKEN_MANIFEST_IMAGE_DIGEST_OFFSET);
}

static int setup(void **state)
{
  size_t index;

  mock_reg32_reset();
  mock_tinycrypt_reset();

  if (mock_flash_map() == NULL)
    {
      return -1;
    }

  g_manifest = (uint8_t *)(uintptr_t)MANIFEST_XIP;

  for (index = 0; index < BL2_SIZE; index++)
    {
      g_bl2_data[index] = (uint8_t)(index * 13u + 7u);
    }

  memcpy((void *)BL2_XIP, g_bl2_data, BL2_SIZE);
  mock_flash_erase_fill(BL2_XIP - 0x02000000u + BL2_SIZE,
                        BK7258_HOST_FLASH_SIZE - (BL2_XIP - 0x02000000u) -
                        BL2_SIZE);

  memset(g_manifest, 0xff, sizeof(uint8_t[BK7258_BL1_MANIFEST_SIZE]));
  memcpy(g_manifest, "BKBL1M2", 7u);
  g_manifest[7] = 0u;
  put_le32(g_manifest + 8u,  BK7258_BL1_MANIFEST_FORMAT);
  put_le32(g_manifest + 12u, BK7258_BL1_MANIFEST_SIGNATURE_ALG);
  put_le32(g_manifest + 16u, BK7258_BL1_MANIFEST_DIGEST_ALG);
  put_le32(g_manifest + 20u, BK7258_BL1_MANIFEST_KEY_ID);
  put_le32(g_manifest + 24u, 1u); /* image version */
  put_le32(g_manifest + 28u, 0u); /* flags */
  put_le32(g_manifest + 32u, BL2_XIP);
  put_le32(g_manifest + 36u, BL2_SIZE);
  put_le32(g_manifest + 40u, BL2_LOAD);
  put_le32(g_manifest + 44u, 0u);
  memcpy(g_manifest + BK7258_BL1_MANIFEST_PUBLIC_KEY_OFFSET,
         bk7258_bl1_manifest_root_public_key, 64u);
  sha256_of(g_manifest + BK7258_BL1_MANIFEST_PUBLIC_KEY_OFFSET, 64u,
            g_manifest + BK7258_BL1_MANIFEST_KEY_HASH_OFFSET);
  sha256_of((const uint8_t *)BL2_XIP, BL2_SIZE,
            g_manifest + BK7258_BL1_MANIFEST_DIGEST_OFFSET);
  return 0;
}

static int teardown(void **state)
{
  mock_flash_unmap();
  return 0;
}

static void test_valid_manifest(void **state)
{
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   0);
  assert_int_equal(mock_tinycrypt_verify_calls(), 1u);
  /* The verifier saw the real software root public key. */
  assert_true(mock_tinycrypt_verify_pubkey_is(
                bk7258_bl1_manifest_root_public_key, 64u));
}

static void test_wrong_magic(void **state)
{
  g_manifest[0] ^= 0xffu;
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -1);
}

static void test_beken_magic_dispatches_to_candidate(void **state)
{
  build_beken_record(g_manifest);
  seed_beken_otp_root();

  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   0);
}

static void test_beken_verify_buffer(void **state)
{
  uint8_t record[BK7258_BL1_MANIFEST_SIZE];

  build_beken_record(record);
  seed_beken_otp_root();

  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   0);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     NULL, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
}

static void test_beken_verify_buffer_rejects(void **state)
{
  uint8_t record[BK7258_BL1_MANIFEST_SIZE];

  build_beken_record(record);
  seed_beken_otp_root();

  /* Wrong layout version. */
  put_le32(record + 4u, 0x00020002u);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
  put_le32(record + 4u, BK7258_BEKEN_MANIFEST_LAYOUT_VERSION);

  /* Wrong total size. */
  put_le32(record + 12u, 0x100u);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
  put_le32(record + 12u, BK7258_BEKEN_MANIFEST_TOTAL_SIZE);

  /* Zero / oversized image size. */
  put_le32(record + 40u, 0u);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
  put_le32(record + 40u, BL2_SIZE + 1u);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
  put_le32(record + 40u, BL2_SIZE);

  /* Erased tail missing (byte past total_size not 0xff). */
  record[BK7258_BEKEN_MANIFEST_TOTAL_SIZE] = 0x00u;
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
  record[BK7258_BEKEN_MANIFEST_TOTAL_SIZE] = 0xffu;

  /* Public key without the 0x04 prefix. */
  record[BK7258_BEKEN_MANIFEST_PUBLIC_KEY_OFFSET] = 0x02u;
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
  record[BK7258_BEKEN_MANIFEST_PUBLIC_KEY_OFFSET] = 0x04u;

  /* Digest mismatch. */
  record[BK7258_BEKEN_MANIFEST_IMAGE_DIGEST_OFFSET] ^= 0x01u;
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -2);
  record[BK7258_BEKEN_MANIFEST_IMAGE_DIGEST_OFFSET] ^= 0x01u;

  /* Rejected signature. */
  g_uECC_verify_result = 0;
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -3);

  /* Unknown magic. */
  put_le32(record, 0xdeadbeefu);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
}

static void test_version_floor_from_otp(void **state)
{
  /* OTP counter bitmap 0x3 -> floor 2; version 1 must be rejected. */
  mock_reg32_write(OTP_COUNTER, 0x3u);

  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -1);

  put_le32(g_manifest + 24u, 2u);
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   0);
  put_le32(g_manifest + 24u, 1u);
}

static void test_beken_version_floor_from_otp(void **state)
{
  uint8_t record[BK7258_BL1_MANIFEST_SIZE];

  build_beken_record(record);
  seed_beken_otp_root();

  mock_reg32_write(OTP_COUNTER, 0x3u);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -1);
  mock_reg32_write(OTP_COUNTER, 0u);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   0);
}

static void test_otp_root_policy(void **state)
{
  uint8_t record[BK7258_BL1_MANIFEST_SIZE];

  build_beken_record(record);

  /* A non-empty OTP hash that differs from the record key's digest: the
   * beken candidate must fail to anchor (-4). */
  mock_reg32_write(OTP_PK_HASH, 0xdeadbeefu);
  mock_reg32_write(OTP_PK_HASH + 4u, 0xcafebabeu);

  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -4);

  /* Empty OTP hash + LCS != CM must also fail closed. */
  mock_reg32_reset();
  mock_reg32_write(OTP_LCS, 0x3u);
  assert_int_equal(bk7258_beken_manifest_verify_buffer(
                     record, BL2_XIP, BL2_SIZE, BL2_LOAD),
                   -4);
}

static void test_key_hash_mismatch(void **state)
{
  g_manifest[BK7258_BL1_MANIFEST_KEY_HASH_OFFSET] ^= 0x01u;
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -4);
  g_manifest[BK7258_BL1_MANIFEST_KEY_HASH_OFFSET] ^= 0x01u;
}

static void test_bl2_digest_mismatch(void **state)
{
  g_manifest[BK7258_BL1_MANIFEST_DIGEST_OFFSET] ^= 0x01u;
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -2);
  g_manifest[BK7258_BL1_MANIFEST_DIGEST_OFFSET] ^= 0x01u;
}

static void test_signature_rejected(void **state)
{
  g_uECC_verify_result = 0;
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -3);
}

static void test_record_field_rejections(void **state)
{
  uint32_t offsets[] = { 8u, 12u, 16u, 20u, 28u, 44u };
  size_t index;

  for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++)
    {
      uint32_t saved = get_le32(g_manifest + offsets[index]);

      put_le32(g_manifest + offsets[index], ~saved);
      assert_int_equal(bk7258_bl1_manifest_verify_at(
                         (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                         BL2_LOAD),
                       -1);
      put_le32(g_manifest + offsets[index], saved);
    }

  /* Fixed handoff fields must match the arguments exactly. */
  put_le32(g_manifest + 32u, BL2_XIP + 0x10u);
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -1);
  put_le32(g_manifest + 32u, BL2_XIP);
  put_le32(g_manifest + 36u, BL2_SIZE + 0x10u);
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -1);
  put_le32(g_manifest + 36u, BL2_SIZE);
  put_le32(g_manifest + 40u, BL2_LOAD + 0x10u);
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -1);
  put_le32(g_manifest + 40u, BL2_LOAD);

  /* Reserved tail not erased. */
  g_manifest[BK7258_BL1_MANIFEST_RESERVED_OFFSET] = 0x00u;
  assert_int_equal(bk7258_bl1_manifest_verify_at(
                     (uint32_t)(uintptr_t)g_manifest, BL2_XIP, BL2_SIZE,
                     BL2_LOAD),
                   -1);
  g_manifest[BK7258_BL1_MANIFEST_RESERVED_OFFSET] = 0xffu;
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup_teardown(test_valid_manifest, setup, teardown),
    cmocka_unit_test_setup_teardown(test_wrong_magic, setup, teardown),
    cmocka_unit_test_setup_teardown(test_beken_magic_dispatches_to_candidate,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_beken_verify_buffer, setup, teardown),
    cmocka_unit_test_setup_teardown(test_beken_verify_buffer_rejects,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_version_floor_from_otp,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_beken_version_floor_from_otp,
                                    setup, teardown),
    cmocka_unit_test_setup_teardown(test_otp_root_policy, setup, teardown),
    cmocka_unit_test_setup_teardown(test_key_hash_mismatch, setup, teardown),
    cmocka_unit_test_setup_teardown(test_bl2_digest_mismatch, setup, teardown),
    cmocka_unit_test_setup_teardown(test_signature_rejected, setup, teardown),
    cmocka_unit_test_setup_teardown(test_record_field_rejections,
                                    setup, teardown),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}