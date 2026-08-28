/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/bk7258/mocks/driver/rott_driver.h
 *
 * Host stand-in for the immutable v3.1.1.9 rotator driver API surface used
 * by bk7258_scale_rotate.c (driver/rott_driver.h).  Types and names mirror
 * the SDK bundle; implementation lives in framework/mock_sdk_scale_rotate.c.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_ROTT_DRIVER_H
#define __MOCK_DRIVER_ROTT_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_err.h>
#include <driver/hw_scale.h>

typedef enum
{
  ROTATE_NONE = 0,
  ROTATE_90 = 1,
  ROTATE_270 = 2,
  ROTATE_180 = 3,
} media_rotate_t;

typedef enum
{
  ROTT_INPUT_NORMAL = 0,
  ROTT_INPUT_REVESE_BYTE_BY_BYTE = 1,
  ROTT_INPUT_REVESE_HALFWORD_BY_HALFWORD = 2,
} rott_input_data_flow_t;

typedef enum
{
  ROTT_OUTPUT_NORMAL = 0,
  ROTT_OUTPUT_REVESE_HALFWORD_BY_HALFWORD = 1,
} rott_output_data_flow_t;

typedef enum
{
  ROTATE_COMPLETE_INT = 0,
  ROTATE_CFG_ERR_INT = 1,
} rott_int_type_t;

typedef void (*rott_int_callback_t)(void);

typedef struct
{
  media_rotate_t rot_mode;
  void *input_addr;
  void *output_addr;
  pixel_format_t input_fmt;
  rott_input_data_flow_t input_flow;
  rott_output_data_flow_t output_flow;
  uint16_t picture_xpixel;
  uint16_t picture_ypixel;
  uint16_t block_xpixel;
  uint16_t block_ypixel;
  uint16_t block_cnt;
  uint16_t watermark_blk;
} rott_config_t;

bk_err_t bk_rott_driver_init(void);
bk_err_t bk_rott_driver_deinit(void);
bk_err_t bk_rott_soft_reset(void);
bk_err_t bk_rott_isr_register(rott_int_type_t type, rott_int_callback_t fn);
bk_err_t bk_rott_int_enable(uint32_t mask, bool enable);
bk_err_t bk_rott_data_reverse(rott_input_data_flow_t input_flow,
                              rott_output_data_flow_t output_flow);
bk_err_t bk_rott_enable(void);
bk_err_t rott_config(rott_config_t *config);

#endif /* __MOCK_DRIVER_ROTT_DRIVER_H */
