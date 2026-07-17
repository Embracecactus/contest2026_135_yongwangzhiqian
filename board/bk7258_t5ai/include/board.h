/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal board header for the Beken BK7258 (T5-AI) Stage N1 port.
 * NuttX's configure step exposes this via <arch/board/board.h>.
 ****************************************************************************/

#ifndef __ARCH_BOARD_BK7258_T5AI_BOARD_H
#define __ARCH_BOARD_BK7258_T5AI_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LEDs / buttons: not modelled at N1. */

#define BOARD_NLEDS       0
#define BOARD_NBUTTONS    0

/* Physical memory layout (informational; the authoritative copy is in
 * scripts/ld.script).
 */

#define BOARD_FLASH_ADDR  0x02010000u
#define BOARD_FLASH_SIZE  0x00010000u
#define BOARD_RAM_ADDR    0x28000000u
#define BOARD_RAM_SIZE    0x000a0000u

#endif /* __ARCH_BOARD_BK7258_T5AI_BOARD_H */
