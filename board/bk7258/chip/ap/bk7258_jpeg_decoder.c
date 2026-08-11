/****************************************************************************
 * board/bk7258/chip/ap/bk7258_jpeg_decoder.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP JPEG decoder typed helper.  The immutable v3.1.1.9 bundle
 * exports the high-level hardware decoder from libbk_jpeg_decoder.a.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/mutex.h>

#include <common/bk_err.h>
#include <components/avdk_utils/avdk_error.h>
#include <components/media_types.h>

#include "../include/bk7258_jpeg_decoder.h"

/* The v3.1.1.9 jpeg_dec_driver.c routes JPEGDEC to CPU2 unconditionally.
 * AP NuttX executes on CPU1, so migrate the interrupt route after the SDK
 * decoder has opened.  sys_driver.h and sys_types.h are not exported by the
 * immutable bundle; these declarations/constants are the minimal ABI from
 * those v3.1.1.9 public driver symbols and the verified BK7258 definitions:
 * CPU1=1, CPU2=2, and JPEGDEC interrupt bit 26. */

extern int32_t sys_drv_core_intr_group1_enable(uint32_t core_id,
                                               uint32_t param);
extern int32_t sys_drv_core_intr_group1_disable(uint32_t core_id,
                                                uint32_t param);

#define BK7258_JPEGDEC_CPU1_CORE_ID          1u
#define BK7258_JPEGDEC_CPU2_CORE_ID          2u
#define BK7258_JPEGDEC_INTERRUPT_CTRL_BIT    (1u << 26)

/*
 * The v3.1.1.9 public bk_jpeg_decode_types.h includes frame_buffer.h, which
 * is not exported by the immutable AP bundle.  The complete frame_buffer_t
 * is nevertheless exported by media_types.h, so use that public type here.
 * Keep only the JPEG config/info/handle declarations needed to bridge the
 * leaked public include; the implementations remain in immutable
 * libbk_jpeg_decoder.a.  These are the six high-level operations used here.
 */

typedef struct
{
  bk_err_t (*in_complete)(frame_buffer_t *in_frame);
  frame_buffer_t *(*out_malloc)(uint32_t size);
  bk_err_t (*out_complete)(uint32_t format_type, uint32_t result,
                           frame_buffer_t *out_frame);
} bk7258_sdk_jpeg_decode_callback_t;

typedef struct
{
  bk7258_sdk_jpeg_decode_callback_t decode_cbs;
} bk7258_sdk_jpeg_decode_hw_config_t;

typedef struct bk7258_sdk_jpeg_decode_hw_s
  *bk7258_sdk_jpeg_decode_hw_handle_t;

typedef struct
{
  frame_buffer_t *frame;
  uint32_t width;
  uint32_t height;
  uint32_t format;
} bk7258_sdk_jpeg_decode_img_info_t;

extern avdk_err_t bk_hardware_jpeg_decode_new(
  bk7258_sdk_jpeg_decode_hw_handle_t *handle,
  bk7258_sdk_jpeg_decode_hw_config_t *config);
extern avdk_err_t bk_jpeg_decode_hw_open(
  bk7258_sdk_jpeg_decode_hw_handle_t handle);
extern avdk_err_t bk_jpeg_decode_hw_close(
  bk7258_sdk_jpeg_decode_hw_handle_t handle);
extern avdk_err_t bk_jpeg_decode_hw_get_img_info(
  bk7258_sdk_jpeg_decode_hw_handle_t handle,
  bk7258_sdk_jpeg_decode_img_info_t *info);
extern avdk_err_t bk_jpeg_decode_hw_decode(
  bk7258_sdk_jpeg_decode_hw_handle_t handle,
  frame_buffer_t *in_frame,
  frame_buffer_t *out_frame);
extern avdk_err_t bk_jpeg_decode_hw_delete(
  bk7258_sdk_jpeg_decode_hw_handle_t handle);

#define BK7258_JPEG_YUV_PIXEL_BYTES 2u
#define BK7258_JPEG_FMT_YUV444      1u
#define BK7258_JPEG_FMT_YUV422      2u
#define BK7258_JPEG_FMT_YUV420      3u
#define BK7258_JPEG_FMT_YUV400      4u

struct bk7258_jpeg_decoder_s
{
  bk7258_sdk_jpeg_decode_hw_handle_t handle;
  bool initialized;
  bool opened;
  bool cpu1_route_enabled;
  bool operation_active;
};

static mutex_t g_bk7258_jpeg_decoder_lock = NXMUTEX_INITIALIZER;
static struct bk7258_jpeg_decoder_s g_bk7258_jpeg_decoder;
static FAR struct bk7258_jpeg_decoder_s *g_bk7258_jpeg_decoder_owner;

static int bk7258_jpeg_decoder_sdk_error(avdk_err_t error)
{
  switch (error)
    {
      case BK_OK:
        return 0;

      case AVDK_ERR_INVAL:
      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
        return -EINVAL;

      case AVDK_ERR_NOMEM:
      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case AVDK_ERR_BUSY:
      case BK_ERR_BUSY:
      case BK_ERR_IN_PROGRESS:
        return -EBUSY;

      case AVDK_ERR_TIMEOUT:
      case BK_ERR_TIMEOUT:
        return -ETIMEDOUT;

      case AVDK_ERR_NODEV:
      case BK_ERR_NO_DEV:
      case BK_ERR_NOT_INIT:
        return -ENODEV;

      case AVDK_ERR_UNSUPPORTED:
      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      case AVDK_ERR_NO_RESOURCE:
        return -ENOMEM;

      case AVDK_ERR_SHUTDOWN:
      case BK_ERR_SHUT_DOWN:
        return -ESHUTDOWN;

      case BK_ERR_TRY_AGAIN:
        return -EAGAIN;

      case AVDK_ERR_GENERIC:
      case AVDK_ERR_HWERROR:
      case AVDK_ERR_IO:
      default:
        /* The SDK's fixed hardware timeout is reported as HWERROR by its
         * public synchronous path.  Do not manufacture -ETIMEDOUT here. */
        return -EIO;
    }
}

static int bk7258_jpeg_decoder_check_locked(
  FAR struct bk7258_jpeg_decoder_s *priv)
{
  if (priv == NULL)
    {
      return -EINVAL;
    }

  if (priv != g_bk7258_jpeg_decoder_owner || !priv->initialized ||
      !priv->opened || !priv->cpu1_route_enabled || priv->handle == NULL)
    {
      return -ENODEV;
    }

  return 0;
}

static int bk7258_jpeg_decoder_route_error(int32_t result)
{
  return result == 0 ? 0 : -EIO;
}

static int bk7258_jpeg_decoder_route_to_cpu1(void)
{
  int ret;

  ret = bk7258_jpeg_decoder_route_error(
    sys_drv_core_intr_group1_disable(BK7258_JPEGDEC_CPU2_CORE_ID,
                                     BK7258_JPEGDEC_INTERRUPT_CTRL_BIT));
  if (ret < 0)
    {
      /* Disable is idempotent in the SDK path.  This also clears a partial
       * CPU1 route if a lower layer reported failure after changing state. */

      (void)sys_drv_core_intr_group1_disable(
        BK7258_JPEGDEC_CPU1_CORE_ID, BK7258_JPEGDEC_INTERRUPT_CTRL_BIT);
      return ret;
    }

  ret = bk7258_jpeg_decoder_route_error(
    sys_drv_core_intr_group1_enable(BK7258_JPEGDEC_CPU1_CORE_ID,
                                    BK7258_JPEGDEC_INTERRUPT_CTRL_BIT));
  if (ret < 0)
    {
      (void)sys_drv_core_intr_group1_disable(
        BK7258_JPEGDEC_CPU1_CORE_ID, BK7258_JPEGDEC_INTERRUPT_CTRL_BIT);
      return ret;
    }

  return 0;
}

static int bk7258_jpeg_decoder_route_from_cpu1(void)
{
  return bk7258_jpeg_decoder_route_error(
    sys_drv_core_intr_group1_disable(BK7258_JPEGDEC_CPU1_CORE_ID,
                                     BK7258_JPEGDEC_INTERRUPT_CTRL_BIT));
}

/* The SDK passes addresses to a 32-bit JPEG register interface.  Reject a
 * range whose final byte cannot be represented by that interface, even when
 * this wrapper is syntax-checked on a wider host. */

static int bk7258_jpeg_decoder_validate_sdk_address(
  FAR const uint8_t *data, uint32_t span)
{
  uint64_t base;
  uint64_t last;

  if (data == NULL || span == 0)
    {
      return -EINVAL;
    }

  base = (uint64_t)(uintptr_t)data;
  if (base > UINT64_MAX - span)
    {
      return -EOVERFLOW;
    }

  last = base + span - 1;
  if (last > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  return 0;
}

static int bk7258_jpeg_decoder_validate_input(
  FAR const struct bk7258_jpeg_decoder_frame_s *input)
{
  int ret;

  if (input == NULL || input->data == NULL || input->capacity == 0 ||
      input->length == 0 || input->length > input->capacity)
    {
      return -EINVAL;
    }

  ret = bk7258_jpeg_decoder_validate_sdk_address(input->data,
                                                 input->length);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_jpeg_decoder_validate_sdk_address(input->data,
                                                   input->capacity);
}

static int bk7258_jpeg_decoder_validate_output(
  FAR const struct bk7258_jpeg_decoder_frame_s *output)
{
  if (output == NULL || output->data == NULL || output->capacity == 0)
    {
      return -EINVAL;
    }

  return bk7258_jpeg_decoder_validate_sdk_address(output->data,
                                                  output->capacity);
}

static int bk7258_jpeg_decoder_validate_no_overlap(
  FAR const struct bk7258_jpeg_decoder_frame_s *input,
  FAR const struct bk7258_jpeg_decoder_frame_s *output)
{
  uintptr_t input_start = (uintptr_t)input->data;
  uintptr_t output_start = (uintptr_t)output->data;
  uintptr_t input_end;
  uintptr_t output_end;

  if (input_start > UINTPTR_MAX - input->length ||
      output_start > UINTPTR_MAX - output->capacity)
    {
      return -EINVAL;
    }

  input_end = input_start + input->length;
  output_end = output_start + output->capacity;

  if (input_start < output_end && output_start < input_end)
    {
      return -EINVAL;
    }

  return 0;
}

static int bk7258_jpeg_decoder_make_input(
  FAR const struct bk7258_jpeg_decoder_frame_s *input,
  FAR frame_buffer_t *sdk_input)
{
  int ret = bk7258_jpeg_decoder_validate_input(input);

  if (ret < 0)
    {
      return ret;
    }

  *sdk_input = (frame_buffer_t){0};
  sdk_input->frame = input->data;
  sdk_input->size = input->capacity;
  sdk_input->length = input->length;
  sdk_input->fmt = PIXEL_FMT_JPEG;
  return 0;
}

static int bk7258_jpeg_decoder_make_output(
  FAR const struct bk7258_jpeg_decoder_frame_s *output,
  FAR frame_buffer_t *sdk_output)
{
  int ret = bk7258_jpeg_decoder_validate_output(output);

  if (ret < 0)
    {
      return ret;
    }

  *sdk_output = (frame_buffer_t){0};
  sdk_output->frame = output->data;
  sdk_output->size = output->capacity;
  sdk_output->fmt = PIXEL_FMT_YUYV;
  return 0;
}

static int bk7258_jpeg_decoder_map_info(
  FAR const bk7258_sdk_jpeg_decode_img_info_t *sdk_info,
  FAR struct bk7258_jpeg_decoder_info_s *info)
{
  if (sdk_info == NULL || info == NULL || sdk_info->width == 0 ||
      sdk_info->height == 0 || sdk_info->width > UINT16_MAX ||
      sdk_info->height > UINT16_MAX)
    {
      return -EINVAL;
    }

  info->width = sdk_info->width;
  info->height = sdk_info->height;

  switch (sdk_info->format)
    {
      case BK7258_JPEG_FMT_YUV444:
        info->format = BK7258_JPEG_DECODER_YUV444;
        break;

      case BK7258_JPEG_FMT_YUV422:
        info->format = BK7258_JPEG_DECODER_YUV422;
        break;

      case BK7258_JPEG_FMT_YUV420:
        info->format = BK7258_JPEG_DECODER_YUV420;
        break;

      case BK7258_JPEG_FMT_YUV400:
        info->format = BK7258_JPEG_DECODER_YUV400;
        break;

      default:
        return -ENOTSUP;
    }

  return 0;
}

static int bk7258_jpeg_decoder_get_info_locked(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_info_s *info,
  FAR frame_buffer_t *sdk_input,
  FAR bk7258_sdk_jpeg_decode_img_info_t *sdk_info)
{
  int ret;
  avdk_err_t sdkret;

  ret = bk7258_jpeg_decoder_make_input(input, sdk_input);
  if (ret < 0)
    {
      return ret;
    }

  sdk_info->frame = sdk_input;
  sdkret = bk_jpeg_decode_hw_get_img_info(priv->handle, sdk_info);
  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_jpeg_decoder_map_info(sdk_info, info);
}

int bk7258_jpeg_decoder_initialize(FAR struct bk7258_jpeg_decoder_s **out)
{
  bk7258_sdk_jpeg_decode_hw_config_t config = {0};
  bk7258_sdk_jpeg_decode_hw_handle_t handle = NULL;
  avdk_err_t sdkret;
  int ret;

  if (out == NULL)
    {
      return -EINVAL;
    }

  *out = NULL;
  ret = nxmutex_lock(&g_bk7258_jpeg_decoder_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_jpeg_decoder_owner != NULL)
    {
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return -EBUSY;
    }

  sdkret = bk_hardware_jpeg_decode_new(&handle, &config);
  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret < 0 || handle == NULL)
    {
      if (ret >= 0)
        {
          ret = -EIO;
        }

      if (handle != NULL)
        {
          (void)bk_jpeg_decode_hw_delete(handle);
        }

      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return ret;
    }

  sdkret = bk_jpeg_decode_hw_open(handle);
  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret < 0)
    {
      /* A failed open leaves the SDK controller disabled on its normal
       * path, so delete is the documented failure cleanup. */
      (void)bk_jpeg_decode_hw_delete(handle);
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return ret;
    }

  ret = bk7258_jpeg_decoder_route_to_cpu1();
  if (ret < 0)
    {
      /* The SDK open path has already registered its ISR and enabled the
       * CPU2 route.  Always close/delete on migration failure; close repeats
       * the SDK's CPU2 disable and the route helper clears any CPU1 partial
       * state. */

      (void)bk_jpeg_decode_hw_close(handle);
      (void)bk_jpeg_decode_hw_delete(handle);
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return -EIO;
    }

  g_bk7258_jpeg_decoder.handle = handle;
  g_bk7258_jpeg_decoder.initialized = true;
  g_bk7258_jpeg_decoder.opened = true;
  g_bk7258_jpeg_decoder.cpu1_route_enabled = true;
  g_bk7258_jpeg_decoder.operation_active = false;
  g_bk7258_jpeg_decoder_owner = &g_bk7258_jpeg_decoder;
  *out = g_bk7258_jpeg_decoder_owner;

  nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
  return 0;
}

int bk7258_jpeg_decoder_uninitialize(FAR struct bk7258_jpeg_decoder_s *priv)
{
  avdk_err_t sdkret;
  int ret;

  if (priv == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_jpeg_decoder_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv != g_bk7258_jpeg_decoder_owner || !priv->initialized)
    {
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return -EINVAL;
    }

  if (priv->operation_active)
    {
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return -EBUSY;
    }

  if (priv->opened)
    {
      sdkret = bk_jpeg_decode_hw_close(priv->handle);
      ret = bk7258_jpeg_decoder_sdk_error(sdkret);
      if (ret < 0)
        {
          nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
          return ret;
        }

      priv->opened = false;
    }

  if (priv->cpu1_route_enabled)
    {
      ret = bk7258_jpeg_decoder_route_from_cpu1();
      if (ret < 0)
        {
          /* Keep ownership and the closed handle so a later uninitialize
           * can retry the CPU1 route disable before deleting the controller. */
          nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
          return ret;
        }

      priv->cpu1_route_enabled = false;
    }

  sdkret = bk_jpeg_decode_hw_delete(priv->handle);
  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret < 0)
    {
      /* Keep ownership and the closed handle so a later uninitialize can
       * retry deletion without allowing a second SDK instance. */
      nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
      return ret;
    }

  priv->handle = NULL;
  priv->initialized = false;
  priv->operation_active = false;
  g_bk7258_jpeg_decoder_owner = NULL;

  nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
  return 0;
}

int bk7258_jpeg_decoder_get_info(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_info_s *info)
{
  frame_buffer_t sdk_input;
  bk7258_sdk_jpeg_decode_img_info_t sdk_info = {0};
  int ret;

  if (info == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_jpeg_decoder_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_check_locked(priv);
  if (ret >= 0)
    {
      ret = bk7258_jpeg_decoder_get_info_locked(priv, input, info,
                                                &sdk_input, &sdk_info);
    }

  nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
  return ret;
}

int bk7258_jpeg_decoder_decode(
  FAR struct bk7258_jpeg_decoder_s *priv,
  FAR struct bk7258_jpeg_decoder_frame_s *input,
  FAR struct bk7258_jpeg_decoder_frame_s *output)
{
  frame_buffer_t sdk_input;
  frame_buffer_t sdk_output;
  bk7258_sdk_jpeg_decode_img_info_t sdk_info = {0};
  struct bk7258_jpeg_decoder_info_s info;
  uint64_t output_bytes;
  avdk_err_t sdkret;
  int ret;

  ret = bk7258_jpeg_decoder_validate_input(input);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_validate_output(output);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_validate_no_overlap(input, output);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_bk7258_jpeg_decoder_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_check_locked(priv);
  if (ret < 0)
    {
      goto out_unlock;
    }

  if (priv->operation_active)
    {
      ret = -EBUSY;
      goto out_unlock;
    }

  ret = bk7258_jpeg_decoder_get_info_locked(priv, input, &info,
                                            &sdk_input, &sdk_info);
  if (ret < 0)
    {
      goto out_unlock;
    }

  output_bytes = (uint64_t)info.width * info.height *
                 BK7258_JPEG_YUV_PIXEL_BYTES;
  if (output_bytes > UINT32_MAX || output_bytes > output->capacity)
    {
      ret = -ENOSPC;
      goto out_unlock;
    }

  ret = bk7258_jpeg_decoder_make_output(output, &sdk_output);
  if (ret < 0)
    {
      goto out_unlock;
    }

  sdk_input.width = (uint16_t)info.width;
  sdk_input.height = (uint16_t)info.height;
  sdk_output.width = (uint16_t)info.width;
  sdk_output.height = (uint16_t)info.height;
  priv->operation_active = true;
  sdkret = bk_jpeg_decode_hw_decode(priv->handle, &sdk_input, &sdk_output);
  priv->operation_active = false;
  ret = bk7258_jpeg_decoder_sdk_error(sdkret);
  if (ret >= 0)
    {
      input->width = sdk_input.width;
      input->height = sdk_input.height;
      output->width = sdk_output.width;
      output->height = sdk_output.height;
      output->length = (uint32_t)output_bytes;
    }

out_unlock:
  nxmutex_unlock(&g_bk7258_jpeg_decoder_lock);
  return ret;
}
