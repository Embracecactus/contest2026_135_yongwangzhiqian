/****************************************************************************
 * chips/bk7258/include/bk7258_slcd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 segment-LCD controller and board/glass binding contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SLCD_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SLCD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_slcd_board_s
{
  const char *name;
  uint8_t com_num;      /* 4 or 8 (slcd_com_num_t) */
  uint8_t bias;         /* slcd_bias_t */
  uint8_t rate;         /* slcd_rate_t */
  uint8_t com_enable;   /* one bit per COM */
  uint32_t seg_enable;  /* one bit per SEG */

  int (*control_pins_initialize)(const struct bk7258_slcd_board_s *board);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_SLCD) && defined(CONFIG_BK7258_AP_CORE)

/* Register /dev/slcd0 for the selected board segment-LCD glass. */

int bk7258_slcd_initialize(const struct bk7258_slcd_board_s *board);

#endif /* CONFIG_BK7258_SLCD && CONFIG_BK7258_AP_CORE */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SLCD_H */
