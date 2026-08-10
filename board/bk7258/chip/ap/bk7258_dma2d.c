/****************************************************************************
 * board/bk7258/chip/ap/bk7258_dma2d.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP DMA2D typed helper.  The immutable BK7258 v3.1.1.9 bundle
 * exports the public driver/dma2d.h data path from libdriver.a.  NuttX has no
 * generic DMA2D upper-half, therefore this file intentionally exposes board
 * operations for an existing framebuffer/video owner rather than a device
 * node or a private character ABI.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>

/* These are public v3.1.1.9 bundle headers.  Do not replace them with the
 * SDK source tree or private HAL headers. */

#include <common/bk_err.h>
#include <driver/dma2d.h>

#include "../include/bk7258_dma2d.h"

#define BK7258_DMA2D_DEFAULT_TIMEOUT_MS 1000
#define BK7258_DMA2D_OFFSET_MAX         0x3fff
#define BK7258_DMA2D_IRQ_MASK           (DMA2D_CFG_ERROR | \
                                         DMA2D_TRANS_ERROR | \
                                         DMA2D_TRANS_COMPLETE)
#define BK7258_DMA2D_IRQ_PENDING        (-EINPROGRESS)

/* v3.1.1.9 routes DMA2D source 28 to physical CPU2.  NuttX AP logical CPU0
 * runs on physical CPU1, so move the group-1 route after SDK init just as the
 * RGB display wrapper does for source 27. */

#define BK7258_DMA2D_AP_PRIMARY_CORE_ID  1u
#define BK7258_DMA2D_SDK_DEFAULT_CORE_ID 2u
#define BK7258_DMA2D_INTERRUPT_CTRL_BIT  (1u << 28)
#define BK7258_DMA2D_MODULE_CTRL_REG      0x48080008u
#define BK7258_DMA2D_MODULE_CLK_GATE      (1u << 1)

extern int32_t sys_drv_core_intr_group1_disable(uint32_t core_id,
                                                uint32_t param);
extern int32_t sys_drv_core_intr_group1_enable(uint32_t core_id,
                                               uint32_t param);

struct bk7258_dma2d_s
{
  mutex_t lock;
  sem_t completion;
  volatile bool operation_active;
  volatile int irq_result;
  bool initialized;
  bool faulted;
  bool mode_valid;
  dma2d_mode_t last_mode;
};

static struct bk7258_dma2d_s g_bk7258_dma2d =
{
  .lock = NXMUTEX_INITIALIZER,
  .completion = SEM_INITIALIZER(0),
};

static int bk7258_dma2d_sdk_error(bk_err_t error)
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
        return -ENODEV;

      case BK_ERR_NOT_INIT:
      case BK_ERR_DMA2D_NOT_INIT:
        return -ENODEV;

      default:
        /* BK_FAIL and DMA2D-specific errors are SDK status values, not
         * NuttX errno values. */
        return -EIO;
    }
}

static int bk7258_dma2d_route_irq_to_ap_primary(void)
{
  int32_t ret;

  (void)sys_drv_core_intr_group1_disable(
          BK7258_DMA2D_SDK_DEFAULT_CORE_ID,
          BK7258_DMA2D_INTERRUPT_CTRL_BIT);
  ret = sys_drv_core_intr_group1_enable(BK7258_DMA2D_AP_PRIMARY_CORE_ID,
                                        BK7258_DMA2D_INTERRUPT_CTRL_BIT);
  return ret == 0 ? 0 : -EIO;
}

static void bk7258_dma2d_unroute_ap_primary_irq(void)
{
  (void)sys_drv_core_intr_group1_disable(
          BK7258_DMA2D_AP_PRIMARY_CORE_ID,
          BK7258_DMA2D_INTERRUPT_CTRL_BIT);
}

static int bk7258_dma2d_map_input(enum bk7258_dma2d_format_e format,
                                  input_color_mode_t *sdk_format)
{
  switch (format)
    {
      case BK7258_DMA2D_ARGB8888:
        *sdk_format = DMA2D_INPUT_ARGB8888;
        return 0;

      case BK7258_DMA2D_RGB888:
        *sdk_format = DMA2D_INPUT_RGB888;
        return 0;

      case BK7258_DMA2D_RGB565:
        *sdk_format = DMA2D_INPUT_RGB565;
        return 0;

      case BK7258_DMA2D_ARGB1555:
        *sdk_format = DMA2D_INPUT_ARGB1555;
        return 0;

      case BK7258_DMA2D_ARGB4444:
        *sdk_format = DMA2D_INPUT_ARGB4444;
        return 0;

      case BK7258_DMA2D_YUYV:
        *sdk_format = DMA2D_INPUT_YUYV;
        return 0;

      case BK7258_DMA2D_UYVY:
        *sdk_format = DMA2D_INPUT_UYVY;
        return 0;

      case BK7258_DMA2D_YYUV:
        *sdk_format = DMA2D_INPUT_YYUV;
        return 0;

      case BK7258_DMA2D_UVYY:
        *sdk_format = DMA2D_INPUT_UVYY;
        return 0;

      case BK7258_DMA2D_VUYY:
        *sdk_format = DMA2D_INPUT_VUYY;
        return 0;

      default:
        return -ENOTSUP;
    }
}

static int bk7258_dma2d_map_output(enum bk7258_dma2d_format_e format,
                                   out_color_mode_t *sdk_format)
{
  switch (format)
    {
      case BK7258_DMA2D_ARGB8888:
        *sdk_format = DMA2D_OUTPUT_ARGB8888;
        return 0;

      case BK7258_DMA2D_RGB888:
        *sdk_format = DMA2D_OUTPUT_RGB888;
        return 0;

      case BK7258_DMA2D_RGB565:
        *sdk_format = DMA2D_OUTPUT_RGB565;
        return 0;

      case BK7258_DMA2D_ARGB1555:
        *sdk_format = DMA2D_OUTPUT_ARGB1555;
        return 0;

      case BK7258_DMA2D_ARGB4444:
        *sdk_format = DMA2D_OUTPUT_ARGB4444;
        return 0;

      case BK7258_DMA2D_YUYV:
        *sdk_format = DMA2D_OUTPUT_YUYV;
        return 0;

      case BK7258_DMA2D_UYVY:
        *sdk_format = DMA2D_OUTPUT_UYVY;
        return 0;

      case BK7258_DMA2D_YYUV:
        *sdk_format = DMA2D_OUTPUT_YYUV;
        return 0;

      case BK7258_DMA2D_UVYY:
        *sdk_format = DMA2D_OUTPUT_UVYY;
        return 0;

      case BK7258_DMA2D_VUYY:
        *sdk_format = DMA2D_OUTPUT_VUYY;
        return 0;

      default:
        return -ENOTSUP;
    }
}

static int bk7258_dma2d_pixel_bytes(enum bk7258_dma2d_format_e format)
{
  switch (format)
    {
      case BK7258_DMA2D_ARGB8888:
        return FOUR_BYTES;

      case BK7258_DMA2D_RGB888:
        return THREE_BYTES;

      case BK7258_DMA2D_RGB565:
      case BK7258_DMA2D_ARGB1555:
      case BK7258_DMA2D_ARGB4444:
      case BK7258_DMA2D_YUYV:
      case BK7258_DMA2D_UYVY:
      case BK7258_DMA2D_YYUV:
      case BK7258_DMA2D_UVYY:
      case BK7258_DMA2D_VUYY:
        return TWO_BYTES;

      default:
        return -ENOTSUP;
    }
}

static int bk7258_dma2d_map_alpha(enum bk7258_dma2d_alpha_e alpha,
                                  blend_alpha_mode_t *sdk_alpha)
{
  switch (alpha)
    {
      case BK7258_DMA2D_ALPHA_KEEP:
        *sdk_alpha = DMA2D_NO_MODIF_ALPHA;
        return 0;

      case BK7258_DMA2D_ALPHA_REPLACE:
        *sdk_alpha = DMA2D_REPLACE_ALPHA;
        return 0;

      case BK7258_DMA2D_ALPHA_COMBINE:
        *sdk_alpha = DMA2D_COMBINE_ALPHA;
        return 0;

      default:
        return -EINVAL;
    }
}

static int bk7258_dma2d_map_swap(enum bk7258_dma2d_swap_e swap,
                                 red_blue_swap_t *sdk_swap)
{
  switch (swap)
    {
      case BK7258_DMA2D_SWAP_REGULAR:
        *sdk_swap = DMA2D_RB_REGULAR;
        return 0;

      case BK7258_DMA2D_SWAP_RED_BLUE:
        *sdk_swap = DMA2D_RB_SWAP;
        return 0;

      default:
        return -EINVAL;
    }
}

static int bk7258_dma2d_map_reverse(enum bk7258_dma2d_reverse_e reverse,
                                    data_reverse_t *sdk_reverse)
{
  switch (reverse)
    {
      case BK7258_DMA2D_REVERSE_NONE:
        *sdk_reverse = NO_REVERSE;
        return 0;

      case BK7258_DMA2D_REVERSE_BYTE:
        *sdk_reverse = BYTE_BY_BYTE_REVERSE;
        return 0;

      case BK7258_DMA2D_REVERSE_HALFWORD:
        *sdk_reverse = HFWORD_BY_HFWORD_REVERSE;
        return 0;

      default:
        return -EINVAL;
    }
}

static int bk7258_dma2d_validate_extent(FAR const void *buffer,
                                        uint16_t frame_width,
                                        uint16_t frame_height,
                                        uint16_t x, uint16_t y,
                                        uint16_t width, uint16_t height,
                                        int pixel_bytes)
{
  uint64_t base;
  uint64_t last_offset;
  uint64_t last_pixel;
  uint64_t last_row;

  if (buffer == NULL || frame_width == 0 || frame_height == 0 ||
      width == 0 || height == 0 || pixel_bytes < TWO_BYTES)
    {
      return -EINVAL;
    }

  if ((uint32_t)x + width > frame_width ||
      (uint32_t)y + height > frame_height ||
      (uint32_t)frame_width - width > BK7258_DMA2D_OFFSET_MAX)
    {
      return -ERANGE;
    }

  /* The immutable SDK truncates addresses to uint32_t and computes the
   * rectangle offset internally.  Reject values that cannot be represented
   * by that ABI rather than silently truncating them. */

  base = (uintptr_t)buffer;
  if (base > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  /* The SDK computes the start address in 32-bit pointer arithmetic and the
   * DMA engine then walks every row using the frame stride.  Check the final
   * byte touched, not only the first pixel, so neither the row calculation
   * nor base-plus-offset can wrap. */

  last_row = (uint64_t)y + height - 1;
  last_pixel = last_row * frame_width + x;
  last_offset = last_pixel * pixel_bytes +
                (uint64_t)width * pixel_bytes - 1;
  if (last_offset > UINT32_MAX || base + last_offset > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  return 0;
}

static void bk7258_dma2d_cache_bounds(FAR const void *buffer,
                                      uint16_t frame_width,
                                      uint16_t x, uint16_t y,
                                      uint16_t width, uint16_t height,
                                      int pixel_bytes,
                                      FAR uintptr_t *start,
                                      FAR uintptr_t *end)
{
  *start = (uintptr_t)buffer +
           ((uint32_t)frame_width * y + x) * pixel_bytes;
  *end = (uintptr_t)buffer +
         (((uint32_t)frame_width * (y + height - 1) + x + width) *
          pixel_bytes);
}

static int bk7258_dma2d_timeout(uint32_t timeout_ms, FAR clock_t *ticks)
{
  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_DMA2D_DEFAULT_TIMEOUT_MS;
    }

  *ticks = MSEC2TICK(timeout_ms);
  if (*ticks <= 0)
    {
      *ticks = 1;
    }

  return 0;
}

/* SDK DMA2D callbacks are invoked from DMA2D_Handler.  They only publish a
 * status and post the NuttX semaphore; no mutex or other blocking operation
 * is allowed in this ISR context. */

static void bk7258_dma2d_complete_isr(void *arg)
{
  FAR struct bk7258_dma2d_s *priv = arg;

  if (priv != &g_bk7258_dma2d)
    {
      return;
    }

  /* A completion cannot clear an error already published by an error
   * callback.  Both callbacks may be raised for one transfer. */

  if (priv->irq_result == BK7258_DMA2D_IRQ_PENDING)
    {
      priv->irq_result = 0;
    }

  if (priv->operation_active)
    {
      (void)nxsem_post(&priv->completion);
    }
}

static void bk7258_dma2d_error_isr(void *arg)
{
  FAR struct bk7258_dma2d_s *priv = arg;

  if (priv != &g_bk7258_dma2d)
    {
      return;
    }

  /* Keep an error sticky until the waiter consumes this transfer.  A later
   * completion callback must not turn this operation into success. */

  priv->irq_result = -EIO;
  if (priv->operation_active)
    {
      (void)nxsem_post(&priv->completion);
    }
}

static int bk7258_dma2d_start_locked(FAR struct bk7258_dma2d_s *priv,
                                     uint32_t timeout_ms)
{
  FAR clock_t ticks;
  bk_err_t sdkret;
  int ret;

  ret = nxsem_reset(&priv->completion, 0);
  if (ret < 0)
    {
      return ret;
    }

  priv->irq_result = BK7258_DMA2D_IRQ_PENDING;
  priv->operation_active = true;

  /* Publish CPU buffer stores and all preceding DMA2D register writes before
   * the start bit becomes visible to the accelerator.  The current AP profile
   * keeps D-cache disabled, but normal SRAM still requires ordering against
   * the device transaction. */

  __asm volatile ("dmb sy" ::: "memory");

  sdkret = bk_dma2d_start_transfer();
  ret = bk7258_dma2d_sdk_error(sdkret);
  if (ret < 0)
    {
      priv->operation_active = false;
      priv->faulted = true;
      return ret;
    }

  bk7258_dma2d_timeout(timeout_ms, &ticks);
  ret = nxsem_tickwait_uninterruptible(&priv->completion, ticks);
  priv->operation_active = false;

  /* Order accelerator writes before the caller inspects its destination.
   * Cache maintenance remains the caller's responsibility if a future board
   * enables a cacheable mapping. */

  __asm volatile ("dmb sy" ::: "memory");

  if (ret < 0)
    {
      if (ret == -ETIMEDOUT)
        {
          /* The public SDK has no bounded abort API.  Stop prevents a new
           * start, and the instance is poisoned until deinitialized so that
           * a potentially still-running DMA transfer cannot be reused. */

          (void)bk_dma2d_stop_transfer();
          priv->faulted = true;
        }

      return ret;
    }

  ret = priv->irq_result;
  if (ret < 0)
    {
      priv->faulted = true;
    }

  return ret;
}

static int bk7258_dma2d_check_handle(FAR struct bk7258_dma2d_s *priv)
{
  if (priv != &g_bk7258_dma2d)
    {
      return -EINVAL;
    }

  if (!priv->initialized)
    {
      return -ENODEV;
    }

  if (priv->faulted)
    {
      return -EIO;
    }

  return 0;
}

/* BK7258 retains transfer-mode state across operations.  In particular, an
 * R2M fill followed by an M2M/PFC copy can complete without moving source
 * data.  Reinitialize the engine before programming a different mode, then
 * restore the settings that the v3.1.1.9 SDK and the Tuya integration
 * establish on their single-mode initialization paths. */

static int
bk7258_dma2d_prepare_mode_locked(FAR struct bk7258_dma2d_s *priv,
                                 dma2d_mode_t mode)
{
  bk_err_t sdkret;
  int ret;

  if (priv->mode_valid && priv->last_mode != mode)
    {
      /* A module soft-reset is insufficient on BK7258: R2M state still
       * survives into a following PFC/M2M operation.  The v3.1.1.9 public
       * deinit/init sequence power-cycles VIDP and is the smallest sequence
       * proven to restore source reads.  Rebuild the SDK ISR callbacks and
       * move its CPU2 route back to the NuttX AP primary afterwards. */

      (void)bk_dma2d_int_enable(BK7258_DMA2D_IRQ_MASK, false);
      bk7258_dma2d_unroute_ap_primary_irq();

      ret = bk7258_dma2d_sdk_error(bk_dma2d_driver_deinit());
      if (ret < 0)
        {
          goto fail;
        }

      ret = bk7258_dma2d_sdk_error(bk_dma2d_driver_init());
      if (ret < 0)
        {
          goto fail;
        }

      ret = bk7258_dma2d_route_irq_to_ap_primary();
      if (ret < 0)
        {
          goto fail_initialized;
        }

      sdkret = bk_dma2d_register_int_callback_isr(
        DMA2D_TRANS_COMPLETE_ISR, bk7258_dma2d_complete_isr, priv);
      ret = bk7258_dma2d_sdk_error(sdkret);
      if (ret < 0)
        {
          goto fail_initialized;
        }

      sdkret = bk_dma2d_register_int_callback_isr(
        DMA2D_TRANS_ERROR_ISR, bk7258_dma2d_error_isr, priv);
      ret = bk7258_dma2d_sdk_error(sdkret);
      if (ret < 0)
        {
          goto fail_initialized;
        }

      sdkret = bk_dma2d_register_int_callback_isr(
        DMA2D_CFG_ERROR_ISR, bk7258_dma2d_error_isr, priv);
      ret = bk7258_dma2d_sdk_error(sdkret);
      if (ret < 0)
        {
          goto fail_initialized;
        }

      ret = bk7258_dma2d_sdk_error(
        bk_dma2d_int_enable(BK7258_DMA2D_IRQ_MASK, true));
      if (ret < 0)
        {
          goto fail_initialized;
        }
    }

  priv->last_mode = mode;
  priv->mode_valid = true;
  return 0;

fail_initialized:
  bk7258_dma2d_unroute_ap_primary_irq();
  (void)bk_dma2d_int_enable(BK7258_DMA2D_IRQ_MASK, false);
  (void)bk_dma2d_register_int_callback_isr(DMA2D_TRANS_COMPLETE_ISR,
                                            NULL, NULL);
  (void)bk_dma2d_register_int_callback_isr(DMA2D_TRANS_ERROR_ISR, NULL,
                                            NULL);
  (void)bk_dma2d_register_int_callback_isr(DMA2D_CFG_ERROR_ISR, NULL, NULL);
  (void)bk_dma2d_driver_deinit();

fail:
  priv->mode_valid = false;
  priv->faulted = true;
  return ret;
}

static int bk7258_dma2d_arm_locked(void)
{
  FAR volatile uint32_t *module_ctrl =
    (FAR volatile uint32_t *)BK7258_DMA2D_MODULE_CTRL_REG;

  *module_ctrl |= BK7258_DMA2D_MODULE_CLK_GATE;
  dma2d_driver_transfes_ability(TRANS_16BYTES);
  __asm volatile ("dmb sy" ::: "memory");

  return bk7258_dma2d_sdk_error(
    bk_dma2d_int_enable(BK7258_DMA2D_IRQ_MASK, true));
}

int bk7258_dma2d_initialize(FAR struct bk7258_dma2d_s **out)
{
  FAR struct bk7258_dma2d_s *priv = &g_bk7258_dma2d;
  bk_err_t sdkret;
  int ret;

  if (out == NULL)
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

  sdkret = bk_dma2d_driver_init();
  ret = bk7258_dma2d_sdk_error(sdkret);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  ret = bk7258_dma2d_route_irq_to_ap_primary();
  if (ret < 0)
    {
      (void)bk_dma2d_driver_deinit();
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  sdkret = bk_dma2d_register_int_callback_isr(DMA2D_TRANS_COMPLETE_ISR,
                                               bk7258_dma2d_complete_isr,
                                               priv);
  ret = bk7258_dma2d_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }

  sdkret = bk_dma2d_register_int_callback_isr(DMA2D_TRANS_ERROR_ISR,
                                               bk7258_dma2d_error_isr, priv);
  ret = bk7258_dma2d_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }

  sdkret = bk_dma2d_register_int_callback_isr(DMA2D_CFG_ERROR_ISR,
                                               bk7258_dma2d_error_isr, priv);
  ret = bk7258_dma2d_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }

  sdkret = bk_dma2d_int_enable(BK7258_DMA2D_IRQ_MASK, true);
  ret = bk7258_dma2d_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }

  priv->faulted = false;
  priv->mode_valid = false;
  priv->initialized = true;
  *out = priv;
  nxmutex_unlock(&priv->lock);
  return 0;

fail:
  bk7258_dma2d_unroute_ap_primary_irq();
  (void)bk_dma2d_int_enable(BK7258_DMA2D_IRQ_MASK, false);
  (void)bk_dma2d_register_int_callback_isr(DMA2D_TRANS_COMPLETE_ISR,
                                            NULL, NULL);
  (void)bk_dma2d_register_int_callback_isr(DMA2D_TRANS_ERROR_ISR, NULL,
                                            NULL);
  (void)bk_dma2d_register_int_callback_isr(DMA2D_CFG_ERROR_ISR, NULL, NULL);
  (void)bk_dma2d_driver_deinit();
  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_dma2d_uninitialize(FAR struct bk7258_dma2d_s *priv)
{
  bk_err_t sdkret;
  int ret;
  int cleanup_ret;
  int deinit_ret;

  if (priv != &g_bk7258_dma2d)
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
      return 0;
    }

  /* Operations hold this mutex from SDK configuration through completion,
   * so a concurrent uninitialize cannot tear down an active callback path. */

  cleanup_ret = bk7258_dma2d_sdk_error(bk_dma2d_stop_transfer());
  if (cleanup_ret < 0 && ret == 0)
    {
      ret = cleanup_ret;
    }

  sdkret = bk_dma2d_int_enable(BK7258_DMA2D_IRQ_MASK, false);
  cleanup_ret = bk7258_dma2d_sdk_error(sdkret);
  if (cleanup_ret < 0 && ret == 0)
    {
      ret = cleanup_ret;
    }

  (void)bk_dma2d_register_int_callback_isr(DMA2D_TRANS_COMPLETE_ISR,
                                            NULL, NULL);
  (void)bk_dma2d_register_int_callback_isr(DMA2D_TRANS_ERROR_ISR, NULL,
                                            NULL);
  (void)bk_dma2d_register_int_callback_isr(DMA2D_CFG_ERROR_ISR, NULL, NULL);

  bk7258_dma2d_unroute_ap_primary_irq();
  sdkret = bk_dma2d_driver_deinit();
  deinit_ret = bk7258_dma2d_sdk_error(sdkret);
  if (deinit_ret < 0 && ret == 0)
    {
      ret = deinit_ret;
    }

  priv->operation_active = false;
  if (deinit_ret < 0)
    {
      /* Keep ownership visible when the SDK did not tear down its driver.
       * The next uninitialize call can retry the public deinit operation,
       * while data operations remain fail-closed through faulted. */

      priv->faulted = true;
    }
  else
    {
      priv->faulted = false;
      priv->mode_valid = false;
      priv->initialized = false;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_dma2d_copy(FAR struct bk7258_dma2d_s *priv,
                      FAR const struct bk7258_dma2d_copy_s *copy)
{
  dma2d_memcpy_pfc_t sdk;
  input_color_mode_t input_format;
  out_color_mode_t output_format;
  data_reverse_t input_reverse;
  data_reverse_t output_reverse;
  red_blue_swap_t input_swap;
  red_blue_swap_t output_swap;
  int src_bytes;
  int dst_bytes;
  int ret;
  bool pfc;
  uintptr_t src_start;
  uintptr_t src_end;
  uintptr_t dst_start;
  uintptr_t dst_end;

  if (copy == NULL)
    {
      return -EINVAL;
    }

  src_bytes = bk7258_dma2d_pixel_bytes(copy->src_format);
  dst_bytes = bk7258_dma2d_pixel_bytes(copy->dst_format);
  ret = bk7258_dma2d_map_input(copy->src_format, &input_format);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_output(copy->dst_format, &output_format);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_reverse(copy->src_reverse, &input_reverse);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_reverse(copy->dst_reverse, &output_reverse);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_swap(copy->src_swap, &input_swap);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_swap(copy->dst_swap, &output_swap);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_validate_extent(copy->src, copy->src_frame_width,
                                     copy->src_frame_height, copy->src_x,
                                     copy->src_y, copy->width, copy->height,
                                     src_bytes);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_validate_extent(copy->dst, copy->dst_frame_width,
                                     copy->dst_frame_height, copy->dst_x,
                                     copy->dst_y, copy->width, copy->height,
                                     dst_bytes);
  if (ret < 0)
    {
      return ret;
    }

  bk7258_dma2d_cache_bounds(copy->src, copy->src_frame_width,
                            copy->src_x, copy->src_y, copy->width,
                            copy->height, src_bytes, &src_start, &src_end);
  bk7258_dma2d_cache_bounds(copy->dst, copy->dst_frame_width,
                            copy->dst_x, copy->dst_y, copy->width,
                            copy->height, dst_bytes, &dst_start, &dst_end);

  /* Raw M2M is a 32-bit word copy on BK7258.  Other pixel widths must use
   * PFC even when the source and destination formats are identical. */

  pfc = src_bytes != FOUR_BYTES || dst_bytes != FOUR_BYTES ||
        copy->src_format != copy->dst_format ||
        copy->src_swap != BK7258_DMA2D_SWAP_REGULAR ||
        copy->dst_swap != BK7258_DMA2D_SWAP_REGULAR ||
        copy->src_reverse != BK7258_DMA2D_REVERSE_NONE ||
        copy->dst_reverse != BK7258_DMA2D_REVERSE_NONE ||
        copy->input_alpha != 0 || copy->output_alpha != 0;

  sdk = (dma2d_memcpy_pfc_t){0};
  sdk.mode = pfc ? DMA2D_M2M_PFC : DMA2D_M2M;
  sdk.input_addr = (void *)(uintptr_t)copy->src;
  sdk.src_frame_width = copy->src_frame_width;
  sdk.src_frame_height = copy->src_frame_height;
  sdk.src_frame_xpos = copy->src_x;
  sdk.src_frame_ypos = copy->src_y;
  sdk.input_color_mode = input_format;
  sdk.src_pixel_byte = (color_bytes_t)src_bytes;
  sdk.input_data_reverse = input_reverse;
  sdk.input_red_blue_swap = input_swap;
  sdk.output_addr = copy->dst;
  sdk.dst_frame_width = copy->dst_frame_width;
  sdk.dst_frame_height = copy->dst_frame_height;
  sdk.dst_frame_xpos = copy->dst_x;
  sdk.dst_frame_ypos = copy->dst_y;
  sdk.output_color_mode = output_format;
  sdk.dst_pixel_byte = (color_bytes_t)dst_bytes;
  sdk.output_red_blue_swap = output_swap;
  sdk.out_byte_by_byte_reverse = output_reverse;
  sdk.dma2d_width = copy->width;
  sdk.dma2d_height = copy->height;
  sdk.input_alpha = copy->input_alpha;
  sdk.output_alpha = copy->output_alpha;

  ret = nxmutex_lock(&g_bk7258_dma2d.lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_check_handle(priv);
  if (ret >= 0)
    {
      /* DMA2D is not cache coherent with the AP cores.  Publish CPU writes
       * before DMA reads, and discard any dirty/stale destination lines
       * before and after DMA ownership. */

      up_clean_dcache(src_start, src_end);
      up_flush_dcache(dst_start, dst_end);

      ret = bk7258_dma2d_prepare_mode_locked(priv, sdk.mode);

      /* This SDK configuration function has a void return type.  Any
       * hardware configuration failure is therefore reported by its ISR;
       * no unconditional success is inferred here. */

      if (ret >= 0)
        {
          bk_dma2d_memcpy_or_pixel_convert(&sdk);
          ret = bk7258_dma2d_arm_locked();
        }

      if (ret >= 0)
        {
          ret = bk7258_dma2d_start_locked(priv, copy->timeout_ms);
        }

      if (ret >= 0)
        {
          up_invalidate_dcache(dst_start, dst_end);
        }
    }

  nxmutex_unlock(&g_bk7258_dma2d.lock);
  return ret;
}

int bk7258_dma2d_fill(FAR struct bk7258_dma2d_s *priv,
                      FAR const struct bk7258_dma2d_fill_s *fill)
{
  dma2d_fill_t sdk;
  out_color_mode_t output_format;
  int pixel_bytes;
  int ret;
  uintptr_t dst_start;
  uintptr_t dst_end;

  if (fill == NULL)
    {
      return -EINVAL;
    }

  pixel_bytes = bk7258_dma2d_pixel_bytes(fill->format);
  ret = bk7258_dma2d_map_output(fill->format, &output_format);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_validate_extent(fill->dst, fill->frame_width,
                                     fill->frame_height, fill->x, fill->y,
                                     fill->width, fill->height, pixel_bytes);
  if (ret < 0)
    {
      return ret;
    }

  bk7258_dma2d_cache_bounds(fill->dst, fill->frame_width,
                            fill->x, fill->y, fill->width, fill->height,
                            pixel_bytes, &dst_start, &dst_end);

  sdk = (dma2d_fill_t){0};
  sdk.frameaddr = fill->dst;
  sdk.frame_xsize = fill->frame_width;
  sdk.frame_ysize = fill->frame_height;
  sdk.xpos = fill->x;
  sdk.ypos = fill->y;
  sdk.width = fill->width;
  sdk.height = fill->height;
  sdk.color_format = output_format;
  sdk.pixel_byte = (color_bytes_t)pixel_bytes;
  sdk.color = fill->color;

  ret = nxmutex_lock(&g_bk7258_dma2d.lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_check_handle(priv);
  if (ret >= 0)
    {
      up_flush_dcache(dst_start, dst_end);
      ret = bk7258_dma2d_prepare_mode_locked(priv, DMA2D_R2M);
      if (ret >= 0)
        {
          ret = bk7258_dma2d_sdk_error(dma2d_fill(&sdk));
        }

      if (ret >= 0)
        {
          ret = bk7258_dma2d_arm_locked();
          if (ret >= 0)
            {
              ret = bk7258_dma2d_start_locked(priv, fill->timeout_ms);
            }

          if (ret >= 0)
            {
              up_invalidate_dcache(dst_start, dst_end);
            }
        }
    }

  nxmutex_unlock(&g_bk7258_dma2d.lock);
  return ret;
}

int bk7258_dma2d_blend(FAR struct bk7258_dma2d_s *priv,
                       FAR const struct bk7258_dma2d_blend_s *blend)
{
  dma2d_offset_blend_t sdk;
  input_color_mode_t foreground_format;
  input_color_mode_t background_format;
  out_color_mode_t dst_format;
  blend_alpha_mode_t foreground_alpha;
  blend_alpha_mode_t background_alpha;
  red_blue_swap_t foreground_swap;
  red_blue_swap_t background_swap;
  red_blue_swap_t dst_swap;
  data_reverse_t foreground_reverse;
  data_reverse_t output_reverse;
  int foreground_bytes;
  int background_bytes;
  int dst_bytes;
  int ret;
  uintptr_t foreground_start;
  uintptr_t foreground_end;
  uintptr_t background_start;
  uintptr_t background_end;
  uintptr_t dst_start;
  uintptr_t dst_end;

  if (blend == NULL)
    {
      return -EINVAL;
    }

  foreground_bytes = bk7258_dma2d_pixel_bytes(blend->foreground_format);
  background_bytes = bk7258_dma2d_pixel_bytes(blend->background_format);
  dst_bytes = bk7258_dma2d_pixel_bytes(blend->dst_format);
  ret = bk7258_dma2d_map_input(blend->foreground_format,
                               &foreground_format);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_input(blend->background_format,
                               &background_format);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_output(blend->dst_format, &dst_format);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_alpha(blend->foreground_alpha_mode,
                               &foreground_alpha);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_alpha(blend->background_alpha_mode,
                               &background_alpha);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_swap(blend->foreground_swap, &foreground_swap);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_swap(blend->background_swap, &background_swap);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_swap(blend->dst_swap, &dst_swap);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_reverse(blend->foreground_reverse,
                                 &foreground_reverse);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_map_reverse(blend->output_reverse, &output_reverse);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_validate_extent(blend->foreground,
                                     blend->foreground_frame_width,
                                     blend->foreground_frame_height,
                                     blend->foreground_x,
                                     blend->foreground_y, blend->width,
                                     blend->height, foreground_bytes);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_validate_extent(blend->background,
                                     blend->background_frame_width,
                                     blend->background_frame_height,
                                     blend->background_x,
                                     blend->background_y, blend->width,
                                     blend->height, background_bytes);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_validate_extent(blend->dst, blend->dst_frame_width,
                                     blend->dst_frame_height, blend->dst_x,
                                     blend->dst_y, blend->width, blend->height,
                                     dst_bytes);
  if (ret < 0)
    {
      return ret;
    }

  bk7258_dma2d_cache_bounds(blend->foreground,
                            blend->foreground_frame_width,
                            blend->foreground_x, blend->foreground_y,
                            blend->width, blend->height, foreground_bytes,
                            &foreground_start, &foreground_end);
  bk7258_dma2d_cache_bounds(blend->background,
                            blend->background_frame_width,
                            blend->background_x, blend->background_y,
                            blend->width, blend->height, background_bytes,
                            &background_start, &background_end);
  bk7258_dma2d_cache_bounds(blend->dst, blend->dst_frame_width,
                            blend->dst_x, blend->dst_y, blend->width,
                            blend->height, dst_bytes, &dst_start, &dst_end);

  sdk = (dma2d_offset_blend_t){0};
  sdk.pfg_addr = (void *)(uintptr_t)blend->foreground;
  sdk.pbg_addr = (void *)(uintptr_t)blend->background;
  sdk.pdst_addr = blend->dst;
  sdk.fg_color_mode = foreground_format;
  sdk.bg_color_mode = background_format;
  sdk.dst_color_mode = dst_format;
  sdk.fg_frame_xpos = blend->foreground_x;
  sdk.fg_frame_ypos = blend->foreground_y;
  sdk.bg_frame_xpos = blend->background_x;
  sdk.bg_frame_ypos = blend->background_y;
  sdk.dst_frame_xpos = blend->dst_x;
  sdk.dst_frame_ypos = blend->dst_y;
  sdk.fg_frame_width = blend->foreground_frame_width;
  sdk.fg_frame_height = blend->foreground_frame_height;
  sdk.bg_frame_width = blend->background_frame_width;
  sdk.bg_frame_height = blend->background_frame_height;
  sdk.dst_frame_width = blend->dst_frame_width;
  sdk.dst_frame_height = blend->dst_frame_height;
  sdk.dma2d_width = blend->width;
  sdk.dma2d_height = blend->height;
  sdk.fg_alpha_mode = foreground_alpha;
  sdk.bg_alpha_mode = background_alpha;
  sdk.fg_alpha_value = blend->foreground_alpha;
  sdk.bg_alpha_value = blend->background_alpha;
  sdk.fg_red_blue_swap = foreground_swap;
  sdk.bg_red_blue_swap = background_swap;
  sdk.dst_red_blue_swap = dst_swap;
  sdk.fg_pixel_byte = (color_bytes_t)foreground_bytes;
  sdk.bg_pixel_byte = (color_bytes_t)background_bytes;
  sdk.dst_pixel_byte = (color_bytes_t)dst_bytes;
  sdk.input_data_reverse = foreground_reverse;
  sdk.out_byte_by_byte_reverse = output_reverse;

  ret = nxmutex_lock(&g_bk7258_dma2d.lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_dma2d_check_handle(priv);
  if (ret >= 0)
    {
      up_clean_dcache(foreground_start, foreground_end);
      up_clean_dcache(background_start, background_end);
      up_flush_dcache(dst_start, dst_end);
      ret = bk7258_dma2d_prepare_mode_locked(priv, DMA2D_M2M_BLEND);
      if (ret >= 0)
        {
          ret = bk7258_dma2d_sdk_error(bk_dma2d_offset_blend(&sdk));
        }

      if (ret >= 0)
        {
          ret = bk7258_dma2d_arm_locked();
          if (ret >= 0)
            {
              ret = bk7258_dma2d_start_locked(priv, blend->timeout_ms);
            }

          if (ret >= 0)
            {
              up_invalidate_dcache(dst_start, dst_end);
            }
        }
    }

  nxmutex_unlock(&g_bk7258_dma2d.lock);
  return ret;
}
