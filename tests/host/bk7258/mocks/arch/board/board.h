/****************************************************************************
 * tests/host/bk7258/mocks/arch/board/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __TESTS_BK7258_MOCKS_ARCH_BOARD_BOARD_H
#define __TESTS_BK7258_MOCKS_ARCH_BOARD_BOARD_H

#include <arch/chip/bk7258_gpio.h>

extern const struct bk7258_gpio_config_s g_bk7258_board_gpio_config;

#ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM
int bk7258_ota_trial_initialize(void);
#endif

#ifdef CONFIG_BK7258_TOUCH
int bk7258_board_cp_devices_initialize(void);
#endif

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_board_ap_initialize(void);
#ifdef CONFIG_BK7258_BOARD_DEFERRED_INIT
int bk7258_board_ap_deferred_initialize(void);
#endif
#endif

#endif /* __TESTS_BK7258_MOCKS_ARCH_BOARD_BOARD_H */
