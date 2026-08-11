/****************************************************************************
 * board/bk7258/chip/include/bk7258_scale_rotate.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP hardware scale/rotate typed board helper.  The current NuttX
 * tree has no generic scale/rotate upper-half, so this interface is intended
 * for an existing framebuffer/video owner and does not create a device node.
 *
 * The implementation is limited to the capabilities proven by the immutable
 * BK7258 v3.1.1.9 public driver API.  All buffers are caller-owned, contiguous
 * and cache-coherent from the hardware's point of view.  The SDK accepts
 * 32-bit addresses; the wrapper validates address ranges before programming
 * the engines.  A single owner may use one engine at a time.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_SCALE_ROTATE_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_SCALE_ROTATE_H

#include <nuttx/compiler.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum bk7258_scale_rotate_engine_e
{
  BK7258_SCALE_ROTATE_SCALE0 = 0,
  BK7258_SCALE_ROTATE_SCALE1,
  BK7258_SCALE_ROTATE_ROTATOR,
};

enum bk7258_scale_rotate_format_e
{
  BK7258_SCALE_ROTATE_RGB565_LE = 0,
  BK7258_SCALE_ROTATE_RGB565,
  BK7258_SCALE_ROTATE_YUYV,
  BK7258_SCALE_ROTATE_UYVY,
  BK7258_SCALE_ROTATE_YYUV,
  BK7258_SCALE_ROTATE_UVYY,
  BK7258_SCALE_ROTATE_VUYY,
};

enum bk7258_scale_rotate_angle_e
{
  BK7258_SCALE_ROTATE_NONE = 0,
  BK7258_SCALE_ROTATE_90,
  BK7258_SCALE_ROTATE_180,
  BK7258_SCALE_ROTATE_270,
};

enum bk7258_scale_rotate_input_flow_e
{
  BK7258_SCALE_ROTATE_INPUT_NORMAL = 0,
  BK7258_SCALE_ROTATE_INPUT_REVERSE_BYTE,
  BK7258_SCALE_ROTATE_INPUT_REVERSE_HALFWORD,
};

enum bk7258_scale_rotate_output_flow_e
{
  BK7258_SCALE_ROTATE_OUTPUT_NORMAL = 0,
  BK7258_SCALE_ROTATE_OUTPUT_REVERSE_HALFWORD,
};

struct bk7258_scale_rotate_s;

struct bk7258_scale_request_s
{
  FAR const void *src;
  FAR void *dst;
  size_t src_size;
  size_t dst_size;
  uint16_t src_width;
  uint16_t src_height;
  uint16_t dst_width;
  uint16_t dst_height;
  enum bk7258_scale_rotate_format_e format;
  uint32_t timeout_ms;
};

struct bk7258_rotate_request_s
{
  FAR const void *src;
  FAR void *dst;
  size_t src_size;
  size_t dst_size;
  uint16_t src_width;
  uint16_t src_height;
  uint16_t block_width;
  uint16_t block_height;
  uint16_t block_count;
  /* Zero disables watermark; otherwise this must be less than block_count. */
  uint16_t watermark_block;
  enum bk7258_scale_rotate_format_e format;
  enum bk7258_scale_rotate_angle_e angle;
  enum bk7258_scale_rotate_input_flow_e input_flow;
  enum bk7258_scale_rotate_output_flow_e output_flow;
  uint32_t timeout_ms;
};

/* v3.1.1.9 implements only SCALE0.  SCALE1 remains an enum value for ABI
 * compatibility and future SDKs, but initialize(SCALE1) returns -ENOTSUP.
 *
 * A repeated initialize is -EBUSY, including when a different engine is
 * requested.  This strict single-owner rule also prevents the SDK scale
 * deinit path from resetting the other scale channel. */

int bk7258_scale_rotate_initialize(
  FAR struct bk7258_scale_rotate_s **out,
  enum bk7258_scale_rotate_engine_e engine);
int bk7258_scale_rotate_uninitialize(
  FAR struct bk7258_scale_rotate_s *priv);

/* Scale uses the SDK FRAME_SCALE path.  The SDK's implementation is proven
 * for contiguous RGB565 and YUYV frames only; RGB888 is deliberately
 * rejected because the v3.1.1.9 row-address code advances by two bytes. */

int bk7258_scale(FAR struct bk7258_scale_rotate_s *priv,
                 FAR const struct bk7258_scale_request_s *request);

/* Rotate output is always RGB565 according to the SDK API.  ROTATE_NONE is
 * the SDK's YUV-to-RGB565 mode.  ROTATE_180 is reserved by the SDK and is
 * rejected; 90/270 use the documented block-based rotator contract. */

int bk7258_rotate(FAR struct bk7258_scale_rotate_s *priv,
                  FAR const struct bk7258_rotate_request_s *request);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_SCALE_ROTATE_H */
