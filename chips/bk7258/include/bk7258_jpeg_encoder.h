/****************************************************************************
 * chips/bk7258/include/bk7258_jpeg_encoder.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP JPEG encoder typed board helper.  NuttX exposes a generic
 * V4L2 M2M codec contract, but the v3.1.1.9 immutable bundle has no exported
 * JPEG-encoder controller object.  This interface therefore does not create
 * a character device; a board media owner may adapt this synchronous helper
 * to its V4L2 output queue.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_ENCODER_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_ENCODER_H

#include <nuttx/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct bk7258_jpeg_encoder_s;

/* These values match the v3.1.1.9 public yuv_format_t enum.  The encoder
 * source and examples prove YUYV, UYVY, YYUV and UVYY; no other input format
 * is advertised by this helper. */

enum bk7258_jpeg_encoder_format_e
{
  BK7258_JPEG_ENCODER_YUYV = 0,
  BK7258_JPEG_ENCODER_UYVY,
  BK7258_JPEG_ENCODER_YYUV,
  BK7258_JPEG_ENCODER_UVYY,
};

struct bk7258_jpeg_encoder_config_s
{
  uint16_t width;
  uint16_t height;
  enum bk7258_jpeg_encoder_format_e format;

  /* The SDK's no-sensor YUV path reads two eight-line blocks from this
   * caller-owned DMA-visible cache and replenishes them through JPEG ISR
   * callbacks.  It must be at least width * 8 * 2 * 2 bytes. */

  FAR uint8_t *line_cache;
  uint32_t line_cache_size;
};

struct bk7258_jpeg_encoder_input_s
{
  FAR const uint8_t *data;
  uint32_t length;
};

struct bk7258_jpeg_encoder_output_s
{
  FAR uint8_t *data;
  uint32_t capacity;
  uint32_t length;
};

/* The caller owns all descriptors and storage.  Input and output storage
 * must be DMA-accessible and cache coherent for the BK7258.  The wrapper
 * performs no allocation or cache maintenance.  The output DMA uses a
 * bounded circular destination, then the hardware frame-size register is
 * checked against capacity; callers must provide enough capacity for the
 * encoded frame or receive -ENOSPC. */

int bk7258_jpeg_encoder_initialize(
  FAR struct bk7258_jpeg_encoder_s **out,
  FAR const struct bk7258_jpeg_encoder_config_s *config);

int bk7258_jpeg_encoder_uninitialize(
  FAR struct bk7258_jpeg_encoder_s *priv);

/* Configure the SDK's size-based compression control.  Both limits must be
 * non-zero, min < max, and no greater than the SDK's proven 16-bit limit. */

int bk7258_jpeg_encoder_set_compression(
  FAR struct bk7258_jpeg_encoder_s *priv,
  uint32_t min_size,
  uint32_t max_size);

/* Synchronous one-frame encode.  Only one call may be active on the strict
 * singleton owner.  The input is read for the duration of this call; output
 * length is written only after a completed hardware frame. */

int bk7258_jpeg_encoder_encode(
  FAR struct bk7258_jpeg_encoder_s *priv,
  FAR const struct bk7258_jpeg_encoder_input_s *input,
  FAR struct bk7258_jpeg_encoder_output_s *output);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_ENCODER_H */
