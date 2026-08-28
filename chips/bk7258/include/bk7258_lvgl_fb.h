/****************************************************************************
 * chips/bk7258/include/bk7258_lvgl_fb.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 accelerated LVGL framebuffer display.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LVGL_FB_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LVGL_FB_H

#include <nuttx/compiler.h>

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Create one RGB565 LVGL display backed by an FB_SYNC framebuffer.  LVGL
 * renders into an internal-SRAM partial buffer; the configured logical
 * canvas is aspect-fitted to the physical panel and DMA2D owns transfers to
 * the two PSRAM scanout pages. */

FAR lv_display_t *bk7258_lvgl_fb_create(FAR const char *path);

/* Bind a pointer input created by LVGL's standard NuttX touchscreen driver.
 * Physical panel coordinates are converted to this display's logical canvas
 * without changing the touchscreen lower half or the LVGL application. */

int bk7258_lvgl_fb_bind_touch(FAR lv_display_t *display,
                              FAR lv_indev_t *indev);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_LVGL_FB_H */
