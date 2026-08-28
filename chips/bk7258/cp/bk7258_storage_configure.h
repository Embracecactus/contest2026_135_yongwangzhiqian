/****************************************************************************
 * chips/bk7258/cp/bk7258_storage_configure.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runner-private interface for applying board-selected storage config.
 ****************************************************************************/

#ifndef __CHIPS_BK7258_CP_BK7258_STORAGE_CONFIGURE_H
#define __CHIPS_BK7258_CP_BK7258_STORAGE_CONFIGURE_H

#include <nuttx/compiler.h>

#include <arch/chip/bk7258_storage_config.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Copy and publish the board-selected topology exactly once during CP
 * bring-up.  A later call returns -EALREADY; no board pointer is retained.
 */

int bk7258_storage_configure(
  FAR const struct bk7258_storage_config_s *config);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_CP_BK7258_STORAGE_CONFIGURE_H */
