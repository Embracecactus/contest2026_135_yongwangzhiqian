/*
 * test_bl2_keys.c - host tests for the BK7258 BL2 public-key material
 * (bk7258_bl2_keys.c).
 *
 * The module is compiled unmodified; the mock bootutil/sign_key.h provides
 * the struct bootutil_key shape.  The tests pin the DER structure of the
 * development P-256 key so a wrong key or a truncated array cannot pass
 * silently.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <bootutil/sign_key.h>

extern const unsigned char ecdsa_pub_key[];
extern const unsigned int ecdsa_pub_key_len;
extern const struct bootutil_key bootutil_keys[];
extern const int bootutil_key_cnt;

static void test_single_bootutil_key(void **state)
{
  (void)state;
  assert_int_equal(bootutil_key_cnt, 1);
}

static void test_key_links_der_array(void **state)
{
  (void)state;
  assert_ptr_equal(bootutil_keys[0].key, ecdsa_pub_key);
  assert_int_equal(*bootutil_keys[0].len, ecdsa_pub_key_len);
  assert_int_equal(ecdsa_pub_key_len, 91);
}

static void test_der_structure(void **state)
{
  const uint8_t oid_p256[] = { 0x2a, 0x86, 0x48, 0xce };
  const uint8_t oid_tail[] = { 0x2a, 0x86, 0x48, 0xce, 0x3d };

  (void)state;
  /* SEQUENCE (89-byte body). */
  assert_int_equal(ecdsa_pub_key[0], 0x30);
  assert_int_equal(ecdsa_pub_key[1], 0x59);
  /* AlgorithmIdentifier: SEQUENCE(19), OID(7) = P-256. */
  assert_int_equal(ecdsa_pub_key[2], 0x30);
  assert_int_equal(ecdsa_pub_key[3], 0x13);
  assert_int_equal(ecdsa_pub_key[4], 0x06);
  assert_int_equal(ecdsa_pub_key[5], 0x07);
  assert_memory_equal(&ecdsa_pub_key[6], oid_p256, sizeof(oid_p256));
  /* Continued OID 1.2.840.10045.3.1.7 (ecPublicKey, prime256v1). */
  assert_int_equal(ecdsa_pub_key[13], 0x06);
  assert_int_equal(ecdsa_pub_key[14], 0x08);
  assert_memory_equal(&ecdsa_pub_key[15], oid_tail, sizeof(oid_tail));
  assert_int_equal(ecdsa_pub_key[22], 0x07);
  /* SubjectPublicKey: BIT STRING(66), 0 unused bits, uncompressed point. */
  assert_int_equal(ecdsa_pub_key[23], 0x03);
  assert_int_equal(ecdsa_pub_key[24], 0x42);
  assert_int_equal(ecdsa_pub_key[25], 0x00);
  assert_int_equal(ecdsa_pub_key[26], 0x04);
}

static void test_point_coordinates_present(void **state)
{
  uint32_t i;
  int any_set = 0;

  (void)state;
  /* 64 bytes of X||Y starting at index 27. */
  assert_int_equal(ecdsa_pub_key_len, 91u);
  for (i = 27; i < 91; i++)
    {
      any_set |= (ecdsa_pub_key[i] != 0);
    }
  assert_true(any_set);
  assert_false(memcmp(ecdsa_pub_key + 27, ecdsa_pub_key + 59, 32) == 0);
}

static void test_key_is_not_the_ap_port_key(void **state)
{
  (void)state;
  /* The NuttX-side mcuboot port key carries 0x2a, 0xcb, 0x40, 0x3c as its
   * first X-coordinate bytes (index 27..30); the BL2 development key must
   * differ so a stray image cannot validate against both trust roots. */
  assert_int_equal(ecdsa_pub_key[27], 0x01u);
  assert_int_not_equal(ecdsa_pub_key[28], 0xcbu);
  assert_int_not_equal(ecdsa_pub_key[29], 0x40u);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test(test_single_bootutil_key),
    cmocka_unit_test(test_key_links_der_array),
    cmocka_unit_test(test_der_structure),
    cmocka_unit_test(test_point_coordinates_present),
    cmocka_unit_test(test_key_is_not_the_ap_port_key),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
