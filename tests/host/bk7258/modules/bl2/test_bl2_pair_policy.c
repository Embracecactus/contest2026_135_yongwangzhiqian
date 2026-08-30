/* SPDX-License-Identifier: Apache-2.0 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "bk7258_bl2_pair_policy.h"

static struct image_version version(uint8_t major, uint8_t minor,
                                    uint16_t revision, uint32_t build)
{
  struct image_version result =
  {
    .iv_major = major,
    .iv_minor = minor,
    .iv_revision = revision,
    .iv_build_num = build,
  };

  return result;
}

static void test_rejects_null_arguments(void **state)
{
  struct bk7258_bl2_pair_candidate_s candidates[2] = { 0 };
  struct bk7258_bl2_pair_order_s order;

  (void)state;
  assert_false(bk7258_bl2_pair_order(NULL, &order));
  assert_false(bk7258_bl2_pair_order(candidates, NULL));
}

static void test_rejects_when_neither_pair_is_usable(void **state)
{
  struct bk7258_bl2_pair_candidate_s candidates[2] = { 0 };
  struct bk7258_bl2_pair_order_s order = { 99, 99 };

  (void)state;
  assert_false(bk7258_bl2_pair_order(candidates, &order));
  assert_int_equal(order.preferred, BK7258_BL2_PAIR_SLOT_NONE);
  assert_int_equal(order.fallback, BK7258_BL2_PAIR_SLOT_NONE);
}

static void test_selects_only_usable_pair(void **state)
{
  struct bk7258_bl2_pair_candidate_s candidates[2] = { 0 };
  struct bk7258_bl2_pair_order_s order;

  (void)state;
  candidates[BK7258_BL2_PAIR_SLOT_PRIMARY].usable = true;
  assert_true(bk7258_bl2_pair_order(candidates, &order));
  assert_int_equal(order.preferred, BK7258_BL2_PAIR_SLOT_PRIMARY);
  assert_int_equal(order.fallback, BK7258_BL2_PAIR_SLOT_NONE);

  candidates[BK7258_BL2_PAIR_SLOT_PRIMARY].usable = false;
  candidates[BK7258_BL2_PAIR_SLOT_SECONDARY].usable = true;
  assert_true(bk7258_bl2_pair_order(candidates, &order));
  assert_int_equal(order.preferred, BK7258_BL2_PAIR_SLOT_SECONDARY);
  assert_int_equal(order.fallback, BK7258_BL2_PAIR_SLOT_NONE);
}

static void assert_order(const struct image_version *primary,
                         const struct image_version *secondary,
                         int preferred, int fallback)
{
  struct bk7258_bl2_pair_candidate_s candidates[2] =
  {
    { true, *primary },
    { true, *secondary },
  };
  struct bk7258_bl2_pair_order_s order;

  assert_true(bk7258_bl2_pair_order(candidates, &order));
  assert_int_equal(order.preferred, preferred);
  assert_int_equal(order.fallback, fallback);
}

static void test_orders_every_version_component(void **state)
{
  struct image_version base = version(1, 2, 3, 4);
  struct image_version newer;

  (void)state;
  newer = version(2, 0, 0, 0);
  assert_order(&base, &newer, BK7258_BL2_PAIR_SLOT_SECONDARY,
               BK7258_BL2_PAIR_SLOT_PRIMARY);
  newer = version(1, 3, 0, 0);
  assert_order(&base, &newer, BK7258_BL2_PAIR_SLOT_SECONDARY,
               BK7258_BL2_PAIR_SLOT_PRIMARY);
  newer = version(1, 2, 4, 0);
  assert_order(&base, &newer, BK7258_BL2_PAIR_SLOT_SECONDARY,
               BK7258_BL2_PAIR_SLOT_PRIMARY);
  newer = version(1, 2, 3, 5);
  assert_order(&base, &newer, BK7258_BL2_PAIR_SLOT_SECONDARY,
               BK7258_BL2_PAIR_SLOT_PRIMARY);

  newer = version(2, 0, 0, 0);
  assert_order(&newer, &base, BK7258_BL2_PAIR_SLOT_PRIMARY,
               BK7258_BL2_PAIR_SLOT_SECONDARY);
}

static void test_equal_versions_prefer_primary(void **state)
{
  struct image_version equal = version(9, 8, 7, 6);

  (void)state;
  assert_order(&equal, &equal, BK7258_BL2_PAIR_SLOT_PRIMARY,
               BK7258_BL2_PAIR_SLOT_SECONDARY);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test(test_rejects_null_arguments),
    cmocka_unit_test(test_rejects_when_neither_pair_is_usable),
    cmocka_unit_test(test_selects_only_usable_pair),
    cmocka_unit_test(test_orders_every_version_component),
    cmocka_unit_test(test_equal_versions_prefer_primary),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
