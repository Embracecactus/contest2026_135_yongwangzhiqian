/****************************************************************************
 * chips/bk7258/include/bk7258_sdk_runtime.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal Beken SDK runtime prerequisites shared by the CP and AP images.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDK_RUNTIME_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDK_RUNTIME_H

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_sdk_runtime_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDK_RUNTIME_H */
