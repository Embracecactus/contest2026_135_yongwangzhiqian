/****************************************************************************
 * chips/bk7258/include/bk7258_identity.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_IDENTITY_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_IDENTITY_H

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_IDENTITY_BYTES 32u

/* Read the SDK-derived, SHA-256 device identity.  The CP owns the UID/OTP
 * access and caches the immutable result; this interface exposes no OTP or
 * eFuse write operation.
 */

#if defined(CONFIG_BK7258_IDENTITY) && !defined(CONFIG_BK7258_AP_CORE)
int bk7258_identity_read(FAR uint8_t identity[BK7258_IDENTITY_BYTES]);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_IDENTITY_H */
