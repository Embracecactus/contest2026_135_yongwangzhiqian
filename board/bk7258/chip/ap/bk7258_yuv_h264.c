/****************************************************************************
 * board/bk7258/chip/ap/bk7258_yuv_h264.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP typed YUV-buffer/H.264 helper for the immutable v3.1.1.9
 * low-level driver bundle.  NuttX has no generic upper-half matching this
 * line-fed encoder, so the public interface is deliberately synchronous and
 * board-owned.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

#include <common/bk_err.h>
#include <driver/dma.h>
#include <driver/h264.h>
#include <driver/yuv_buf.h>

#include "../include/bk7258_yuv_h264.h"
#include "bk7258_media_root.h"

#define BK7258_YUV_H264_DEFAULT_TIMEOUT_MS 5000u
#define BK7258_YUV_H264_MAX_WIDTH          1920u
#define BK7258_YUV_H264_MAX_HEIGHT         1088u
#define BK7258_YUV_H264_LINES_PER_BLOCK    16u
#define BK7258_YUV_H264_PIXEL_BYTES        2u
#define BK7258_YUV_H264_DMA_USER           DMA_DEV_H264
/* This is the v3.1.1.9 H.264 pipeline's repeat-DMA transaction size. */
#define BK7258_YUV_H264_DMA_CHUNK_BYTES    (10u * 1024u)

/* The v3.1.1.9 H.264 driver unconditionally selects CPU2 for group-2 H.264
 * interrupts.  sys_driver.h is not exported by the immutable bundle; these
 * are the minimal exported ABI declarations.  The bit value is from the
 * official BK7258 sys_types.h/sys_reg.h (group-2 position 14).  Disable
 * returns the old enable register rather than an errno/status value. */

extern int32_t sys_drv_core_intr_group2_enable(uint32_t core_id,
                                               uint32_t param);
extern int32_t sys_drv_core_intr_group2_disable(uint32_t core_id,
                                                uint32_t param);

#define BK7258_YUV_H264_CPU1_CORE_ID 1u
#define BK7258_YUV_H264_CPU2_CORE_ID 2u
#define BK7258_YUV_H264_H264_IRQ_BIT (1u << 14)

enum
{
  BK7258_YUV_H264_EVENT_LINE = 1,
  BK7258_YUV_H264_EVENT_FINAL,
  BK7258_YUV_H264_EVENT_ERROR,
};

struct bk7258_yuv_h264_s
{
  mutex_t api_lock;
  spinlock_t state_lock;
  sem_t event_sem;

  uint16_t width;
  uint16_t height;
  enum bk7258_yuv_h264_format_e format;
  FAR uint8_t *line_cache;
  size_t line_cache_size;
  uint32_t timeout_ms;
  uint32_t frame_bytes;
  uint32_t block_bytes;
  uint32_t block_count;

  dma_id_t dma;
  uint32_t fifo_addr;
  bool dma_allocated;
  bool dma_attempted;
  bool dma_ready;
  bool dma_started;
  bool dma_callback_ready;
  bool dma_finish_enabled;

  bool yuv_attempted;
  bool h264_attempted;
  bool yuv_ready;
  bool h264_ready;
  bool h264_route_cpu1;
  bool h264_callback_ready;
  bool yuv_callback_ready;
  bool yuv_started;
  bool h264_enabled;

  bool initialized;
  bool faulted;
  bool operation_active;
  uint32_t pending_lines;
  bool pending_final;
  bool pending_error;
  bool pending_dma_full;
  int callback_error;
  uint32_t completed_blocks;
  uint32_t next_block;
  uint32_t dma_output_capacity;
  uint64_t dma_completed_bytes;
  bool dma_full_seen;
};

static struct bk7258_yuv_h264_s g_bk7258_yuv_h264 =
{
  .api_lock = NXMUTEX_INITIALIZER,
  .state_lock = SP_UNLOCKED,
  .event_sem = SEM_INITIALIZER(0),
  .dma = DMA_ID_MAX,
};

static int bk7258_yuv_h264_sdk_error(bk_err_t error)
{
  switch (error)
    {
      case BK_OK:
        return 0;

      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
      case BK_ERR_DMA_ID:
      case BK_ERR_DMA_INVALID_ADDR:
      case BK_ERR_DMA_TRANS_LEN:
      case BK_ERR_H264_INVALID_RESOLUTION_TYPE:
      case BK_ERR_H264_INVALID_PIXEL_HEIGHT:
      case BK_ERR_H264_INVALID_CONFIG_PARAM:
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

      case BK_ERR_DMA_NOT_INIT:
      case BK_ERR_DMA_ID_NOT_INIT:
      case BK_ERR_YUV_BUF_DRIVER_NOT_INIT:
      case BK_ERR_H264_DRIVER_NOT_INIT:
        return -ENODEV;

      default:
        return -EIO;
    }
}

static int bk7258_yuv_h264_validate_range(FAR const void *data,
                                          size_t length)
{
  uint64_t base;

  if (data == NULL || length == 0)
    {
      return -EINVAL;
    }

  base = (uintptr_t)data;
  if (base > UINT32_MAX || (uint64_t)length - 1 > UINT32_MAX - base)
    {
      return -EOVERFLOW;
    }

  return 0;
}

static bool bk7258_yuv_h264_overlap(FAR const void *left, size_t left_len,
                                    FAR const void *right, size_t right_len)
{
  uint64_t left_start = (uintptr_t)left;
  uint64_t right_start = (uintptr_t)right;
  uint64_t left_end = left_start + left_len;
  uint64_t right_end = right_start + right_len;

  return left_start < right_end && right_start < left_end;
}

static int bk7258_yuv_h264_timeout(uint32_t timeout_ms, FAR clock_t *ticks)
{
  uint64_t value;

  if (ticks == NULL)
    {
      return -EINVAL;
    }

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_YUV_H264_DEFAULT_TIMEOUT_MS;
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

static int bk7258_yuv_h264_route_to_cpu1(void)
{
  int32_t ret;

  (void)sys_drv_core_intr_group2_disable(BK7258_YUV_H264_CPU2_CORE_ID,
                                         BK7258_YUV_H264_H264_IRQ_BIT);
  ret = sys_drv_core_intr_group2_enable(BK7258_YUV_H264_CPU1_CORE_ID,
                                        BK7258_YUV_H264_H264_IRQ_BIT);
  if (ret != 0)
    {
      (void)sys_drv_core_intr_group2_disable(BK7258_YUV_H264_CPU1_CORE_ID,
                                             BK7258_YUV_H264_H264_IRQ_BIT);
      return -EIO;
    }

  return 0;
}

static void bk7258_yuv_h264_route_from_cpu1(void)
{
  (void)sys_drv_core_intr_group2_disable(BK7258_YUV_H264_CPU1_CORE_ID,
                                         BK7258_YUV_H264_H264_IRQ_BIT);
}

static void bk7258_yuv_h264_event(FAR struct bk7258_yuv_h264_s *priv,
                                  uint32_t event, int error_code)
{
  irqstate_t flags;
  bool post = false;

  if (priv != &g_bk7258_yuv_h264)
    {
      return;
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  if (priv->operation_active)
    {
      switch (event)
        {
          case BK7258_YUV_H264_EVENT_LINE:
            if (priv->pending_lines != UINT32_MAX)
              {
                priv->pending_lines++;
              }
            break;

          case BK7258_YUV_H264_EVENT_FINAL:
            priv->pending_final = true;
            break;

          case BK7258_YUV_H264_EVENT_ERROR:
            if (!priv->pending_error)
              {
                priv->callback_error = error_code < 0 ? error_code : -EIO;
              }
            priv->pending_error = true;
            break;

          default:
            break;
        }
      post = true;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

  if (post)
    {
      (void)nxsem_post(&priv->event_sem);
    }
}

static void bk7258_yuv_h264_line_isr(h264_unit_t id, void *param)
{
  (void)id;
  bk7258_yuv_h264_event(param, BK7258_YUV_H264_EVENT_LINE, 0);
}

static void bk7258_yuv_h264_final_isr(h264_unit_t id, void *param)
{
  (void)id;
  bk7258_yuv_h264_event(param, BK7258_YUV_H264_EVENT_FINAL, 0);
}

static void bk7258_yuv_h264_yuv_error_isr(yuv_buf_unit_t id, void *param)
{
  (void)id;
  bk7258_yuv_h264_event(param, BK7258_YUV_H264_EVENT_ERROR, -EIO);
}

static void bk7258_yuv_h264_dma_finish_isr(dma_id_t id)
{
  irqstate_t flags;
  bool post = false;

  (void)id;
  flags = spin_lock_irqsave(&g_bk7258_yuv_h264.state_lock);
  if (g_bk7258_yuv_h264.operation_active)
    {
      /* The destination address loops only at the caller-provided capacity,
       * while each finish interrupt accounts for one complete 10 KiB DMA
       * transaction.  Saturate the arithmetic so a pathological interrupt
       * storm cannot wrap the accounting back below the capacity. */

      if (g_bk7258_yuv_h264.dma_completed_bytes <=
          UINT64_MAX - BK7258_YUV_H264_DMA_CHUNK_BYTES)
        {
          g_bk7258_yuv_h264.dma_completed_bytes +=
            BK7258_YUV_H264_DMA_CHUNK_BYTES;
        }
      else
        {
          g_bk7258_yuv_h264.dma_completed_bytes = UINT64_MAX;
        }

      if (g_bk7258_yuv_h264.dma_output_capacity != 0 &&
          g_bk7258_yuv_h264.dma_completed_bytes >=
          g_bk7258_yuv_h264.dma_output_capacity &&
          !g_bk7258_yuv_h264.pending_dma_full)
        {
          g_bk7258_yuv_h264.pending_dma_full = true;
          g_bk7258_yuv_h264.dma_full_seen = true;
          post = true;
        }
    }
  spin_unlock_irqrestore(&g_bk7258_yuv_h264.state_lock, flags);

  /* Ordinary chunk completion is deliberately not posted.  The H.264 and
   * YUV callbacks drive the line/final state machine; this event is reserved
   * for the first point at which the output capacity is exhausted. */
  if (post)
    {
      (void)nxsem_post(&g_bk7258_yuv_h264.event_sem);
    }
}

static void bk7258_yuv_h264_clear_events(
  FAR struct bk7258_yuv_h264_s *priv)
{
  while (nxsem_trywait(&priv->event_sem) == 0)
    {
    }

  {
    irqstate_t flags = spin_lock_irqsave(&priv->state_lock);
    priv->pending_lines = 0;
    priv->pending_final = false;
    priv->pending_error = false;
    priv->pending_dma_full = false;
    priv->callback_error = 0;
    priv->completed_blocks = 0;
    priv->next_block = 2;
    priv->dma_completed_bytes = 0;
    priv->dma_full_seen = false;
    spin_unlock_irqrestore(&priv->state_lock, flags);
  }
}

static void bk7258_yuv_h264_set_active(
  FAR struct bk7258_yuv_h264_s *priv, bool active)
{
  irqstate_t flags = spin_lock_irqsave(&priv->state_lock);
  priv->operation_active = active;
  spin_unlock_irqrestore(&priv->state_lock, flags);
}

static bool bk7258_yuv_h264_dma_full_seen(
  FAR struct bk7258_yuv_h264_s *priv)
{
  irqstate_t flags;
  bool full;

  flags = spin_lock_irqsave(&priv->state_lock);
  full = priv->dma_full_seen;
  spin_unlock_irqrestore(&priv->state_lock, flags);
  return full;
}

static int bk7258_yuv_h264_dma_setup(
  FAR struct bk7258_yuv_h264_s *priv,
  FAR const struct bk7258_yuv_h264_output_s *output)
{
  dma_config_t dma_config;
  uint32_t max_len;
  uint32_t length;
  uint64_t end;
  bk_err_t sdkret;

  /* A repeat DMA destination must complete whole 10 KiB transactions before
   * it wraps.  This is a deliberate board-helper contract: the output is
   * larger than one DMA transfer in normal H.264 configurations. */

  if (output->capacity > UINT32_MAX ||
      output->capacity < BK7258_YUV_H264_DMA_CHUNK_BYTES ||
      output->capacity % BK7258_YUV_H264_DMA_CHUNK_BYTES != 0)
    {
      return -EINVAL;
    }

  length = BK7258_YUV_H264_DMA_CHUNK_BYTES;

  max_len = bk_dma_get_transfer_len_max(priv->dma);
  if (max_len == 0 || BK7258_YUV_H264_DMA_CHUNK_BYTES > max_len)
    {
      return -E2BIG;
    }

  /* Check the complete caller-owned output range.  The DMA register fields
   * are 32-bit and the destination loop must not wrap outside that range. */

  end = (uint64_t)(uintptr_t)output->data + output->capacity;
  if (end > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  priv->dma_output_capacity = (uint32_t)output->capacity;
  priv->dma_completed_bytes = 0;

  if (!priv->dma_ready)
    {
      memset(&dma_config, 0, sizeof(dma_config));
      dma_config.mode = DMA_WORK_MODE_REPEAT;
      dma_config.chan_prio = 0;
      dma_config.src.dev = DMA_DEV_H264;
      dma_config.src.width = DMA_DATA_WIDTH_32BITS;
      dma_config.src.addr_inc_en = DMA_ADDR_INC_ENABLE;
      dma_config.src.addr_loop_en = DMA_ADDR_LOOP_ENABLE;
      dma_config.src.start_addr = priv->fifo_addr;
      dma_config.src.end_addr = priv->fifo_addr + 4;
      dma_config.dst.dev = DMA_DEV_DTCM;
      dma_config.dst.width = DMA_DATA_WIDTH_32BITS;
      dma_config.dst.addr_inc_en = DMA_ADDR_INC_ENABLE;
      dma_config.dst.addr_loop_en = DMA_ADDR_LOOP_ENABLE;
      dma_config.dst.start_addr = (uint32_t)(uintptr_t)output->data;
      dma_config.dst.end_addr = (uint32_t)end;

      /* bk_dma_init() performs hardware setup before validating every
       * parameter.  Retain the attempt so a failed call is still paired
       * with bk_dma_deinit() during frame cleanup. */

      priv->dma_attempted = true;
      sdkret = bk_dma_init(priv->dma, &dma_config);
      if (sdkret != BK_OK)
        {
          return bk7258_yuv_h264_sdk_error(sdkret);
        }
      priv->dma_ready = true;
    }
  else
    {
      sdkret = bk_dma_set_dest_addr(priv->dma,
                                    (uint32_t)(uintptr_t)output->data,
                                    (uint32_t)end);
      if (sdkret != BK_OK)
        {
          return bk7258_yuv_h264_sdk_error(sdkret);
        }
    }

  sdkret = bk_dma_set_transfer_len(priv->dma, length);
  if (sdkret != BK_OK)
    {
      return bk7258_yuv_h264_sdk_error(sdkret);
    }

  sdkret = bk_dma_set_src_burst_len(priv->dma, BURST_LEN_SINGLE);
  if (sdkret != BK_OK)
    {
      return bk7258_yuv_h264_sdk_error(sdkret);
    }

  sdkret = bk_dma_set_dest_burst_len(priv->dma, BURST_LEN_INC16);
  if (sdkret != BK_OK)
    {
      return bk7258_yuv_h264_sdk_error(sdkret);
    }

  sdkret = bk_dma_set_src_sec_attr(priv->dma, DMA_ATTR_SEC);
  if (sdkret != BK_OK)
    {
      return bk7258_yuv_h264_sdk_error(sdkret);
    }

  sdkret = bk_dma_set_dest_sec_attr(priv->dma, DMA_ATTR_SEC);
  if (sdkret != BK_OK)
    {
      return bk7258_yuv_h264_sdk_error(sdkret);
    }

  if (!priv->dma_finish_enabled)
    {
      sdkret = bk_dma_enable_finish_interrupt(priv->dma);
      if (sdkret != BK_OK)
        {
          return bk7258_yuv_h264_sdk_error(sdkret);
        }
      priv->dma_finish_enabled = true;
    }

  return 0;
}

static int bk7258_yuv_h264_dma_stop(
  FAR struct bk7258_yuv_h264_s *priv, bool flush)
{
  int first_error = 0;
  bk_err_t sdkret;

  if (priv->dma_started)
    {
      if (flush)
        {
          sdkret = bk_dma_flush_src_buffer(priv->dma);
          if (sdkret != BK_OK)
            {
              first_error = bk7258_yuv_h264_sdk_error(sdkret);
            }
        }

      sdkret = bk_dma_stop(priv->dma);
      if (sdkret != BK_OK && first_error == 0)
        {
          first_error = bk7258_yuv_h264_sdk_error(sdkret);
        }
      if (sdkret == BK_OK)
        {
          priv->dma_started = false;
        }
    }

  if (priv->dma_finish_enabled)
    {
      sdkret = bk_dma_disable_finish_interrupt(priv->dma);
      if (sdkret != BK_OK && first_error == 0)
        {
          first_error = bk7258_yuv_h264_sdk_error(sdkret);
        }
      if (sdkret == BK_OK)
        {
          priv->dma_finish_enabled = false;
        }
    }

  return first_error;
}

static int bk7258_yuv_h264_stop_frame(
  FAR struct bk7258_yuv_h264_s *priv, bool reset_h264,
  bool account_dma_finish)
{
  int first_error = 0;
  int ret;
  bk_err_t sdkret;

  /* For FINAL_OUT, retain the active state through stop/flush so a finish
   * IRQ already in flight can account its final complete chunk.  Error and
   * timeout paths close the callback window before touching the DMA. */

  if (!account_dma_finish)
    {
      bk7258_yuv_h264_set_active(priv, false);
    }

  ret = bk7258_yuv_h264_dma_stop(priv, true);
  bk7258_yuv_h264_set_active(priv, false);
  if (ret < 0)
    {
      first_error = ret;
    }

  /* The official pipeline resets YUV after every successful frame but
   * keeps H.264 initialized so its GOP state survives.  Error/timeout paths
   * additionally reset H.264 and leave the wrapper faulted until the owner
   * explicitly uninitializes it. */

  if (priv->yuv_ready)
    {
      sdkret = bk_yuv_buf_soft_reset();
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
    }

  if (reset_h264 && priv->h264_ready)
    {
      sdkret = bk_h264_soft_reset();
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
    }

  if (priv->dma_attempted && !priv->dma_ready)
    {
      sdkret = bk_dma_deinit(priv->dma);
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->dma_attempted = false;
          priv->dma_callback_ready = false;
          priv->dma_finish_enabled = false;
        }
    }

  return first_error;
}

static int bk7258_yuv_h264_release_stream(
  FAR struct bk7258_yuv_h264_s *priv)
{
  int first_error = 0;
  int ret;
  bool callbacks_ok;
  bk_err_t sdkret;

  bk7258_yuv_h264_set_active(priv, false);

  ret = bk7258_yuv_h264_dma_stop(priv, true);
  if (ret < 0)
    {
      first_error = ret;
    }

  if (priv->dma_attempted)
    {
      sdkret = bk_dma_deinit(priv->dma);
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->dma_attempted = false;
          priv->dma_ready = false;
          /* bk_dma_deinit() also clears the SDK callback table and resets
           * the channel interrupt state. */
          priv->dma_callback_ready = false;
          priv->dma_finish_enabled = false;
        }
    }

  if (priv->dma_callback_ready)
    {
      sdkret = bk_dma_register_isr(priv->dma, NULL, NULL);
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->dma_callback_ready = false;
        }
    }

  if (priv->h264_enabled)
    {
      sdkret = bk_h264_encode_disable();
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->h264_enabled = false;
        }
    }

  if (priv->yuv_started)
    {
      sdkret = bk_yuv_buf_stop(H264_MODE);
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->yuv_started = false;
        }
    }

  if (priv->yuv_ready)
    {
      sdkret = bk_yuv_buf_soft_reset();
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
    }

  if (priv->yuv_callback_ready)
    {
      sdkret = bk_yuv_buf_unregister_isr(YUV_BUF_H264_ERR);
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->yuv_callback_ready = false;
        }
    }

  if (priv->h264_callback_ready)
    {
      callbacks_ok = true;
      sdkret = bk_h264_unregister_isr(H264_LINE_DONE);
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret < 0)
        {
          callbacks_ok = false;
        }

      sdkret = bk_h264_unregister_isr(H264_FINAL_OUT);
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret < 0)
        {
          callbacks_ok = false;
        }
      if (callbacks_ok)
        {
          priv->h264_callback_ready = false;
        }
    }

  if (priv->h264_route_cpu1)
    {
      bk7258_yuv_h264_route_from_cpu1();
      priv->h264_route_cpu1 = false;
    }

  /* The global driver roots are shared by other board media users.  Only
   * release the per-instance YUV/H.264 configuration here; never call the
   * *_driver_deinit roots from this wrapper. */

  if (priv->h264_attempted && !priv->h264_callback_ready &&
      !priv->h264_route_cpu1)
    {
      sdkret = bk_h264_deinit();
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->h264_attempted = false;
          priv->h264_ready = false;
        }
    }

  if (priv->yuv_attempted && !priv->yuv_callback_ready)
    {
      sdkret = bk_yuv_buf_deinit();
      ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->yuv_attempted = false;
          priv->yuv_ready = false;
        }
    }

  return first_error;
}

static int bk7258_yuv_h264_drain_event(
  FAR struct bk7258_yuv_h264_s *priv, FAR bool *line,
  FAR bool *final, FAR bool *error, FAR bool *dma_full,
  FAR int *error_code)
{
  irqstate_t flags;

  *line = false;
  *final = false;
  *error = false;
  *dma_full = false;
  *error_code = 0;

  flags = spin_lock_irqsave(&priv->state_lock);
  if (priv->pending_error)
    {
      priv->pending_error = false;
      *error = true;
      *error_code = priv->callback_error;
    }
  else if (priv->pending_final)
    {
      priv->pending_final = false;
      *final = true;
    }
  else if (priv->pending_dma_full)
    {
      priv->pending_dma_full = false;
      *dma_full = true;
    }
  else if (priv->pending_lines != 0)
    {
      priv->pending_lines--;
      *line = true;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

  return *line || *final || *error || *dma_full ? 0 : -EAGAIN;
}

static int bk7258_yuv_h264_copy_next_block(
  FAR struct bk7258_yuv_h264_s *priv, FAR const uint8_t *input)
{
  uint64_t offset = (uint64_t)priv->next_block * priv->block_bytes;
  FAR uint8_t *slot;

  if (priv->next_block >= priv->block_count)
    {
      return 0;
    }

  slot = priv->line_cache +
    ((priv->next_block & 1u) != 0 ? priv->block_bytes : 0);
  memcpy(slot, input + offset, priv->block_bytes);
  priv->next_block++;
  return 0;
}

static int bk7258_yuv_h264_finish_output(
  FAR struct bk7258_yuv_h264_s *priv,
  FAR struct bk7258_yuv_h264_output_s *output,
  uint32_t encoded)
{
  irqstate_t flags;
  uint64_t completed;
  uint64_t written;
  uint32_t partial;
  uint32_t remain;

  /* dma_stop() leaves the final repeat-DMA remain count available.  A finish
   * interrupt may have completed one or more whole chunks immediately before
   * FINAL_OUT, so combine both observations under the state lock. */

  remain = bk_dma_get_remain_len(priv->dma);
  if (remain > BK7258_YUV_H264_DMA_CHUNK_BYTES)
    {
      return -EIO;
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  completed = priv->dma_completed_bytes;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  partial = BK7258_YUV_H264_DMA_CHUNK_BYTES - remain;
  if (completed > UINT64_MAX - partial)
    {
      return -EOVERFLOW;
    }
  written = completed + partial;

  if (encoded > output->capacity)
    {
      output->length = 0;
      return -ENOSPC;
    }

  if (written > output->capacity)
    {
      output->length = 0;
      return -EIO;
    }

  if (encoded == 0)
    {
      output->length = 0;
      return -EIO;
    }

  /* A final IRQ can race the last DMA finish IRQ.  The missing amount is
   * therefore allowed to be one just-completed chunk, matching the official
   * v3.1.1.9 pipeline's validation rule.  Any other mismatch is a broken
   * FIFO/DMA transaction, not a short output to be hidden. */

  if (written < encoded && encoded - written !=
      BK7258_YUV_H264_DMA_CHUNK_BYTES)
    {
      output->length = 0;
      return -EIO;
    }

  output->length = encoded;
  return 0;
}

static uint32_t bk7258_yuv_h264_encoded_bytes(void)
{
  uint32_t words = bk_h264_get_encode_count();

  if (words > UINT32_MAX / 4u)
    {
      return UINT32_MAX;
    }

  return words << 2;
}

static int bk7258_yuv_h264_init_stream(
  FAR struct bk7258_yuv_h264_s *priv)
{
  yuv_buf_config_t yuv_config;
  int ret;
  bk_err_t sdkret;

  memset(&yuv_config, 0, sizeof(yuv_config));
  yuv_config.work_mode = H264_MODE;
  yuv_config.mclk_div = YUV_MCLK_DIV_4;
  yuv_config.x_pixel = priv->width / 8;
  yuv_config.y_pixel = priv->height / 8;
  yuv_config.yuv_mode_cfg.yuv_format = (yuv_format_t)priv->format;
  yuv_config.yuv_mode_cfg.vsync = SYNC_HIGH_LEVEL;
  yuv_config.yuv_mode_cfg.hsync = SYNC_HIGH_LEVEL;
  /* The official no-sensor pipeline leaves the sensor base empty and sets
   * the encode/read base after H.264 init. */
  yuv_config.base_addr = NULL;
  yuv_config.emr_base_addr = NULL;

  priv->yuv_attempted = true;
  sdkret = bk_yuv_buf_init(&yuv_config);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }
  priv->yuv_ready = true;

  sdkret = bk_yuv_buf_enable_nosensor_encode_mode();
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }

  priv->h264_attempted = true;
  sdkret = bk_h264_init(priv->width, priv->height);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }
  priv->h264_ready = true;

  sdkret = bk_yuv_buf_set_em_base_addr(
    (uint32_t)(uintptr_t)priv->line_cache);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }

  sdkret = bk_h264_get_fifo_addr(&priv->fifo_addr);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_yuv_h264_route_to_cpu1();
  if (ret < 0)
    {
      return ret;
    }
  priv->h264_route_cpu1 = true;

  sdkret = bk_h264_register_isr(H264_LINE_DONE,
                                bk7258_yuv_h264_line_isr, priv);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }
  priv->h264_callback_ready = true;

  sdkret = bk_h264_register_isr(H264_FINAL_OUT,
                                bk7258_yuv_h264_final_isr, priv);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }

  sdkret = bk_yuv_buf_register_isr(YUV_BUF_H264_ERR,
                                   bk7258_yuv_h264_yuv_error_isr, priv);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }
  priv->yuv_callback_ready = true;

  sdkret = bk_yuv_buf_start(H264_MODE);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }
  priv->yuv_started = true;

  sdkret = bk_h264_encode_enable();
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      return ret;
    }
  priv->h264_enabled = true;
  return 0;
}

int bk7258_yuv_h264_initialize(
  FAR struct bk7258_yuv_h264_s **out,
  FAR const struct bk7258_yuv_h264_config_s *config)
{
  FAR struct bk7258_yuv_h264_s *priv = &g_bk7258_yuv_h264;
  uint64_t frame_bytes;
  uint64_t block_bytes;
  uint64_t cache_bytes;
  int ret;
  int cleanup_ret;
  bk_err_t sdkret;

  if (out == NULL || config == NULL)
    {
      return -EINVAL;
    }
  *out = NULL;

  if (config->width == 0 || config->height < 32 ||
      config->width > BK7258_YUV_H264_MAX_WIDTH ||
      config->height > BK7258_YUV_H264_MAX_HEIGHT ||
      (config->width & 7u) != 0 ||
      (config->height & (BK7258_YUV_H264_LINES_PER_BLOCK - 1u)) != 0 ||
      config->format > BK7258_YUV_H264_UVYY)
    {
      return -EINVAL;
    }

  frame_bytes = (uint64_t)config->width * config->height *
                BK7258_YUV_H264_PIXEL_BYTES;
  block_bytes = (uint64_t)config->width *
                BK7258_YUV_H264_LINES_PER_BLOCK *
                BK7258_YUV_H264_PIXEL_BYTES;
  cache_bytes = block_bytes * 2;
  if (frame_bytes > UINT32_MAX || block_bytes > UINT32_MAX ||
      cache_bytes > config->line_cache_size ||
      config->line_cache_size == 0 ||
      ((uintptr_t)config->line_cache & 3u) != 0)
    {
      return -EINVAL;
    }

  ret = bk7258_yuv_h264_validate_range(config->line_cache,
                                       config->line_cache_size);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }
  if (priv->initialized)
    {
      nxmutex_unlock(&priv->api_lock);
      return -EBUSY;
    }

  priv->width = config->width;
  priv->height = config->height;
  priv->format = config->format;
  priv->line_cache = config->line_cache;
  priv->line_cache_size = config->line_cache_size;
  priv->timeout_ms = config->timeout_ms;
  priv->frame_bytes = (uint32_t)frame_bytes;
  priv->block_bytes = (uint32_t)block_bytes;
  priv->block_count = config->height / BK7258_YUV_H264_LINES_PER_BLOCK;
  priv->faulted = false;
  priv->dma = DMA_ID_MAX;
  priv->dma_allocated = false;
  priv->dma_attempted = false;
  priv->dma_ready = false;
  priv->dma_started = false;
  priv->dma_callback_ready = false;
  priv->dma_finish_enabled = false;
  priv->dma_output_capacity = 0;
  priv->dma_completed_bytes = 0;
  priv->dma_full_seen = false;
  priv->yuv_attempted = false;
  priv->h264_attempted = false;
  priv->yuv_ready = false;
  priv->h264_ready = false;
  priv->h264_route_cpu1 = false;
  priv->h264_callback_ready = false;
  priv->yuv_callback_ready = false;
  priv->yuv_started = false;
  priv->h264_enabled = false;
  priv->operation_active = false;
  bk7258_yuv_h264_clear_events(priv);

  ret = bk7258_media_root_initialize(BK7258_MEDIA_ROOT_H264);
  if (ret < 0)
    {
      goto fail;
    }
  priv->dma = bk_dma_alloc(BK7258_YUV_H264_DMA_USER);
  if (priv->dma >= DMA_ID_MAX)
    {
      ret = -EBUSY;
      goto fail;
    }
  priv->dma_allocated = true;

  sdkret = bk_dma_register_isr(priv->dma, NULL,
                               bk7258_yuv_h264_dma_finish_isr);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }
  priv->dma_callback_ready = true;

  ret = bk7258_yuv_h264_init_stream(priv);
  if (ret < 0)
    {
      goto fail;
    }

  priv->initialized = true;
  *out = priv;
  nxmutex_unlock(&priv->api_lock);
  return 0;

fail:
  priv->initialized = false;
  cleanup_ret = bk7258_yuv_h264_release_stream(priv);
  if (ret >= 0 && cleanup_ret < 0)
    {
      ret = cleanup_ret;
    }
  if (priv->dma_allocated && !priv->dma_attempted &&
      !priv->dma_callback_ready)
    {
      sdkret = bk_dma_free(BK7258_YUV_H264_DMA_USER, priv->dma);
      cleanup_ret = bk7258_yuv_h264_sdk_error(sdkret);
      if (ret >= 0 && cleanup_ret < 0)
        {
          ret = cleanup_ret;
        }
      if (cleanup_ret >= 0)
        {
          priv->dma_allocated = false;
          priv->dma = DMA_ID_MAX;
        }
    }
  nxmutex_unlock(&priv->api_lock);
  return ret;
}

int bk7258_yuv_h264_uninitialize(FAR struct bk7258_yuv_h264_s *priv)
{
  int first_error = 0;
  int ret;
  bk_err_t sdkret;

  if (priv != &g_bk7258_yuv_h264)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }
  if (!priv->initialized)
    {
      nxmutex_unlock(&priv->api_lock);
      return -ENODEV;
    }

  {
    irqstate_t flags = spin_lock_irqsave(&priv->state_lock);
    if (priv->operation_active)
      {
        spin_unlock_irqrestore(&priv->state_lock, flags);
        nxmutex_unlock(&priv->api_lock);
        return -EBUSY;
      }
    spin_unlock_irqrestore(&priv->state_lock, flags);
  }

  ret = bk7258_yuv_h264_release_stream(priv);
  if (ret < 0)
    {
      first_error = ret;
    }

  if (priv->dma_allocated)
    {
      if (!priv->dma_attempted && !priv->dma_callback_ready)
        {
          sdkret = bk_dma_free(BK7258_YUV_H264_DMA_USER, priv->dma);
          ret = bk7258_yuv_h264_sdk_error(sdkret);
          if (ret < 0 && first_error == 0)
            {
              first_error = ret;
            }
          if (ret >= 0)
            {
              priv->dma_allocated = false;
              priv->dma = DMA_ID_MAX;
            }
        }
    }

  if (priv->dma_allocated || priv->dma_attempted ||
      priv->dma_callback_ready || priv->dma_ready ||
      priv->h264_attempted || priv->yuv_attempted ||
      priv->h264_callback_ready || priv->yuv_callback_ready ||
      priv->h264_route_cpu1 || priv->h264_enabled || priv->yuv_started)
    {
      priv->faulted = true;
      nxmutex_unlock(&priv->api_lock);
      return first_error < 0 ? first_error : -EIO;
    }

  priv->initialized = false;
  priv->faulted = false;
  nxmutex_unlock(&priv->api_lock);
  return first_error;
}

int bk7258_yuv_h264_encode(
  FAR struct bk7258_yuv_h264_s *priv,
  FAR const struct bk7258_yuv_h264_input_s *input,
  FAR struct bk7258_yuv_h264_output_s *output)
{
  clock_t ticks;
  clock_t deadline;
  clock_t now;
  clock_t remaining;
  uint32_t encoded = 0;
  int ret;
  int cleanup_ret;
  bk_err_t sdkret;
  bool line;
  bool final;
  bool error;
  bool dma_full;
  int event_error;

  if (priv != &g_bk7258_yuv_h264 || input == NULL || output == NULL ||
      output->data == NULL)
    {
      return -EINVAL;
    }
  output->length = 0;

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }
  if (!priv->initialized)
    {
      ret = -ENODEV;
      goto out;
    }
  if (priv->faulted)
    {
      ret = -EIO;
      goto out;
    }

  ret = bk7258_yuv_h264_validate_range(input->data, input->length);
  if (ret < 0)
    {
      goto out;
    }
  ret = bk7258_yuv_h264_validate_range(output->data, output->capacity);
  if (ret < 0)
    {
      goto out;
    }
  if (input->length < priv->frame_bytes ||
      bk7258_yuv_h264_overlap(input->data, priv->frame_bytes,
                              priv->line_cache, priv->line_cache_size) ||
      bk7258_yuv_h264_overlap(output->data, output->capacity,
                              input->data, input->length) ||
      bk7258_yuv_h264_overlap(output->data, output->capacity,
                              priv->line_cache, priv->line_cache_size))
    {
      ret = -EINVAL;
      goto out;
    }

  ret = bk7258_yuv_h264_timeout(priv->timeout_ms, &ticks);
  if (ret < 0)
    {
      goto out;
    }

  if (!priv->yuv_ready || !priv->h264_ready || !priv->yuv_started ||
      !priv->h264_enabled)
    {
      ret = -EIO;
      goto out;
    }

  ret = bk7258_yuv_h264_dma_setup(priv, output);
  if (ret < 0)
    {
      goto frame_fail;
    }

  memcpy(priv->line_cache, input->data, priv->block_bytes * 2);
  bk7258_yuv_h264_clear_events(priv);
  bk7258_yuv_h264_set_active(priv, true);

  sdkret = bk_dma_start(priv->dma);
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      goto frame_fail_active;
    }
  priv->dma_started = true;

  sdkret = bk_yuv_buf_rencode_start();
  ret = bk7258_yuv_h264_sdk_error(sdkret);
  if (ret < 0)
    {
      goto frame_fail_active;
    }

  /* The timeout is for the complete frame, not for each line event. */

  deadline = clock_systime_ticks() + ticks;
  for (;;)
    {
      now = clock_systime_ticks();
      remaining = deadline - now;
      if ((int32_t)remaining <= 0)
        {
          ret = bk7258_yuv_h264_dma_full_seen(priv) ? -ENOSPC :
                -ETIMEDOUT;
          goto frame_fail_active;
        }

      ret = nxsem_tickwait_uninterruptible(&priv->event_sem, remaining);
      if (ret < 0)
        {
          if (ret == -ETIMEDOUT &&
              bk7258_yuv_h264_dma_full_seen(priv))
            {
              ret = -ENOSPC;
            }
          goto frame_fail_active;
        }

      if (bk7258_yuv_h264_drain_event(priv, &line, &final, &error,
                                      &dma_full, &event_error) < 0)
        {
          continue;
        }
      if (error)
        {
          ret = event_error < 0 ? event_error : -EIO;
          goto frame_fail_active;
        }
      if (final)
        {
          ret = bk7258_yuv_h264_stop_frame(priv, false, true);
          if (ret < 0)
            {
              priv->faulted = true;
              goto out;
            }
          encoded = bk7258_yuv_h264_encoded_bytes();
          ret = bk7258_yuv_h264_finish_output(priv, output, encoded);
          goto out;
        }
      if (dma_full)
        {
          encoded = bk7258_yuv_h264_encoded_bytes();
          if (encoded > output->capacity)
            {
              ret = -ENOSPC;
              goto frame_fail_active;
            }

          /* Do not let repeat DMA wrap and overwrite a full output while the
           * final H.264 IRQ is racing this notification.  An exact-capacity
           * stream can still publish FINAL_OUT after the DMA is stopped.  If
           * no legal final arrives before the frame deadline, the sticky
           * full state maps that bounded wait to ENOSPC. */

          ret = bk7258_yuv_h264_dma_stop(priv, true);
          if (ret < 0)
            {
              goto frame_fail_active;
            }
        }
      if (line && priv->dma_started)
        {
          priv->completed_blocks++;
          if (priv->completed_blocks < priv->block_count)
            {
              /* This is intentionally rencode-then-copy.  The official
               * pipeline starts the next read before overwriting the slot
               * that the previous line event has just consumed. */

              sdkret = bk_yuv_buf_rencode_start();
              ret = bk7258_yuv_h264_sdk_error(sdkret);
              if (ret < 0)
                {
                  goto frame_fail_active;
                }
              ret = bk7258_yuv_h264_copy_next_block(priv, input->data);
              if (ret < 0)
                {
                  goto frame_fail_active;
                }
            }
        }
    }

frame_fail_active:
  bk7258_yuv_h264_set_active(priv, false);
  priv->faulted = true;
frame_fail:
  priv->faulted = true;
  cleanup_ret = bk7258_yuv_h264_stop_frame(priv, true, false);
  if (ret >= 0 && cleanup_ret < 0)
    {
      ret = cleanup_ret;
    }
  if (ret == -ETIMEDOUT)
    {
      priv->faulted = true;
    }

out:
  nxmutex_unlock(&priv->api_lock);
  return ret;
}
