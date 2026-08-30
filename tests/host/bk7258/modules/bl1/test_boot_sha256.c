/*
 * test_boot_sha256.c - host unit tests for boot_sha256.c.
 *
 * Vectors: FIPS 180-4 plus a 1,000,000-byte "a" digest.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "boot_sha256.h"

static void digest_bytes(const uint8_t *data, size_t len, uint8_t out[32])
{
  struct boot_sha256_context_s context;

  boot_sha256_init(&context);
  boot_sha256_update(&context, data, len);
  boot_sha256_final(&context, out);
}

static void expect_digest(const uint8_t *data, size_t len,
                          const char *expected_hex)
{
  uint8_t digest[32];
  char hex[65];
  size_t index;

  digest_bytes(data, len, digest);
  for (index = 0; index < 32; index++)
    {
      snprintf(hex + index * 2, 3, "%02x", digest[index]);
    }

  hex[64] = '\0';
  assert_string_equal(hex, expected_hex);
}

static void test_empty_message(void **state)
{
  expect_digest((const uint8_t *)"", 0,
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

static void test_abc(void **state)
{
  expect_digest((const uint8_t *)"abc", 3,
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_two_block(void **state)
{
  expect_digest((const uint8_t *)
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void fill_pattern(uint8_t *msg, size_t len)
{
  size_t index;

  for (index = 0; index < len; index++)
    {
      msg[index] = (uint8_t)('a' + (index % 26));
    }
}

/* Exercise the final()-padding branches: used < 56 (55), used == 56 (one
 * extra transform), used > 56 (55+pad+transform+words), and full block
 * boundaries at 64 and 65 bytes. */
static void test_padding_boundaries(void **state)
{
  static const size_t lengths[] = { 55, 56, 57, 64, 65 };
  static const char *expected[] =
  {
    "595615dbe4f0f407ae397d08b4c2cb870cb9b0e11937416f950c5160acf9c005",
    "784f623b787495078e93ff28a25b581df0584055a7e71d8cd90c454716b92f51",
    "808f0738aa4401bdee842e5a15a7baad5809f976d8eb6f9bd2683cebd2e8d671",
    "2fcd5a0d60e4c941381fcc4e00a4bf8be422c3ddfafb93c809e8d1e2bfffae8e",
    "1b3cd1877ab2f2f19f7be001722554f336cb799df0329de0bb4c118dc6abc06d",
  };
  size_t index;

  for (index = 0; index < sizeof(lengths) / sizeof(lengths[0]); index++)
    {
      uint8_t msg[65];

      fill_pattern(msg, lengths[index]);
      expect_digest(msg, lengths[index], expected[index]);
    }
}

static void test_million_a(void **state)
{
  const uint8_t *buf = malloc(1000000);
  size_t index;

  assert_non_null(buf);
  memset((void *)buf, 'a', 1000000);
  expect_digest(buf, 1000000,
                "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  free((void *)buf);
}

static void test_incremental_update(void **state)
{
  struct boot_sha256_context_s context;
  uint8_t digest[32];
  char hex[65];
  size_t index;

  boot_sha256_init(&context);
  boot_sha256_update(&context, (const uint8_t *)"abc", 2);
  boot_sha256_update(&context, (const uint8_t *)"c", 1);
  boot_sha256_final(&context, digest);

  for (index = 0; index < 32; index++)
    {
      snprintf(hex + index * 2, 3, "%02x", digest[index]);
    }

  hex[64] = '\0';
  assert_string_equal(hex,
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  /* The final call does NOT reset the context; re-init is required before
   * reuse.  Verify the second round still produces the same digest. */
  boot_sha256_init(&context);
  boot_sha256_update(&context, (const uint8_t *)"abc", 3);
  boot_sha256_final(&context, digest);
  for (index = 0; index < 32; index++)
    {
      snprintf(hex + index * 2, 3, "%02x", digest[index]);
    }

  hex[64] = '\0';
  assert_string_equal(hex,
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test(test_empty_message),
    cmocka_unit_test(test_abc),
    cmocka_unit_test(test_two_block),
    cmocka_unit_test(test_padding_boundaries),
    cmocka_unit_test(test_million_a),
    cmocka_unit_test(test_incremental_update),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}