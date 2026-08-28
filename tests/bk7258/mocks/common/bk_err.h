/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/common/bk_err.h
 *
 * Minimal Beken SDK error-code shim matching the v3.1.1.9 AP bundle values.
 ****************************************************************************/

#ifndef __MOCK_COMMON_BK_ERR_H
#define __MOCK_COMMON_BK_ERR_H

#include <stdint.h>

typedef int bk_err_t;

#define BK_OK                      0
#define BK_ERR_COMMON_BASE         (-0x1000)
#define BK_ERR_NOT_INIT            (BK_ERR_COMMON_BASE)
#define BK_ERR_PARAM               (BK_ERR_COMMON_BASE - 1)
#define BK_ERR_IN_PROGRESS         (BK_ERR_COMMON_BASE - 4)
#define BK_ERR_NO_MEM              (BK_ERR_COMMON_BASE - 5)
#define BK_ERR_TIMEOUT             (BK_ERR_COMMON_BASE - 6)
#define BK_ERR_NOT_FOUND           (BK_ERR_COMMON_BASE - 7)
#define BK_ERR_TRY_AGAIN           (BK_ERR_COMMON_BASE - 8)
#define BK_ERR_NULL_PARAM          (BK_ERR_COMMON_BASE - 9)
#define BK_ERR_NOT_SUPPORT         (BK_ERR_COMMON_BASE - 10)
#define BK_ERR_BUSY                (BK_ERR_COMMON_BASE - 11)
#define BK_ERR_NO_DEV              (BK_ERR_COMMON_BASE - 15)
#define BK_ERR_SHUT_DOWN           (BK_ERR_COMMON_BASE - 16)

/* v3.1.1.9 driver-bundle codes used by the AP board helpers.  Exact values
 * follow the SDK's driver error ranges; identity is what the wrappers'
 * switch statements rely on. */
#define BK_ERR_HW_SCALE_NOT_INIT   (BK_ERR_COMMON_BASE - 0x2000)
#define BK_ERR_ROTT_NOT_INIT       (BK_ERR_COMMON_BASE - 0x3000)
#define BK_ERR_CAN_BASE            (-0x4900)

#endif /* __MOCK_COMMON_BK_ERR_H */
