/****************************************************************************
 * tests/mocks/driver/yuv_buf.h
 *
 * Host mirror of the v3.1.1.9 SDK <driver/yuv_buf.h> ABI (types from
 * yuv_buf_types.h / hal_yuv_buf_types.h).  Only the surface used by the
 * board helpers is provided.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_YUV_BUF_H
#define __MOCK_DRIVER_YUV_BUF_H

#include <stdint.h>

#include <common/bk_err.h>

#define BK_ERR_YUV_BUF_BASE            (-0x4100)
#define BK_ERR_YUV_BUF_DRIVER_NOT_INIT (BK_ERR_YUV_BUF_BASE - 1)

typedef uint8_t yuv_buf_unit_t;
typedef void (*yuv_buf_isr_t)(yuv_buf_unit_t id, void *param);

typedef enum
{
  YUV_FORMAT_YUYV = 0,
  YUV_FORMAT_UYVY,
  YUV_FORMAT_YYUV,
  YUV_FORMAT_UVYY,
} yuv_format_t;

typedef enum
{
  SYNC_LOW_LEVEL,
  SYNC_HIGH_LEVEL,
} sync_level_t;

typedef enum
{
  YUV_MCLK_DIV_4 = 0,
  YUV_MCLK_DIV_6 = 1,
  YUV_MCLK_DIV_2 = 2,
  YUV_MCLK_DIV_3 = 3,
} mclk_div_t;

typedef enum
{
  UNKNOW_MODE = 0,
  YUV_MODE,
  JPEG_MODE,
  H264_MODE,
} yuv_mode_t;

typedef struct
{
  yuv_format_t yuv_format;
  sync_level_t vsync;
  sync_level_t hsync;
} yuv_mode_cfg_t;

typedef struct
{
  yuv_mode_t work_mode;
  mclk_div_t mclk_div;
  uint32_t x_pixel;
  uint32_t y_pixel;
  yuv_mode_cfg_t yuv_mode_cfg;
  uint8_t *base_addr;
  uint8_t *emr_base_addr;
} yuv_buf_config_t;

typedef enum
{
  YUV_BUF_VSYNC_NEGEDGE = 0,
  YUV_BUF_YUV_ARV,
  YUV_BUF_SM0_WR,
  YUV_BUF_SM1_WR,
  YUV_BUF_FULL,
  YUV_BUF_ENC_LINE,
  YUV_BUF_SEN_RESL,
  YUV_BUF_H264_ERR,
  YUV_BUF_ENC_SLOW,
  YUV_BUF_ISR_MAX,
} yuv_buf_isr_type_t;

bk_err_t bk_yuv_buf_driver_init(void);
bk_err_t bk_yuv_buf_driver_deinit(void);
bk_err_t bk_yuv_buf_init(const yuv_buf_config_t *config);
bk_err_t bk_yuv_buf_deinit(void);
bk_err_t bk_yuv_buf_start(yuv_mode_t work_mode);
bk_err_t bk_yuv_buf_stop(yuv_mode_t work_mode);
bk_err_t bk_yuv_buf_rencode_start(void);
bk_err_t bk_yuv_buf_enable_nosensor_encode_mode(void);
bk_err_t bk_yuv_buf_set_em_base_addr(uint32_t em_base_addr);
bk_err_t bk_yuv_buf_register_isr(yuv_buf_isr_type_t type_id,
                                 yuv_buf_isr_t isr, void *param);
bk_err_t bk_yuv_buf_unregister_isr(yuv_buf_isr_type_t type_id);
bk_err_t bk_yuv_buf_soft_reset(void);

#endif /* __MOCK_DRIVER_YUV_BUF_H */