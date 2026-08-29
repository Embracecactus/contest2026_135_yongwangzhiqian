/****************************************************************************
 * boards/bk7258/t5_board/src/bk7258_t5_board_camera_i2c.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARDS_BK7258_T5_BOARD_SRC_BK7258_T5_BOARD_CAMERA_I2C_H
#define __BOARDS_BK7258_T5_BOARD_SRC_BK7258_T5_BOARD_CAMERA_I2C_H

#include <nuttx/compiler.h>

struct bk7258_dvp_i2c_ops_s;

extern const struct bk7258_dvp_i2c_ops_s g_bk7258_t5_camera_i2c_ops;

int bk7258_t5_camera_prepare(FAR void *arg);
void bk7258_t5_camera_mclk_started(FAR void *arg);

#endif /* __BOARDS_BK7258_T5_BOARD_SRC_BK7258_T5_BOARD_CAMERA_I2C_H */
