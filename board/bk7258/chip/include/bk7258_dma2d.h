/****************************************************************************
 * board/bk7258/chip/include/bk7258_dma2d.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP DMA2D board helper.  NuttX has no generic DMA2D upper-half, so
 * this is a typed board integration interface rather than a character ABI.
 * The implementation uses only the immutable BK7258 v3.1.1.9 public DMA2D
 * driver API.  It provides synchronous fill, copy/pixel-conversion and blend
 * operations; hardware scale/rotate are deliberately outside this helper.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_DMA2D_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_DMA2D_H

#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct bk7258_dma2d_s;

/* These values are intentionally board-owned.  The source file maps them to
 * the v3.1.1.9 SDK enums instead of exposing SDK headers through the board
 * integration contract. */

enum bk7258_dma2d_format_e
{
  BK7258_DMA2D_ARGB8888 = 0,
  BK7258_DMA2D_RGB888,
  BK7258_DMA2D_RGB565,
  BK7258_DMA2D_ARGB1555,
  BK7258_DMA2D_ARGB4444,
  BK7258_DMA2D_YUYV,
  BK7258_DMA2D_UYVY,
  BK7258_DMA2D_YYUV,
  BK7258_DMA2D_UVYY,
  BK7258_DMA2D_VUYY,
};

enum bk7258_dma2d_alpha_e
{
  BK7258_DMA2D_ALPHA_KEEP = 0,
  BK7258_DMA2D_ALPHA_REPLACE,
  BK7258_DMA2D_ALPHA_COMBINE,
};

enum bk7258_dma2d_swap_e
{
  BK7258_DMA2D_SWAP_REGULAR = 0,
  BK7258_DMA2D_SWAP_RED_BLUE,
};

enum bk7258_dma2d_reverse_e
{
  BK7258_DMA2D_REVERSE_NONE = 0,
  BK7258_DMA2D_REVERSE_BYTE,
  BK7258_DMA2D_REVERSE_HALFWORD,
};

/* All dimensions and positions use the SDK's 16-bit fields.  The helper
 * validates the frame/rectangle bounds and rejects unsupported formats. */

struct bk7258_dma2d_copy_s
{
  FAR const void *src;
  FAR void *dst;

  uint16_t src_frame_width;
  uint16_t src_frame_height;
  uint16_t src_x;
  uint16_t src_y;

  uint16_t dst_frame_width;
  uint16_t dst_frame_height;
  uint16_t dst_x;
  uint16_t dst_y;

  uint16_t width;
  uint16_t height;

  enum bk7258_dma2d_format_e src_format;
  enum bk7258_dma2d_format_e dst_format;
  enum bk7258_dma2d_swap_e src_swap;
  enum bk7258_dma2d_swap_e dst_swap;
  enum bk7258_dma2d_reverse_e src_reverse;
  enum bk7258_dma2d_reverse_e dst_reverse;

  uint8_t input_alpha;
  uint8_t output_alpha;
  uint32_t timeout_ms;
};

struct bk7258_dma2d_fill_s
{
  FAR void *dst;
  uint16_t frame_width;
  uint16_t frame_height;
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
  enum bk7258_dma2d_format_e format;
  uint32_t color;
  uint32_t timeout_ms;
};

struct bk7258_dma2d_blend_s
{
  FAR const void *foreground;
  FAR const void *background;
  FAR void *dst;

  uint16_t foreground_frame_width;
  uint16_t foreground_frame_height;
  uint16_t foreground_x;
  uint16_t foreground_y;

  uint16_t background_frame_width;
  uint16_t background_frame_height;
  uint16_t background_x;
  uint16_t background_y;

  uint16_t dst_frame_width;
  uint16_t dst_frame_height;
  uint16_t dst_x;
  uint16_t dst_y;

  uint16_t width;
  uint16_t height;

  enum bk7258_dma2d_format_e foreground_format;
  enum bk7258_dma2d_format_e background_format;
  enum bk7258_dma2d_format_e dst_format;
  enum bk7258_dma2d_alpha_e foreground_alpha_mode;
  enum bk7258_dma2d_alpha_e background_alpha_mode;
  uint8_t foreground_alpha;
  uint8_t background_alpha;
  enum bk7258_dma2d_swap_e foreground_swap;
  enum bk7258_dma2d_swap_e background_swap;
  enum bk7258_dma2d_swap_e dst_swap;
  enum bk7258_dma2d_reverse_e foreground_reverse;
  enum bk7258_dma2d_reverse_e output_reverse;
  uint32_t timeout_ms;
};

/* The DMA2D engine is a singleton in the SDK and this wrapper has one strict
 * owner.  A repeated initialize returns -EBUSY; only the owner may call
 * uninitialize.  Deinitialization must be performed in normal thread context
 * after all operations have returned. */

int bk7258_dma2d_initialize(FAR struct bk7258_dma2d_s **out);
int bk7258_dma2d_uninitialize(FAR struct bk7258_dma2d_s *priv);

/* The caller owns all buffers.  They must remain valid and DMA-accessible and
 * must not overlap in a way the SDK DMA2D operation cannot safely handle.
 * The helper performs the required cache maintenance over each operation's
 * active source and destination rectangles; it does not allocate or copy
 * caller storage. */

int bk7258_dma2d_copy(FAR struct bk7258_dma2d_s *priv,
                      FAR const struct bk7258_dma2d_copy_s *copy);
int bk7258_dma2d_fill(FAR struct bk7258_dma2d_s *priv,
                      FAR const struct bk7258_dma2d_fill_s *fill);
int bk7258_dma2d_blend(FAR struct bk7258_dma2d_s *priv,
                       FAR const struct bk7258_dma2d_blend_s *blend);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_DMA2D_H */
