/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/components/avdk_utils/avdk_error.h
 *
 * Minimal shim for the v3.1.1.9 avdk_error.h; values mirror the SDK header.
 ****************************************************************************/

#ifndef __MOCK_COMPONENTS_AVDK_UTILS_AVDK_ERROR_H
#define __MOCK_COMPONENTS_AVDK_UTILS_AVDK_ERROR_H

typedef int avdk_err_t;

#define AVDK_ERR_OK              0
#define AVDK_ERR_GENERIC        -1
#define AVDK_ERR_INVAL          -2
#define AVDK_ERR_NOMEM          -3
#define AVDK_ERR_BUSY           -4
#define AVDK_ERR_NODEV          -5
#define AVDK_ERR_TIMEOUT        -6
#define AVDK_ERR_HWERROR        -7
#define AVDK_ERR_RDYDONE        -8
#define AVDK_ERR_SHUTDOWN       -9
#define AVDK_ERR_UNKNOWN        -10
#define AVDK_ERR_UNSUPPORTED    -11
#define AVDK_ERR_NO_RESOURCE    -12
#define AVDK_ERR_IO             -13

#endif /* __MOCK_COMPONENTS_AVDK_UTILS_AVDK_ERROR_H */
