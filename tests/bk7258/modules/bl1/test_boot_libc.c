/*
 * test_boot_libc.c - host unit tests for boot_libc.c (freestanding libc).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "boot_libc.h"

#define LIB_CASES 8

static void test_memcpy_bytewise(void **state)
{
  static const uint8_t src[64] =
  {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
  };
  uint8_t dst[64];

  memset(dst, 0xa5, sizeof(dst));
  assert_ptr_equal(memcpy(dst, src, sizeof(src)), dst);
  assert_memory_equal(dst, src, sizeof(src));

  memset(dst, 0xa5, sizeof(dst));
  memcpy(dst, src, 0);
  assert_memory_equal(dst, dst, sizeof(dst));
  assert_int_equal(dst[0], 0xa5);

  memcpy(dst, src, 1);
  assert_int_equal(dst[0], 0x00);
  assert_int_equal(dst[1], 0xa5);
}

static void test_memcpy_unaligned(void **state)
{
  const uint8_t src[17] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                            11, 12, 13, 14, 15, 16, 17 };
  uint8_t dst[17];

  memset(dst, 0, sizeof(dst));
  memcpy(dst + 1, src, 16);
  assert_memory_equal(dst + 1, src, 16);
  assert_int_equal(dst[0], 0);
}

static void test_memset(void **state)
{
  uint8_t buf[32];
  size_t index;

  assert_ptr_equal(memset(buf, 0x5a, sizeof(buf)), buf);
  for (index = 0; index < sizeof(buf); index++)
    {
      assert_int_equal(buf[index], 0x5a);
    }

  memset(buf + 4, 0x7f, 8);
  assert_int_equal(buf[3], 0x5a);
  assert_int_equal(buf[4], 0x7f);
  assert_int_equal(buf[11], 0x7f);
  assert_int_equal(buf[12], 0x5a);

  memset(buf, 0xff, sizeof(buf));
  assert_int_equal(buf[0], 0xff);
  assert_int_equal(buf[31], 0xff);
}

static void test_memcmp(void **state)
{
  const uint8_t a[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  const uint8_t b[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  const uint8_t c[] = { 1, 2, 3, 4, 0, 6, 7, 8 };

  assert_int_equal(memcmp(a, b, sizeof(a)), 0);
  assert_int_equal(memcmp(a, a, 0), 0);
  assert_int_equal(memcmp(a, c, sizeof(a)) > 0, 1);
  assert_int_equal(memcmp(c, a, sizeof(a)) < 0, 1);
  assert_int_equal(memcmp(a, c, 4), 0);
}

static void test_strcmp(void **state)
{
  assert_int_equal(strcmp("", ""), 0);
  assert_int_equal(strcmp("abc", "abc"), 0);
  assert_true(strcmp("abc", "abd") < 0);
  assert_true(strcmp("abd", "abc") > 0);
  assert_true(strcmp("abc", "ab") > 0);
  assert_true(strcmp("ab", "abc") < 0);
  assert_true(strcmp("ABC", "abc") < 0);
}

static void test_memcpy_large(void **state)
{
  enum { SIZE = 4096 };
  uint8_t src[SIZE];
  uint8_t dst[SIZE];
  size_t index;

  for (index = 0; index < SIZE; index++)
    {
      src[index] = (uint8_t)(index * 7u + 1u);
    }

  memcpy(dst, src, SIZE);
  assert_memory_equal(dst, src, SIZE);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test(test_memcpy_bytewise),
    cmocka_unit_test(test_memcpy_unaligned),
    cmocka_unit_test(test_memset),
    cmocka_unit_test(test_memcmp),
    cmocka_unit_test(test_strcmp),
    cmocka_unit_test(test_memcpy_large),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}