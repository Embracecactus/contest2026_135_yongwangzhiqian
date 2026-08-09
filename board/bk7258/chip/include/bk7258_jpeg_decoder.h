/****************************************************************************
 * board/bk7258/chip/include/bk7258_jpeg_decoder.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP JPEG decoder board helper.  NuttX has a generic V4L2 M2M
 * upper-half, but it has no JPEG decoder lower-half matching the BK7258
 * SDK's frame_buffer_t contract.  This interface is therefore a typed
 * board integration helper, not a character-device ABI.  A board media
 * owner may adapt it to V4L2 queue operations.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_JPEG_DECODER_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_JPEG_DECODER_H

#include <nuttx/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct bk7258_jpeg_decoder_s;

/* The v3.1.1.9 hardware decoder emits interleaved YUYV only.  The helper
 * intentionally does not advertise RGB, planar YUV, scaling, or rotation;
 * those are not part of the public hardware-decoder contract used here. */

enum bk7258_jpeg_decoder_format_e
{
  BK7258_JPEG_DECODER_YUV444 = 1,
  BK7258_JPEG_DECODER_YUV422,
  BK7258_JPEG_DECODER_YUV420,
  BK7258_JPEG_DECODER_YUV400,
};

struct bk7258_jpeg_decoder_info_s
{
  uint32_t width;
  uint32_t height;
  enum bk7258_jpeg_decoder_format_e format;
};

/* The caller owns both the descriptor and the storage.  The storage must be
 * DMA-accessible and cache coherent for the BK7258 JPEG engine.  The helper
 * performs no allocation or cache maintenance.  Input data is read only;
 * decode updates input width/height, and output length/width/height on
 * success.  Output is width * height * 2 bytes of YUYV. */

struct bk7258_jpeg_decoder_frame_s
{
  FAR uint8_t *data;
  uint32_t capacity;
  uint32_t length;
  uint32_t width;
  uint32_t height;
};

/* The SDK owns a global JPEG hardware task and engine.  This wrapper has one
 * strict owner: a repeated initialize returns -EBUSY, and only the exact
 * returned handle may be uninitialized.  Initialize/uninitialize and decode
 * are normal thread-context calls.  Deinitialize only after decode returns. */

int bk7258_jpeg_decoder_initialize(FAR struct bk7258_jpeg_decoder_s **out);
int bk7258_jpeg_decoder_uninitialize(FAR struct bk7258_jpeg_decoder_s *priv);

int bk7258_jpeg_decoder_get_info(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_info_s *info);

/* Synchronous decode.  The immutable SDK also exposes an asynchronous API,
 * but it retains the input frame pointer, has no public cancellation API, and
 * completes through callbacks from its private decoder task.  It is not
 * exposed here, so caller buffer lifetime and stop/error ownership remain
 * explicit.  SDK's fixed 200 ms hardware timeout is propagated as its public
 * hardware-error status and mapped to -EIO; this helper does not invent a
 * caller timeout or an unverified -ETIMEDOUT result. */

int bk7258_jpeg_decoder_decode(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_frame_s *output);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_JPEG_DECODER_H */
