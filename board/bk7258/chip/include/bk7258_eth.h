/****************************************************************************
 * board/bk7258/chip/include/bk7258_eth.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 Ethernet MAC (netdev) contract.
 *
 * The v3.1.1.9 AP SDK exports a complete STM32H7-compatible HAL Ethernet
 * driver (HAL_ETH_*, LAN8742 PHY) compiled with CONFIG_ETH=y.  This board
 * wrapper adapts it to the native NuttX netdev interface.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_ETH_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_ETH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Board binding for the Ethernet PHY pin group.  The SDK supports two
 * mutually exclusive groups: GROUP0 (pins 27,29-39, conflicts with DVP)
 * and GROUP1 (pins 46-55, conflicts with LCD).
 */

enum bk7258_eth_pin_group_e
{
  BK7258_ETH_PIN_GROUP0 = 0,
  BK7258_ETH_PIN_GROUP1 = 1
};

struct bk7258_eth_board_s
{
  const char *name;
  uint8_t pin_group;                /* enum bk7258_eth_pin_group_e */
  uint8_t mac_addr[6];              /* station MAC address */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_ETH) && defined(CONFIG_BK7258_AP_CORE)

/* Implemented by the selected physical board. */

FAR const struct bk7258_eth_board_s *bk7258_board_eth_config(void);

/* Register the ethernet netdev (eth0). */

int bk7258_eth_initialize(void);

#endif /* CONFIG_BK7258_ETH && CONFIG_BK7258_AP_CORE */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_ETH_H */
