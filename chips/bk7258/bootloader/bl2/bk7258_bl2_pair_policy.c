/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>

#include "bk7258_bl2_pair_policy.h"

static int bk7258_bl2_version_compare(const struct image_version *left,
                                      const struct image_version *right)
{
  if (left->iv_major != right->iv_major)
    {
      return left->iv_major > right->iv_major ? 1 : -1;
    }

  if (left->iv_minor != right->iv_minor)
    {
      return left->iv_minor > right->iv_minor ? 1 : -1;
    }

  if (left->iv_revision != right->iv_revision)
    {
      return left->iv_revision > right->iv_revision ? 1 : -1;
    }

  if (left->iv_build_num != right->iv_build_num)
    {
      return left->iv_build_num > right->iv_build_num ? 1 : -1;
    }

  return 0;
}

bool bk7258_bl2_pair_order(
  const struct bk7258_bl2_pair_candidate_s candidates[2],
  struct bk7258_bl2_pair_order_s *order)
{
  if (candidates == NULL || order == NULL)
    {
      return false;
    }

  order->preferred = BK7258_BL2_PAIR_SLOT_NONE;
  order->fallback = BK7258_BL2_PAIR_SLOT_NONE;

  if (candidates[BK7258_BL2_PAIR_SLOT_PRIMARY].usable &&
      candidates[BK7258_BL2_PAIR_SLOT_SECONDARY].usable)
    {
      if (bk7258_bl2_version_compare(
            &candidates[BK7258_BL2_PAIR_SLOT_SECONDARY].version,
            &candidates[BK7258_BL2_PAIR_SLOT_PRIMARY].version) > 0)
        {
          order->preferred = BK7258_BL2_PAIR_SLOT_SECONDARY;
          order->fallback = BK7258_BL2_PAIR_SLOT_PRIMARY;
        }
      else
        {
          order->preferred = BK7258_BL2_PAIR_SLOT_PRIMARY;
          order->fallback = BK7258_BL2_PAIR_SLOT_SECONDARY;
        }

      return true;
    }

  if (candidates[BK7258_BL2_PAIR_SLOT_PRIMARY].usable)
    {
      order->preferred = BK7258_BL2_PAIR_SLOT_PRIMARY;
      return true;
    }

  if (candidates[BK7258_BL2_PAIR_SLOT_SECONDARY].usable)
    {
      order->preferred = BK7258_BL2_PAIR_SLOT_SECONDARY;
      return true;
    }

  return false;
}
