/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_internal.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal interfaces between the BK7258 board initialization layers.
 ****************************************************************************/

#ifndef __BOARD_BK7258_SRC_BK7258_INTERNAL_H
#define __BOARD_BK7258_SRC_BK7258_INTERNAL_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_platform_initialize(void);
int bk7258_bringup(void);
#if !defined(CONFIG_BK7258_AP_CORE) && \
    (defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET))
int bk7258_mac_storage_initialize(void);
#endif

#endif /* __BOARD_BK7258_SRC_BK7258_INTERNAL_H */
