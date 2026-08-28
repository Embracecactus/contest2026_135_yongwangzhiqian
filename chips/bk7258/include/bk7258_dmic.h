/****************************************************************************
 * chips/bk7258/include/bk7258_dmic.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 audio DMIC helper and board binding contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DMIC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DMIC_H

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

struct bk7258_dmic_board_s
{
  const char *name;
  uint8_t channel;      /* 0 = left, 1 = right */
  uint32_t sample_rate; /* 8000..48000 */

  int (*control_pins_initialize)(const struct bk7258_dmic_board_s *board);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_DMIC) && defined(CONFIG_BK7258_AP_CORE)

int bk7258_dmic_initialize(
  const struct bk7258_dmic_board_s *board);
int bk7258_dmic_start(void);
int bk7258_dmic_stop(void);
int bk7258_dmic_read_fifo(uint32_t *sample);
int bk7258_dmic_uninitialize(void);

#endif /* CONFIG_BK7258_DMIC && CONFIG_BK7258_AP_CORE */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DMIC_H */
