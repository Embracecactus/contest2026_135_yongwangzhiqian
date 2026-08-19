/****************************************************************************
 * tests/bk7258/mocks/driver/hw_scale.h
 *
 * Host stand-in for the immutable v3.1.1.9 hardware scale driver API
 * surface used by bk7258_scale_rotate.c (driver/hw_scale.h).  Types and
 * names mirror the SDK bundle; implementation lives in
 * framework/mock_sdk_scale_rotate.c.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_HW_SCALE_H
#define __MOCK_DRIVER_HW_SCALE_H

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_err.h>

typedef enum
{
  HW_SCALE0 = 0,
  HW_SCALE1 = 1,
} scale_id_t;

typedef enum
{
  PIXEL_FMT_RGB565 = 0,
  PIXEL_FMT_RGB565_LE,
  PIXEL_FMT_YUYV,
  PIXEL_FMT_UYVY,
  PIXEL_FMT_YYUV,
  PIXEL_FMT_UVYY,
  PIXEL_FMT_VUYY,
  PIXEL_FMT_MAX,
} pixel_format_t;

typedef enum
{
  FRAME_SCALE = 0,
  BLOCK_SCALE = 1,
} scale_mode_t;

typedef struct
{
  uint8_t line_cycle;
  uint8_t line_mask;
  uint16_t src_width;
  uint16_t src_height;
  uint16_t dst_width;
  uint16_t dst_height;
  scale_mode_t scale_mode;
  pixel_format_t pixel_fmt;
  uint8_t *src_addr;
  uint8_t *dst_addr;
} scale_drv_config_t;

bk_err_t bk_hw_scale_driver_init(scale_id_t id);
bk_err_t bk_hw_scale_driver_deinit(scale_id_t id);
bk_err_t bk_hw_scale_isr_register(scale_id_t id, void (*fn)(void *),
                                  void *arg);
bk_err_t bk_hw_scale_isr_unregister(scale_id_t id);
bk_err_t bk_hw_scale_int_enable(scale_id_t id, bool enable);
bk_err_t bk_hw_scale_mem_free(scale_id_t id);
bk_err_t bk_hw_scale_stop(scale_id_t id);
bk_err_t hw_scale_frame(scale_id_t id, scale_drv_config_t *config);

#endif /* __MOCK_DRIVER_HW_SCALE_H */