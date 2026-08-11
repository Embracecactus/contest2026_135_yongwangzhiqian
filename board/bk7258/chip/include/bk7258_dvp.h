/****************************************************************************
 * board/bk7258/chip/include/bk7258_dvp.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP DVP image-data lower-half.  The v3.1.1.9 DVP API owns the
 * sensor discovery and hardware stream.  This wrapper exposes only the
 * standard NuttX imgdata object; a board sensor lower-half must be paired
 * with it when calling capture_register().
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_DVP_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_DVP_H

#include <nuttx/compiler.h>

#include <stdint.h>

#include <components/dvp_camera_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct imgdata_s;
struct bk7258_dvp_s;

/* A caller-owned, DMA-capable frame backing store.  The wrapper never
 * allocates frame memory from the SDK callback because that callback can be
 * entered from a DVP interrupt.  Each store must remain valid until
 * bk7258_dvp_uninitialize() returns. */

struct bk7258_dvp_frame_mem_s
{
  FAR uint8_t *addr;
  uint32_t size;
};

/* The SDK's encode_buffer has no size parameter in bk_dvp_open().  The size
 * is retained here for board validation/documentation; the board remains
 * responsible for supplying a buffer large enough for its immutable SDK
 * configuration (two 8-line JPEG banks or two 16-line H.264 banks). */

struct bk7258_dvp_config_s
{
  bk_dvp_config_t sdk;
  FAR struct bk7258_dvp_frame_mem_s *frames;
  uint8_t frame_count;
  FAR uint8_t *encode_buffer;
  uint32_t encode_buffer_size;
};

/* Configure the singleton AP DVP instance.  At least two frame stores are
 * required so the SDK can hold the current frame while requesting the next
 * one.  This wrapper currently exposes only IMAGE_YUV and IMAGE_MJPEG as
 * NuttX imgdata formats; combined YUV/encode and H.264 modes return
 * -ENOTSUP.  Repeating an identical configuration is idempotent; a
 * conflicting configuration returns -EBUSY.  IMAGE_H264 is exposed through
 * the standard V4L2 H.264 fourcc.  The pinned NuttX imgdata bridge lacks an
 * internal H.264 token, so the implementation contains a private, instance-
 * scoped compatibility mapping without changing NuttX or its public ABI. */

int bk7258_dvp_initialize(FAR const struct bk7258_dvp_config_s *config,
                          FAR struct bk7258_dvp_s **out);

/* Return the standard NuttX image-data lower-half used by capture_register.
 * No private character-device ABI is introduced. */

FAR struct imgdata_s *bk7258_dvp_get_imgdata(FAR struct bk7258_dvp_s *priv);

/* The object must be stopped and closed by the NuttX video owner before this
 * call.  It releases the wrapper's state but does not free caller-owned
 * frame or encode memory. */

int bk7258_dvp_uninitialize(FAR struct bk7258_dvp_s *priv);

/* Board PM integration helpers.  They map directly to the SDK suspend and
 * resume operations and must be called in normal thread context. */

int bk7258_dvp_suspend(FAR struct bk7258_dvp_s *priv);
int bk7258_dvp_resume(FAR struct bk7258_dvp_s *priv);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_DVP_H */
