/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __BK7258_BL2_PAIR_POLICY_H
#define __BK7258_BL2_PAIR_POLICY_H

#include <stdbool.h>

#include <bootutil/image.h>

enum bk7258_bl2_pair_slot_e
{
  BK7258_BL2_PAIR_SLOT_NONE = -1,
  BK7258_BL2_PAIR_SLOT_PRIMARY = 0,
  BK7258_BL2_PAIR_SLOT_SECONDARY = 1
};

struct bk7258_bl2_pair_candidate_s
{
  bool usable;
  struct image_version version;
};

struct bk7258_bl2_pair_order_s
{
  int preferred;
  int fallback;
};

/* Order complete CP/AP pairs without interpreting or replacing MCUboot's
 * per-image trailer state.  The preferred pair is attempted in isolation so
 * upstream direct-XIP code cannot select or mutate CP and AP from different
 * physical slots.  Equal versions retain the factory-primary preference. */

bool bk7258_bl2_pair_order(
  const struct bk7258_bl2_pair_candidate_s candidates[2],
  struct bk7258_bl2_pair_order_s *order);

#endif /* __BK7258_BL2_PAIR_POLICY_H */
