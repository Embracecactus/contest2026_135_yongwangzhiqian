/****************************************************************************
 * contest2026_135_yongwangzhiqian/boards/bk7258/common/src/bk7258_internal.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal interfaces between the BK7258 board initialization layers.
 ****************************************************************************/

#ifndef __BOARD_BK7258_SRC_BK7258_INTERNAL_H
#define __BOARD_BK7258_SRC_BK7258_INTERNAL_H

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_bringup(void);
#ifndef CONFIG_BK7258_AP_CORE
int bk7258_cp_bringup_initialize(void);
int bk7258_cp_bringup_result(void);
#endif
#if !defined(CONFIG_BK7258_AP_CORE) && \
    (defined(CONFIG_BK7258_OTA) || \
     defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC) || \
     defined(CONFIG_BK7258_FLASH_MTD) || \
     defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET))
struct bk7258_storage_config_s;
extern const struct bk7258_storage_config_s
  g_bk7258_board_storage_config;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_SRC_BK7258_INTERNAL_H */
