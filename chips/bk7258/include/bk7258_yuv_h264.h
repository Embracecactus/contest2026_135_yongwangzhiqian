/****************************************************************************
 * board/bk7258/chip/include/bk7258_yuv_h264.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP YUV-buffer/H.264 typed board helper.  The current NuttX tree
 * does not provide a generic YUV-buffer/H.264 encoder upper-half, so this
 * interface is intentionally a board media helper and does not create a
 * character device or replace the SDK media pipeline.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_YUV_H264_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_YUV_H264_H

#include <nuttx/compiler.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct bk7258_yuv_h264_s;

/* Values match the v3.1.1.9 public yuv_format_t enum.  The no-sensor H.264
 * path is proven by the SDK pipeline for packed 4:2:2 input only. */

enum bk7258_yuv_h264_format_e
{
  BK7258_YUV_H264_YUYV = 0,
  BK7258_YUV_H264_UYVY,
  BK7258_YUV_H264_YYUV,
  BK7258_YUV_H264_UVYY,
};

struct bk7258_yuv_h264_config_s
{
  uint16_t width;
  uint16_t height;
  enum bk7258_yuv_h264_format_e format;

  /* The SDK no-sensor path consumes two 16-line blocks from this
   * caller-owned scratch buffer.  Its required size is
   * width * 16 * 2 bytes per block, for two blocks total. */

  FAR uint8_t *line_cache;
  size_t line_cache_size;

  /* Width/height, input and scratch buffers must remain fixed for the
   * lifetime of the owner.  A zero timeout selects the conservative 5 s
   * default for each synchronous encode. */

  uint32_t timeout_ms;
};

struct bk7258_yuv_h264_input_s
{
  FAR const uint8_t *data;
  size_t length;
};

struct bk7258_yuv_h264_output_s
{
  FAR uint8_t *data;
  size_t capacity;
  size_t length;
};

/* The v3.1.1.9 H.264 pipeline uses a 10 KiB repeat-DMA chunk because the
 * DMA transfer-length field is limited to one hardware transaction.  An
 * output therefore must be at least 10 KiB and its capacity must be an
 * integer multiple of 10 KiB.  The wrapper checks the complete capacity
 * address range (not just the first chunk) before enabling the destination
 * address loop. */

/* Strict single-owner initialization.  The wrapper owns the configured
 * YUV/H.264 instances and one DMA channel until uninitialize.  Failed setup
 * is rolled back through the same teardown path as normal close.  If an SDK
 * cleanup operation itself fails, the wrapper retains its ownership tokens
 * and retries that teardown before accepting a later initialize request.
 * The SDK *_driver_init roots are shared and intentionally remain owned by
 * the board integration; this helper never deinitializes those global roots.
 * It cannot coexist with the SDK DVP/H.264 pipeline.  All descriptors and
 * buffers remain caller-owned.  Buffers must be contiguous, 32-bit
 * addressable, DMA-visible and cache coherent; no cache maintenance or
 * allocation is performed here. */

int bk7258_yuv_h264_initialize(
  FAR struct bk7258_yuv_h264_s **out,
  FAR const struct bk7258_yuv_h264_config_s *config);

int bk7258_yuv_h264_uninitialize(FAR struct bk7258_yuv_h264_s *priv);

/* Encode one complete packed YUV422 frame synchronously.  Initialization
 * starts one persistent YUV/H.264 stream; each call feeds 16-line blocks,
 * preserves H.264 GOP state across frames, and resets only the YUV path after
 * a successful frame.  The SDK writes the H.264 bitstream through its
 * FIFO/DMA path.  Only one call may be active; input/output storage must not
 * overlap the line cache or each other. */

int bk7258_yuv_h264_encode(
  FAR struct bk7258_yuv_h264_s *priv,
  FAR const struct bk7258_yuv_h264_input_s *input,
  FAR struct bk7258_yuv_h264_output_s *output);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_YUV_H264_H */
