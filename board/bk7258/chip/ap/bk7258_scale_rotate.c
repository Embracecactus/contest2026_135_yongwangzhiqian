/****************************************************************************
 * board/bk7258/chip/ap/bk7258_scale_rotate.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP typed helper for the v3.1.1.9 hardware scale and rotator
 * engines.  NuttX has no generic upper-half for these operations; an
 * existing framebuffer/video owner must supply the buffers and call this
 * helper from normal thread context.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <common/bk_err.h>
#include <driver/hw_scale.h>
#include <driver/rott_driver.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>

#include <arch/chip/bk7258_scale_rotate.h>

#define BK7258_SCALE_ROTATE_DEFAULT_TIMEOUT_MS 1000
#define BK7258_SCALE_ROTATE_MAX_DIM            1280
#define BK7258_SCALE_ROTATE_MIN_BLOCK_PIXELS  1
#define BK7258_SCALE_ROTATE_MAX_BLOCK_PIXELS  4800

/* These are the non-zero bookkeeping values used by the SDK's official
 * BLOCK_SCALE examples.  The common scale ISR reads line_cycle and applies a
 * modulo before it branches on FRAME_SCALE, even though FRAME_SCALE address
 * programming does not use block addresses.  Keep both fields representable
 * and safe for that shared ISR path; never encode src_height in line_cycle. */

#define BK7258_SCALE_ROTATE_FRAME_LINE_CYCLE 16
#define BK7258_SCALE_ROTATE_FRAME_LINE_MASK  0x1f

struct bk7258_scale_rotate_s
{
  mutex_t lock;
  sem_t completion;
  enum bk7258_scale_rotate_engine_e engine;
  volatile bool operation_active;
  volatile int completion_result;
  bool initialized;
  bool faulted;
};

static struct bk7258_scale_rotate_s g_bk7258_scale_rotate =
{
  .lock = NXMUTEX_INITIALIZER,
  .completion = SEM_INITIALIZER(0),
};

static int bk7258_scale_rotate_sdk_error(bk_err_t error)
{
  switch (error)
    {
      case BK_OK:
        return 0;

      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
        return -EINVAL;

      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case BK_ERR_TIMEOUT:
        return -ETIMEDOUT;

      case BK_ERR_BUSY:
      case BK_ERR_IN_PROGRESS:
        return -EBUSY;

      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      case BK_ERR_NO_DEV:
      case BK_ERR_NOT_FOUND:
      case BK_ERR_NOT_INIT:
      case BK_ERR_HW_SCALE_NOT_INIT:
      case BK_ERR_ROTT_NOT_INIT:
        return -ENODEV;

      default:
        return -EIO;
    }
}

static bool bk7258_scale_rotate_valid_engine(
  enum bk7258_scale_rotate_engine_e engine)
{
  return engine == BK7258_SCALE_ROTATE_SCALE0 ||
         engine == BK7258_SCALE_ROTATE_SCALE1 ||
         engine == BK7258_SCALE_ROTATE_ROTATOR;
}

static bool bk7258_scale_rotate_is_scale(
  enum bk7258_scale_rotate_engine_e engine)
{
  return engine == BK7258_SCALE_ROTATE_SCALE0 ||
         engine == BK7258_SCALE_ROTATE_SCALE1;
}

static scale_id_t bk7258_scale_rotate_scale_id(
  enum bk7258_scale_rotate_engine_e engine)
{
  return engine == BK7258_SCALE_ROTATE_SCALE1 ? HW_SCALE1 : HW_SCALE0;
}

static int bk7258_scale_rotate_pixel_bytes(
  enum bk7258_scale_rotate_format_e format)
{
  switch (format)
    {
      case BK7258_SCALE_ROTATE_RGB565_LE:
      case BK7258_SCALE_ROTATE_RGB565:
      case BK7258_SCALE_ROTATE_YUYV:
      case BK7258_SCALE_ROTATE_UYVY:
      case BK7258_SCALE_ROTATE_YYUV:
      case BK7258_SCALE_ROTATE_UVYY:
      case BK7258_SCALE_ROTATE_VUYY:
        return 2;

      default:
        return -ENOTSUP;
    }
}

static int bk7258_scale_rotate_map_scale_format(
  enum bk7258_scale_rotate_format_e format, pixel_format_t *mapped)
{
  if (mapped == NULL)
    {
      return -EINVAL;
    }

  switch (format)
    {
      case BK7258_SCALE_ROTATE_RGB565:
        *mapped = PIXEL_FMT_RGB565;
        return 0;

      case BK7258_SCALE_ROTATE_YUYV:
        *mapped = PIXEL_FMT_YUYV;
        return 0;

      default:
        return -ENOTSUP;
    }
}

static int bk7258_scale_rotate_map_rotate_format(
  enum bk7258_scale_rotate_format_e format, pixel_format_t *mapped)
{
  if (mapped == NULL)
    {
      return -EINVAL;
    }

  switch (format)
    {
      case BK7258_SCALE_ROTATE_RGB565_LE:
        *mapped = PIXEL_FMT_RGB565_LE;
        return 0;

      case BK7258_SCALE_ROTATE_RGB565:
        *mapped = PIXEL_FMT_RGB565;
        return 0;

      case BK7258_SCALE_ROTATE_YUYV:
        *mapped = PIXEL_FMT_YUYV;
        return 0;

      case BK7258_SCALE_ROTATE_UYVY:
        *mapped = PIXEL_FMT_UYVY;
        return 0;

      case BK7258_SCALE_ROTATE_YYUV:
        *mapped = PIXEL_FMT_YYUV;
        return 0;

      case BK7258_SCALE_ROTATE_UVYY:
        *mapped = PIXEL_FMT_UVYY;
        return 0;

      case BK7258_SCALE_ROTATE_VUYY:
        *mapped = PIXEL_FMT_VUYY;
        return 0;

      default:
        return -ENOTSUP;
    }
}

static int bk7258_scale_rotate_map_angle(
  enum bk7258_scale_rotate_angle_e angle, media_rotate_t *mapped)
{
  if (mapped == NULL)
    {
      return -EINVAL;
    }

  switch (angle)
    {
      case BK7258_SCALE_ROTATE_NONE:
        *mapped = ROTATE_NONE;
        return 0;

      case BK7258_SCALE_ROTATE_90:
        *mapped = ROTATE_90;
        return 0;

      case BK7258_SCALE_ROTATE_270:
        *mapped = ROTATE_270;
        return 0;

      case BK7258_SCALE_ROTATE_180:
      default:
        return -ENOTSUP;
    }
}

static int bk7258_scale_rotate_map_input_flow(
  enum bk7258_scale_rotate_input_flow_e flow,
  rott_input_data_flow_t *mapped)
{
  if (mapped == NULL)
    {
      return -EINVAL;
    }

  switch (flow)
    {
      case BK7258_SCALE_ROTATE_INPUT_NORMAL:
        *mapped = ROTT_INPUT_NORMAL;
        return 0;

      case BK7258_SCALE_ROTATE_INPUT_REVERSE_BYTE:
        *mapped = ROTT_INPUT_REVESE_BYTE_BY_BYTE;
        return 0;

      case BK7258_SCALE_ROTATE_INPUT_REVERSE_HALFWORD:
        *mapped = ROTT_INPUT_REVESE_HALFWORD_BY_HALFWORD;
        return 0;

      default:
        return -EINVAL;
    }
}

static int bk7258_scale_rotate_map_output_flow(
  enum bk7258_scale_rotate_output_flow_e flow,
  rott_output_data_flow_t *mapped)
{
  if (mapped == NULL)
    {
      return -EINVAL;
    }

  switch (flow)
    {
      case BK7258_SCALE_ROTATE_OUTPUT_NORMAL:
        *mapped = ROTT_OUTPUT_NORMAL;
        return 0;

      case BK7258_SCALE_ROTATE_OUTPUT_REVERSE_HALFWORD:
        *mapped = ROTT_OUTPUT_REVESE_HALFWORD_BY_HALFWORD;
        return 0;

      default:
        return -EINVAL;
    }
}

static int bk7258_scale_rotate_validate_buffer(FAR const void *buffer,
                                               size_t length)
{
  uint64_t base;

  if (buffer == NULL || length == 0)
    {
      return -EINVAL;
    }

  base = (uintptr_t)buffer;
  if (base > UINT32_MAX || (uint64_t)length - 1 > UINT32_MAX - base)
    {
      return -EOVERFLOW;
    }

  return 0;
}

static bool bk7258_scale_rotate_ranges_overlap(FAR const void *left,
                                               size_t left_size,
                                               FAR const void *right,
                                               size_t right_size)
{
  uint64_t left_start = (uintptr_t)left;
  uint64_t right_start = (uintptr_t)right;
  uint64_t left_end = left_start + left_size;
  uint64_t right_end = right_start + right_size;

  return left_start < right_end && right_start < left_end;
}

static int bk7258_scale_rotate_timeout(uint32_t timeout_ms,
                                       FAR clock_t *ticks)
{
  uint64_t value;

  if (ticks == NULL)
    {
      return -EINVAL;
    }

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_SCALE_ROTATE_DEFAULT_TIMEOUT_MS;
    }

  value = ((uint64_t)timeout_ms * CLK_TCK + 999) / 1000;
  if (value == 0)
    {
      value = 1;
    }

  if (value > (uint64_t)INT_MAX)
    {
      return -ERANGE;
    }

  *ticks = (clock_t)value;
  return 0;
}

/* Both SDK drivers call their registered completion callback directly from
 * the peripheral ISR.  These callbacks only publish a result and post a
 * semaphore; all locking, timeout recovery and buffer ownership remain in
 * the caller's thread context. */

static void bk7258_scale_rotate_scale_isr(FAR void *arg)
{
  FAR struct bk7258_scale_rotate_s *priv = arg;

  if (priv == &g_bk7258_scale_rotate && priv->operation_active)
    {
      priv->completion_result = 0;
      (void)nxsem_post(&priv->completion);
    }
}

static void bk7258_scale_rotate_rotate_complete_isr(void)
{
  FAR struct bk7258_scale_rotate_s *priv = &g_bk7258_scale_rotate;

  if (priv->engine == BK7258_SCALE_ROTATE_ROTATOR &&
      priv->operation_active)
    {
      priv->completion_result = 0;
      (void)nxsem_post(&priv->completion);
    }
}

static void bk7258_scale_rotate_rotate_error_isr(void)
{
  FAR struct bk7258_scale_rotate_s *priv = &g_bk7258_scale_rotate;

  if (priv->engine == BK7258_SCALE_ROTATE_ROTATOR &&
      priv->operation_active)
    {
      priv->completion_result = -EIO;
      (void)nxsem_post(&priv->completion);
    }
}

static int bk7258_scale_rotate_prepare_wait_locked(
  FAR struct bk7258_scale_rotate_s *priv, uint32_t timeout_ms,
  FAR clock_t *ticks)
{
  int ret;

  ret = bk7258_scale_rotate_timeout(timeout_ms, ticks);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxsem_reset(&priv->completion, 0);
  if (ret < 0)
    {
      return ret;
    }

  priv->completion_result = -EIO;
  priv->operation_active = true;
  return 0;
}

static int bk7258_scale_rotate_wait_locked(
  FAR struct bk7258_scale_rotate_s *priv, clock_t ticks)
{
  int ret;

  ret = nxsem_tickwait_uninterruptible(&priv->completion, ticks);
  priv->operation_active = false;
  if (ret < 0)
    {
      if (priv->engine == BK7258_SCALE_ROTATE_ROTATOR)
        {
          (void)bk_rott_soft_reset();
        }
      else
        {
          (void)bk_hw_scale_stop(
            bk7258_scale_rotate_scale_id(priv->engine));
        }

      /* The public APIs provide no bounded abort-and-join contract.  Poison
       * the owner after timeout so a possibly running DMA transfer cannot be
       * reused until the caller deinitializes it. */

      priv->faulted = true;
      return ret;
    }

  ret = priv->completion_result;
  if (ret < 0)
    {
      priv->faulted = true;
    }

  return ret;
}

static int bk7258_scale_rotate_check(
  FAR struct bk7258_scale_rotate_s *priv,
  enum bk7258_scale_rotate_engine_e engine)
{
  if (priv != &g_bk7258_scale_rotate)
    {
      return -EINVAL;
    }

  if (!priv->initialized)
    {
      return -ENODEV;
    }

  if (priv->engine != engine)
    {
      return -ENOTSUP;
    }

  if (priv->faulted)
    {
      return -EIO;
    }

  return 0;
}

static int bk7258_scale_rotate_init_scale_locked(
  FAR struct bk7258_scale_rotate_s *priv)
{
  scale_id_t id = bk7258_scale_rotate_scale_id(priv->engine);
  int ret;

  ret = bk7258_scale_rotate_sdk_error(bk_hw_scale_driver_init(id));
  if (ret < 0)
    {
      /* The SDK may have allocated one of its records before reporting a
       * failed init.  Its deinit API intentionally does not free them.  If
       * that failure occurred after SDK PM/IRQ setup but before is_init was
       * set, the public SDK has no rollback API; mem_free is the only safe
       * cleanup available to this wrapper. */

      (void)bk_hw_scale_mem_free(id);
      return ret;
    }

  ret = bk7258_scale_rotate_sdk_error(
    bk_hw_scale_isr_register(id, bk7258_scale_rotate_scale_isr, priv));
  if (ret < 0)
    {
      (void)bk_hw_scale_driver_deinit(id);
      (void)bk_hw_scale_mem_free(id);
      return ret;
    }

  ret = bk7258_scale_rotate_sdk_error(bk_hw_scale_int_enable(id, true));
  if (ret < 0)
    {
      (void)bk_hw_scale_isr_unregister(id);
      (void)bk_hw_scale_driver_deinit(id);
      (void)bk_hw_scale_mem_free(id);
      return ret;
    }

  return 0;
}

static int bk7258_scale_rotate_init_rotate_locked(void)
{
  int ret;

  ret = bk7258_scale_rotate_sdk_error(bk_rott_driver_init());
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_scale_rotate_sdk_error(
    bk_rott_isr_register(ROTATE_COMPLETE_INT,
                         bk7258_scale_rotate_rotate_complete_isr));
  if (ret < 0)
    {
      (void)bk_rott_driver_deinit();
      return ret;
    }

  ret = bk7258_scale_rotate_sdk_error(
    bk_rott_isr_register(ROTATE_CFG_ERR_INT,
                         bk7258_scale_rotate_rotate_error_isr));
  if (ret < 0)
    {
      (void)bk_rott_isr_register(ROTATE_COMPLETE_INT, NULL);
      (void)bk_rott_driver_deinit();
      return ret;
    }

  ret = bk7258_scale_rotate_sdk_error(
    bk_rott_int_enable(ROTATE_COMPLETE_INT | ROTATE_CFG_ERR_INT, true));
  if (ret < 0)
    {
      (void)bk_rott_isr_register(ROTATE_COMPLETE_INT, NULL);
      (void)bk_rott_isr_register(ROTATE_CFG_ERR_INT, NULL);
      (void)bk_rott_driver_deinit();
      return ret;
    }

  return 0;
}

int bk7258_scale_rotate_initialize(
  FAR struct bk7258_scale_rotate_s **out,
  enum bk7258_scale_rotate_engine_e engine)
{
  FAR struct bk7258_scale_rotate_s *priv = &g_bk7258_scale_rotate;
  int ret;

  if (out == NULL || !bk7258_scale_rotate_valid_engine(engine))
    {
      return -EINVAL;
    }

  *out = NULL;
  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->initialized)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  priv->engine = engine;
  priv->faulted = false;
  ret = bk7258_scale_rotate_is_scale(engine) ?
    bk7258_scale_rotate_init_scale_locked(priv) :
    bk7258_scale_rotate_init_rotate_locked();
  if (ret >= 0)
    {
      priv->initialized = true;
      *out = priv;
    }
  else
    {
      priv->engine = BK7258_SCALE_ROTATE_SCALE0;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_scale_rotate_uninitialize(
  FAR struct bk7258_scale_rotate_s *priv)
{
  int ret;
  int cleanup_ret;

  if (priv != &g_bk7258_scale_rotate)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->initialized)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  if (bk7258_scale_rotate_is_scale(priv->engine))
    {
      scale_id_t id = bk7258_scale_rotate_scale_id(priv->engine);

      cleanup_ret = bk7258_scale_rotate_sdk_error(
        bk_hw_scale_int_enable(id, false));
      (void)bk_hw_scale_isr_unregister(id);
      ret = bk7258_scale_rotate_sdk_error(bk_hw_scale_driver_deinit(id));
      {
        int free_ret = bk7258_scale_rotate_sdk_error(
          bk_hw_scale_mem_free(id));
        if (ret >= 0 && free_ret < 0)
          {
            ret = free_ret;
          }
      }
    }
  else
    {
      cleanup_ret = bk7258_scale_rotate_sdk_error(
        bk_rott_int_enable(ROTATE_COMPLETE_INT | ROTATE_CFG_ERR_INT, false));
      (void)bk_rott_isr_register(ROTATE_COMPLETE_INT, NULL);
      (void)bk_rott_isr_register(ROTATE_CFG_ERR_INT, NULL);
      (void)bk_rott_soft_reset();
      ret = bk7258_scale_rotate_sdk_error(bk_rott_driver_deinit());
    }

  if (ret >= 0 && cleanup_ret < 0)
    {
      ret = cleanup_ret;
    }

  priv->initialized = false;
  priv->faulted = false;
  priv->operation_active = false;
  priv->engine = BK7258_SCALE_ROTATE_SCALE0;
  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_scale(FAR struct bk7258_scale_rotate_s *priv,
                 FAR const struct bk7258_scale_request_s *request)
{
  scale_drv_config_t config;
  pixel_format_t format;
  clock_t ticks;
  uint64_t src_need;
  uint64_t dst_need;
  int pixel_bytes;
  int ret;

  if (request == NULL || priv == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_scale_rotate_check(priv, priv->engine);
  if (ret < 0 || !bk7258_scale_rotate_is_scale(priv->engine))
    {
      ret = ret < 0 ? ret : -ENOTSUP;
      goto out;
    }

  pixel_bytes = bk7258_scale_rotate_pixel_bytes(request->format);
  ret = bk7258_scale_rotate_map_scale_format(request->format, &format);
  if (ret < 0)
    {
      goto out;
    }

  if (pixel_bytes < 0 || request->src_width == 0 ||
      request->src_height == 0 || request->dst_width == 0 ||
      request->dst_height == 0 || request->src_width > BK7258_SCALE_ROTATE_MAX_DIM ||
      request->src_height > BK7258_SCALE_ROTATE_MAX_DIM ||
      request->dst_width > BK7258_SCALE_ROTATE_MAX_DIM ||
      request->dst_height > BK7258_SCALE_ROTATE_MAX_DIM ||
      (request->dst_width & 7) != 0)
    {
      ret = -EINVAL;
      goto out;
    }

  src_need = (uint64_t)request->src_width * request->src_height * pixel_bytes;
  dst_need = (uint64_t)request->dst_width * request->dst_height * pixel_bytes;
  if (src_need > request->src_size || dst_need > request->dst_size)
    {
      ret = -EINVAL;
      goto out;
    }

  ret = bk7258_scale_rotate_validate_buffer(request->src, request->src_size);
  if (ret < 0)
    {
      goto out;
    }

  ret = bk7258_scale_rotate_validate_buffer(request->dst, request->dst_size);
  if (ret < 0)
    {
      goto out;
    }

  if (bk7258_scale_rotate_ranges_overlap(request->src, request->src_size,
                                         request->dst, request->dst_size))
    {
      ret = -EINVAL;
      goto out;
    }

  memset(&config, 0, sizeof(config));
  config.line_cycle = BK7258_SCALE_ROTATE_FRAME_LINE_CYCLE;
  config.line_mask = BK7258_SCALE_ROTATE_FRAME_LINE_MASK;
  config.src_width = request->src_width;
  config.src_height = request->src_height;
  config.dst_width = request->dst_width;
  config.dst_height = request->dst_height;
  config.scale_mode = FRAME_SCALE;
  config.pixel_fmt = format;
  config.src_addr = (FAR uint8_t *)request->src;
  config.dst_addr = request->dst;

  ret = bk7258_scale_rotate_prepare_wait_locked(priv, request->timeout_ms,
                                                &ticks);
  if (ret < 0)
    {
      goto out;
    }

  ret = bk7258_scale_rotate_sdk_error(hw_scale_frame(
    bk7258_scale_rotate_scale_id(priv->engine), &config));
  if (ret < 0)
    {
      priv->operation_active = false;
      priv->faulted = true;
      goto out;
    }

  ret = bk7258_scale_rotate_wait_locked(priv, ticks);

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_rotate(FAR struct bk7258_scale_rotate_s *priv,
                  FAR const struct bk7258_rotate_request_s *request)
{
  rott_config_t config;
  pixel_format_t format;
  media_rotate_t angle;
  rott_input_data_flow_t input_flow;
  rott_output_data_flow_t output_flow;
  uint16_t output_width;
  uint16_t output_height;
  uint64_t src_need;
  uint64_t dst_need;
  clock_t ticks;
  int ret;

  if (request == NULL || priv == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_scale_rotate_check(priv, priv->engine);
  if (ret < 0 || priv->engine != BK7258_SCALE_ROTATE_ROTATOR)
    {
      ret = ret < 0 ? ret : -ENOTSUP;
      goto out;
    }

  ret = bk7258_scale_rotate_map_rotate_format(request->format, &format);
  if (ret < 0)
    {
      goto out;
    }

  ret = bk7258_scale_rotate_map_angle(request->angle, &angle);
  if (ret < 0)
    {
      goto out;
    }

  ret = bk7258_scale_rotate_map_input_flow(request->input_flow, &input_flow);
  if (ret < 0)
    {
      goto out;
    }

  ret = bk7258_scale_rotate_map_output_flow(request->output_flow,
                                            &output_flow);
  if (ret < 0)
    {
      goto out;
    }

  if (request->src_width == 0 || request->src_height == 0 ||
      request->block_width < BK7258_SCALE_ROTATE_MIN_BLOCK_PIXELS ||
      request->block_height < BK7258_SCALE_ROTATE_MIN_BLOCK_PIXELS ||
      (uint32_t)request->block_width * request->block_height >
        BK7258_SCALE_ROTATE_MAX_BLOCK_PIXELS ||
      request->src_width % request->block_width != 0 ||
      request->src_height % request->block_height != 0 ||
      request->block_count !=
        (uint32_t)(request->src_width / request->block_width) *
        (request->src_height / request->block_height) ||
      request->watermark_block > request->block_count)
    {
      ret = -EINVAL;
      goto out;
    }

  output_width = request->angle == BK7258_SCALE_ROTATE_NONE ?
    request->src_width : request->src_height;
  output_height = request->angle == BK7258_SCALE_ROTATE_NONE ?
    request->src_height : request->src_width;
  src_need = (uint64_t)request->src_width * request->src_height * 2;
  dst_need = (uint64_t)output_width * output_height * 2;
  if (src_need > request->src_size || dst_need > request->dst_size)
    {
      ret = -EINVAL;
      goto out;
    }

  ret = bk7258_scale_rotate_validate_buffer(request->src, request->src_size);
  if (ret < 0)
    {
      goto out;
    }

  ret = bk7258_scale_rotate_validate_buffer(request->dst, request->dst_size);
  if (ret < 0)
    {
      goto out;
    }

  if (bk7258_scale_rotate_ranges_overlap(request->src, request->src_size,
                                         request->dst, request->dst_size))
    {
      ret = -EINVAL;
      goto out;
    }

  memset(&config, 0, sizeof(config));
  config.rot_mode = angle;
  config.input_addr = (FAR void *)request->src;
  config.output_addr = request->dst;
  config.input_fmt = format;
  config.input_flow = input_flow;
  config.output_flow = output_flow;
  config.picture_xpixel = request->src_width;
  config.picture_ypixel = request->src_height;
  config.block_xpixel = request->block_width;
  config.block_ypixel = request->block_height;
  config.block_cnt = request->block_count;
  config.watermark_blk = request->watermark_block;

  ret = bk7258_scale_rotate_sdk_error(rott_config(&config));
  if (ret < 0)
    {
      priv->faulted = true;
      goto out;
    }

  /* v3.1.1.9 rott_config leaves the input/output flow fields unused in its
   * implementation.  Apply the same public settings explicitly. */

  ret = bk7258_scale_rotate_sdk_error(
    bk_rott_data_reverse(input_flow, output_flow));
  if (ret >= 0)
    {
      ret = bk7258_scale_rotate_prepare_wait_locked(priv,
                                                    request->timeout_ms,
                                                    &ticks);
    }

  if (ret >= 0)
    {
      ret = bk7258_scale_rotate_sdk_error(bk_rott_enable());
    }

  if (ret >= 0)
    {
      ret = bk7258_scale_rotate_wait_locked(priv, ticks);
    }

  if (ret < 0)
    {
      priv->operation_active = false;
      priv->faulted = true;
    }

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}
