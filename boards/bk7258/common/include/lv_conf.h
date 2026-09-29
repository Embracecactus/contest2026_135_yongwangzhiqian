/****************************************************************************
 * boards/bk7258/common/include/lv_conf.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 overrides for LVGL configuration attributes.
 ****************************************************************************/

#ifndef LV_CONF_H
#define LV_CONF_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LVGL v9 maps CONFIG_LV_ATTRIBUTE_* string symbols into C attribute
 * positions.  NuttX emits even empty string symbols as quoted literals,
 * which are invalid in those positions.  LVGL's default for these attributes
 * is an empty token.  The board common include directory is exported to all
 * applications by both supported build backends.
 */

#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_EXTERN_DATA
#define LV_ATTRIBUTE_TICK_INC
#define LV_ATTRIBUTE_TIMER_HANDLER
#define LV_ATTRIBUTE_FLUSH_READY

#endif /* LV_CONF_H */
