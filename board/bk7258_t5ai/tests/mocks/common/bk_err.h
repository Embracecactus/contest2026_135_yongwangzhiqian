/****************************************************************************
 * tests/mocks/common/bk_err.h
 *
 * Minimal Beken SDK error-code shim.  Only BK_OK / BK_ERR_BUSY matter to the
 * implementation: BK_ERR_BUSY is mapped to -EAGAIN by bk7258_rptun_mbox_send.
 ****************************************************************************/

#ifndef __MOCK_COMMON_BK_ERR_H
#define __MOCK_COMMON_BK_ERR_H

#include <stdint.h>

typedef int bk_err_t;

#define BK_OK        0
#define BK_ERR_BUSY  1
#define BK_ERR_PARAM 2
#define BK_ERR_TIMEOUT 3

#endif /* __MOCK_COMMON_BK_ERR_H */
