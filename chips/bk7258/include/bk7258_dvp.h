/****************************************************************************
 * chips/bk7258/include/bk7258_dvp.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP DVP image-data lower-half.  The v3.1.1.9 DVP API owns the
 * sensor discovery and hardware stream.  This wrapper exposes only the
 * standard NuttX imgdata object; a board sensor lower-half must be paired
 * with it when calling capture_register().
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DVP_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DVP_H

#include <nuttx/compiler.h>

#include <stddef.h>
#include <stdint.h>

#include <components/dvp_camera_types.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct imgdata_s;
struct bk7258_dvp_s;

/* The SDK can emit one sensor-control write while the first H.264 pipeline
 * is still being opened.  A physical sensor binding may ask the reusable
 * lower half to defer such a write without exposing SDK/linker wrappers to
 * the board source.  The callback supplies any sensor-specific immediate
 * hold value; the lower half replays the original write after open has
 * completed. */

#define BK7258_DVP_BINDING_VERSION       2u
#define BK7258_DVP_DEFERRED_I2C_WRITES   4u

enum bk7258_dvp_i2c_write_action_e
{
  BK7258_DVP_I2C_WRITE_PASS = 0,
  BK7258_DVP_I2C_WRITE_DEFER = 1,
};

struct bk7258_dvp_i2c_write_s
{
  uint8_t addr;
  uint8_t reg;
  uint8_t value;
  uint8_t immediate_value;
};

typedef int (*bk7258_dvp_i2c_write_cb_t)(FAR void *arg,
                                         FAR struct
                                           bk7258_dvp_i2c_write_s *write);

/* Board-specific sensor-control transports are described with an errno
 * based contract.  The chip lower half owns the immutable SDK wrappers and
 * translates its SDK I2C request into this stable board-facing structure.
 */

struct bk7258_dvp_i2c_transfer_s
{
  uint16_t address;
  uint32_t memory_address;
  uint8_t memory_address_bytes;
  FAR uint8_t *buffer;
  size_t length;
};

struct bk7258_dvp_i2c_ops_s
{
  int (*initialize)(FAR void *arg);
  int (*uninitialize)(FAR void *arg);
  int (*read)(FAR void *arg,
              FAR const struct bk7258_dvp_i2c_transfer_s *transfer);
  int (*write)(FAR void *arg,
               FAR const struct bk7258_dvp_i2c_transfer_s *transfer);
};

typedef int (*bk7258_dvp_prepare_cb_t)(FAR void *arg);
typedef void (*bk7258_dvp_mclk_started_cb_t)(FAR void *arg);

struct bk7258_dvp_binding_s
{
  uint16_t version;
  uint16_t size;
  FAR void *arg;
  bk7258_dvp_i2c_write_cb_t i2c_write;
  bk7258_dvp_prepare_cb_t prepare;
  bk7258_dvp_mclk_started_cb_t mclk_started;
  uint8_t i2c_bus;
  FAR const struct bk7258_dvp_i2c_ops_s *i2c;
};

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
  FAR const struct bk7258_dvp_binding_s *binding;
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
 * call.  The SDK handle itself is the wrapper's open/close ownership token;
 * no parallel state flag can hide a half-open handle.  This call releases the
 * wrapper's state but does not free caller-owned frame or encode memory. */

int bk7258_dvp_uninitialize(FAR struct bk7258_dvp_s *priv);

/* Board PM integration helpers.  They map directly to the SDK suspend and
 * resume operations and must be called in normal thread context. */

int bk7258_dvp_suspend(FAR struct bk7258_dvp_s *priv);
int bk7258_dvp_resume(FAR struct bk7258_dvp_s *priv);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DVP_H */
