/*
 * test_bl2_security_cnt.c - host tests for the BK7258 BL2 security-counter
 * backend (bk7258_bl2_security_cnt.c).
 *
 * The real implementation is compiled from a throwaway patched copy where
 * BK7258_BL2_OTP_REG32 is routed to mock_reg32_ref(); the Dubhe OTP shadow
 * counter window (0x4b111100, 64 bytes / 16 words) is preset and inspected
 * through the framework MMIO map.
 *
 * The test build defines BK7258_BL2_SECURITY_COUNTER_FLOOR=7 so the
 * compile-time floor vs. OTP bitmap max() logic is exercised both ways.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <bootutil/security_cnt.h>

#include "mock_reg32.h"

#define OTP_CNT_BASE 0x4b111100u
#define OTP_CNT_WORDS 16u

static void seed_otp_zero(void)
{
  uint32_t index;

  for (index = 0; index < OTP_CNT_WORDS; index++)
    {
      mock_reg32_set(OTP_CNT_BASE + index * sizeof(uint32_t), 0u);
    }
}

static void seed_otp_popcount(uint32_t bits)
{
  uint32_t index;

  seed_otp_zero();
  for (index = 0; index < bits; index++)
    {
      mock_reg32_set(OTP_CNT_BASE + (index % OTP_CNT_WORDS) * sizeof(uint32_t),
                     1u << (index / OTP_CNT_WORDS));
    }
}

static void test_security_cnt_init_success(void **state)
{
  (void)state;
  seed_otp_zero();
  assert_int_equal(boot_nv_security_counter_init(), FIH_SUCCESS);
}

static void test_get_returns_compile_floor_when_otp_empty(void **state)
{
  fih_int value = -1;

  (void)state;
  seed_otp_zero();
  assert_int_equal(boot_nv_security_counter_get(0u, &value), FIH_SUCCESS);
  assert_int_equal(value, 7);
}

static void test_get_null_pointer_fails(void **state)
{
  (void)state;
  seed_otp_zero();
  assert_int_equal(boot_nv_security_counter_get(0u, NULL), FIH_FAILURE);
}

static void test_get_floor_dominates_small_otp_count(void **state)
{
  fih_int value = -1;

  (void)state;
  /* Three bits programmed: OTP count 3 is below the floor of 7. */
  seed_otp_popcount(3u);
  assert_int_equal(boot_nv_security_counter_get(0u, &value), FIH_SUCCESS);
  assert_int_equal(value, 7);
}

static void test_get_otp_count_wins_above_floor(void **state)
{
  fih_int value = -1;

  (void)state;
  /* A 16-bit word: OTP count 16 exceeds the floor of 7. */
  seed_otp_zero();
  mock_reg32_set(OTP_CNT_BASE, 0x0000ffffu);
  assert_int_equal(boot_nv_security_counter_get(0u, &value), FIH_SUCCESS);
  assert_int_equal(value, 16);
}

static void test_get_counts_bits_across_all_words(void **state)
{
  fih_int value = -1;

  (void)state;
  /* 0x0f0f0f0f = 16 bits per word, all 16 words: 256 bits. */
  {
    uint32_t index;

    seed_otp_zero();
    for (index = 0; index < OTP_CNT_WORDS; index++)
      {
        mock_reg32_set(OTP_CNT_BASE + index * sizeof(uint32_t), 0x0f0f0f0fu);
      }
  }
  assert_int_equal(boot_nv_security_counter_get(0u, &value), FIH_SUCCESS);
  assert_int_equal(value, 256);
}

static void test_get_full_bitmap(void **state)
{
  fih_int value = -1;

  (void)state;
  seed_otp_zero();
  mock_reg32_set(OTP_CNT_BASE, UINT32_MAX);
  mock_reg32_set(OTP_CNT_BASE + 15u * sizeof(uint32_t), UINT32_MAX);
  /* Two full words: 64 bits. */
  assert_int_equal(boot_nv_security_counter_get(0u, &value), FIH_SUCCESS);
  assert_int_equal(value, 64);
}

static void test_update_accepts_at_or_above_floor(void **state)
{
  (void)state;
  seed_otp_zero();
  assert_int_equal(boot_nv_security_counter_update(0u, 7u), 0);
  assert_int_equal(boot_nv_security_counter_update(0u, 100u), 0);
}

static void test_update_rejects_below_floor(void **state)
{
  (void)state;
  seed_otp_zero();
  assert_int_equal(boot_nv_security_counter_update(0u, 6u), -1);
  assert_int_equal(boot_nv_security_counter_update(0u, 0u), -1);
}

static void test_update_accepts_above_otp_count(void **state)
{
  (void)state;
  /* OTP count 9 (above the floor): update at 9 and below 9. */
  seed_otp_popcount(9u);
  assert_int_equal(boot_nv_security_counter_update(0u, 9u), 0);
  assert_int_equal(boot_nv_security_counter_update(0u, 8u), -1);
}

static void test_update_ignores_image_id(void **state)
{
  (void)state;
  seed_otp_zero();
  assert_int_equal(boot_nv_security_counter_update(0xdeadbeefu, 7u), 0);
}

static void test_get_ignores_image_id(void **state)
{
  fih_int value = -1;

  (void)state;
  seed_otp_popcount(3u);
  assert_int_equal(boot_nv_security_counter_get(7u, &value), FIH_SUCCESS);
  assert_int_equal(value, 7);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test(test_security_cnt_init_success),
    cmocka_unit_test(test_get_returns_compile_floor_when_otp_empty),
    cmocka_unit_test(test_get_null_pointer_fails),
    cmocka_unit_test(test_get_floor_dominates_small_otp_count),
    cmocka_unit_test(test_get_otp_count_wins_above_floor),
    cmocka_unit_test(test_get_counts_bits_across_all_words),
    cmocka_unit_test(test_get_full_bitmap),
    cmocka_unit_test(test_update_accepts_at_or_above_floor),
    cmocka_unit_test(test_update_rejects_below_floor),
    cmocka_unit_test(test_update_accepts_above_otp_count),
    cmocka_unit_test(test_update_ignores_image_id),
    cmocka_unit_test(test_get_ignores_image_id),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
