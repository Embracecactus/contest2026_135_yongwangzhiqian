/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/common/
 * bk7258_rptun.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP/AP NuttX RPTUN lower-half interface.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPTUN_H
#define __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPTUN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_rptun_initialize(uint32_t generation);
int bk7258_rptun_quiesce(void);

#ifdef CONFIG_BK7258_RPMSG_TEST
int bk7258_rpmsg_test_initialize(void);
#endif

#ifdef CONFIG_BK7258_RPMSGFS
int bk7258_rpmsgfs_initialize(void);
#endif

#ifdef CONFIG_BK7258_RPMSGFS_TEST
int bk7258_rpmsgfs_test_initialize(void);
#endif

#ifdef CONFIG_BK7258_BT_IPC_TEST
int bk7258_bt_test_initialize(void);
#endif

#ifdef CONFIG_BK7258_AP_SUPERVISOR
int bk7258_rpmsg_health_initialize(void);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPTUN_H */
