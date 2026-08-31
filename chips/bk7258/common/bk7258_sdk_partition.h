/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * chips/bk7258/common/bk7258_sdk_partition.h
 *
 * Private translation between generated layout rows and pinned SDK semantic
 * partition identifiers.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_COMMON_BK7258_SDK_PARTITION_H
#define __ARCH_ARM_SRC_BK7258_COMMON_BK7258_SDK_PARTITION_H

#include <stdint.h>

int bk7258_sdk_partition_from_layout(uint32_t layout_partition,
                                     uint32_t *sdk_partition);

#endif /* __ARCH_ARM_SRC_BK7258_COMMON_BK7258_SDK_PARTITION_H */
