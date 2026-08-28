/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __CHIPS_BK7258_CP_BK7258_OTA_ENGINE_INTERNAL_H
#define __CHIPS_BK7258_CP_BK7258_OTA_ENGINE_INTERNAL_H

#include <arch/chip/bk7258_storage_config.h>

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_ota_resolve_layout(
  FAR const struct bk7258_ota_layout_s **layout,
  FAR enum bk7258_boot_slot_e *active_slot);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_CP_BK7258_OTA_ENGINE_INTERNAL_H */
