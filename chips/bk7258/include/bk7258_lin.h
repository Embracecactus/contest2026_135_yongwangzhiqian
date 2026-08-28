/****************************************************************************
 * chips/bk7258/include/bk7258_lin.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 LIN lower-half and board/controller binding contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LIN_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LIN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_lin_mode_e
{
  BK7258_LIN_MODE_SLAVE = 0,
  BK7258_LIN_MODE_MASTER = 1
};

enum bk7258_lin_checksum_e
{
  BK7258_LIN_CHECKSUM_CLASSIC = 0,
  BK7258_LIN_CHECKSUM_ENHANCED = 1
};

/* One LIN node binding.  The SDK channel/GPIO mapping is board wiring and
 * remains opaque here; the chip lower half casts the channel value through
 * the SDK lin_channel_t in its own translation unit.
 */

struct bk7258_lin_board_s
{
  const char *name;
  uint8_t channel;             /* SDK lin_channel_t */
  uint8_t mode;                /* enum bk7258_lin_mode_e */
  uint8_t checksum;            /* enum bk7258_lin_checksum_e */
  uint8_t data_length;         /* 1..8 */
  double rate;                 /* master bit rate, 1k..20k */
  int (*control_pins_initialize)(const struct bk7258_lin_board_s *board);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_LIN) && defined(CONFIG_BK7258_AP_CORE)

/* Register /dev/lin0 for the selected board LIN node. */

int bk7258_lin_initialize(const struct bk7258_lin_board_s *board);

#endif /* CONFIG_BK7258_LIN && CONFIG_BK7258_AP_CORE */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LIN_H */
