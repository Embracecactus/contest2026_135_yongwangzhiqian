/****************************************************************************
 * chips/bk7258/common/bk7258_reset_marker_internal.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Chip-internal retained reset-marker contract shared by the raw reset-cause
 * decoder and its CP implementation.
 ****************************************************************************/

#ifndef __CHIPS_BK7258_COMMON_BK7258_RESET_MARKER_INTERNAL_H
#define __CHIPS_BK7258_COMMON_BK7258_RESET_MARKER_INTERNAL_H

#include <stdint.h>

#include <arch/chip/bk7258_reset_cause.h>

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_reset_marker_capture_previous(void);
int bk7258_reset_marker_previous(FAR uint32_t *reason);
int bk7258_reset_marker_stamp(enum bk7258_reset_source_e reason);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_COMMON_BK7258_RESET_MARKER_INTERNAL_H */
