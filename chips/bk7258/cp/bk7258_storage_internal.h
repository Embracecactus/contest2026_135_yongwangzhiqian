/****************************************************************************
 * chips/bk7258/cp/bk7258_storage_internal.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __CHIPS_BK7258_CP_BK7258_STORAGE_INTERNAL_H
#define __CHIPS_BK7258_CP_BK7258_STORAGE_INTERNAL_H

#include <stdint.h>

#include <arch/chip/bk7258_storage_config.h>
#include <arch/chip/bk7258_storage_guard.h>

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_storage_ota_layout(
  FAR const struct bk7258_ota_layout_s **layout);
int bk7258_storage_radio_config(
  FAR const struct bk7258_radio_storage_config_s **config);
int bk7258_storage_marker_address(FAR uint32_t *address);
int bk7258_storage_lock(enum bk7258_storage_guard_e guard,
                        uint32_t timeout_ms);
void bk7258_storage_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_CP_BK7258_STORAGE_INTERNAL_H */
