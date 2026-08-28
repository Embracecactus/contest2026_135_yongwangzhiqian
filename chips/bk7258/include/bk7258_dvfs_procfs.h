/****************************************************************************
 * chips/bk7258/include/bk7258_dvfs_procfs.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Public registration hook for the chip-owned BK7258 DVFS procfs node.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DVFS_PROCFS_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DVFS_PROCFS_H

#include <nuttx/config.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
int bk7258_dvfs_procfs_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DVFS_PROCFS_H */
