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
 * accepts a bounded baseline SOF0 subset with three components and YUV444,
 * YUV422, or YUV420 sampling.  That subset has one 8-bit, nonzero DQT each
 * for table 0 and table 1; one valid baseline DHT each for DC0, DC1, AC0,
 * and AC1; Y using quantization/Huffman table 0; and Cb/Cr using table 1.
 * The scan component order must match SOF order.  The YUV400 value is
 * retained for source ABI compatibility but is never returned because the
 * hardware path rejects a one-component stream.  RGB, planar YUV, scaling,
 * rotation, progressive/multiscan JPEG, and custom table numbering are
 * outside this contract. */

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

/* The caller owns both the descriptor and the storage, and both address
 * ranges must be representable by the BK7258 32-bit interface.  Decode copies
 * compressed bytes into a grow-only, cache-line-aligned handle buffer with
 * the SDK's required zeroed read guard; successful uninitialize releases that
 * buffer.  It also performs input clean and output flush/invalidate operations
 * around the hardware transfer.  Input data is read only; decode updates input
 * width/height and output length/width/height on success.  Output is tightly
 * packed width * height * 2 byte YUYV with no configurable stride.  Its base
 * must be aligned to the active data-cache line size and its capacity must
 * cover the correspondingly rounded span.  When D-cache is disabled, as in
 * the maintained non-cacheable AP handoff, natural pointer alignment is used
 * instead.  The caller must own that complete rounded span exclusively for
 * the duration of decode.  Output length reports only the unrounded pixel
 * payload, is cleared before validation, and stays zero on every failure. */

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
 * are normal thread-context calls.  Deinitialize only after decode returns.
 * An SDK decode failure faults this handle and is returned as -EIO;
 * subsequent get_info/decode calls also return -EIO until the owner
 * uninitializes and creates a new instance.  If initialize itself fails and
 * SDK rollback cannot close/delete the unique handle, initialize returns a
 * non-NULL recovery-only handle through `out` together with its negative
 * status.  The caller must retain that handle and retry uninitialize until it
 * succeeds before creating another instance.  A later initialize also
 * returns the same recovery handle with -EIO if the original caller lost it.
 */

int bk7258_jpeg_decoder_initialize(FAR struct bk7258_jpeg_decoder_s **out);
int bk7258_jpeg_decoder_uninitialize(FAR struct bk7258_jpeg_decoder_s *priv);

int bk7258_jpeg_decoder_get_info(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_info_s *info);

/* Synchronous decode.  JPEG metadata is obtained only after the helper's
 * bounded parser has validated one SOF0/SOS scan through its terminating EOI;
 * the SDK's unbounded public get-info operation is never called.  The
 * immutable SDK also exposes an asynchronous API,
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
