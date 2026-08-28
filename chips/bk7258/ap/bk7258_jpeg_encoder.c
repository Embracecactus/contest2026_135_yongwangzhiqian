/****************************************************************************
 * board/bk7258/chip/ap/bk7258_jpeg_encoder.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP JPEG encoder typed helper.  The immutable v3.1.1.9 AP bundle
 * exports the low-level JPEG encoder, YUV buffer and DMA APIs from
 * libdriver.a.  It does not export the SDK's higher-level controller object,
 * so this file keeps the board-facing contract explicit and synchronous.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/compiler.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <common/bk_err.h>
#include <driver/dma.h>
#include <driver/jpeg_enc.h>
#include <driver/yuv_buf.h>

#include <arch/chip/bk7258_jpeg_encoder.h>
#include "bk7258_media_root.h"

#define BK7258_JPEG_ENCODER_FLEXA_LINES       8u
#define BK7258_JPEG_ENCODER_PIXEL_BYTES       2u
#define BK7258_JPEG_ENCODER_DMA_BYTES         (10u * 1024u)
#define BK7258_JPEG_ENCODER_DMA_CHANNEL       DMA_ID_8
#define BK7258_JPEG_ENCODER_TIMEOUT_TICKS     SEC2TICK(5)

/* v3.1.1.9 jpeg_driver.c enables the source in the CPU1 interrupt register,
 * while the generic SMP route may retain the old CPU2 target.  The public
 * sys_driver header is not in the immutable bundle, so these are the minimal
 * declarations and the verified BK7258 v3.1.1.9 values.  The disable API
 * returns the previous enable register, not a zero-on-success errno. */

extern int32_t sys_drv_core_intr_group1_enable(uint32_t core_id,
                                               uint32_t param);
extern int32_t sys_drv_core_intr_group1_disable(uint32_t core_id,
                                                uint32_t param);

#define BK7258_JPEGENC_CPU1_CORE_ID           1u
#define BK7258_JPEGENC_CPU2_CORE_ID           2u
#define BK7258_JPEGENC_INTERRUPT_CTRL_BIT     (1u << 25)

struct bk7258_jpeg_encoder_s
{
  mutex_t api_lock;
  spinlock_t state_lock;
  sem_t complete_sem;
  struct work_s line_work;
  struct work_s complete_work;

  FAR uint8_t *line_cache;
  uint32_t line_cache_size;
  uint16_t width;
  uint16_t height;
  yuv_format_t format;

  dma_id_t dma;
  bool dma_ready;
  bool jpeg_ready;
  bool yuv_ready;
  bool callbacks_ready;
  bool route_ready;
  bool dma_started;

  bool initialized;
  bool faulted;
  volatile bool operation_active;
  volatile bool completion_queued;
  volatile bool completion_done;
  volatile bool head_pending;
  volatile uint32_t pending_lines;
  volatile uint32_t next_block;
  volatile int callback_result;
  volatile int completion_result;

  FAR const uint8_t *input;
  uint32_t input_length;
  FAR uint8_t *output;
  uint32_t output_capacity;
  uint32_t block_bytes;
  uint32_t block_count;
};

static struct bk7258_jpeg_encoder_s g_bk7258_jpeg_encoder =
{
  .api_lock = NXMUTEX_INITIALIZER,
  .complete_sem = SEM_INITIALIZER(0),
};

static void bk7258_jpeg_encoder_line_worker(void *arg);
static void bk7258_jpeg_encoder_complete_worker(void *arg);
static void bk7258_jpeg_encoder_head_callback(jpeg_unit_t id, void *param);

static int bk7258_jpeg_encoder_sdk_error(bk_err_t error)
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

      case BK_ERR_NOT_INIT:
      case BK_ERR_JPEG_NOT_INIT:
      case BK_ERR_YUV_BUF_DRIVER_NOT_INIT:
      case BK_ERR_DMA_NOT_INIT:
      case BK_ERR_DMA_ID_NOT_INIT:
        return -ENODEV;

      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      case BK_ERR_NO_DEV:
        return -ENODEV;

      case BK_FAIL:
      default:
        return -EIO;
    }
}

static int bk7258_jpeg_encoder_check_handle(
  FAR struct bk7258_jpeg_encoder_s *priv)
{
  if (priv != &g_bk7258_jpeg_encoder)
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

static int bk7258_jpeg_encoder_validate_range(FAR const void *data,
                                              uint32_t length,
                                              bool exclusive_end)
{
  uint64_t base;
  uint64_t last;
  uint64_t end;

  if (data == NULL || length == 0)
    {
      return -EINVAL;
    }

  base = (uint64_t)(uintptr_t)data;
  if (base > UINT64_MAX - length)
    {
      return -EOVERFLOW;
    }

  last = base + length - 1;
  end = base + length;
  if (last > UINT32_MAX || (exclusive_end && end > UINT32_MAX))
    {
      return -EOVERFLOW;
    }

  return 0;
}

static bool bk7258_jpeg_encoder_overlap(FAR const void *a,
                                         uint32_t a_len,
                                         FAR const void *b,
                                         uint32_t b_len)
{
  uintptr_t a_start = (uintptr_t)a;
  uintptr_t b_start = (uintptr_t)b;
  uintptr_t a_end;
  uintptr_t b_end;

  if (a_start > UINTPTR_MAX - a_len || b_start > UINTPTR_MAX - b_len)
    {
      return true;
    }

  a_end = a_start + a_len;
  b_end = b_start + b_len;
  return a_start < b_end && b_start < a_end;
}

static int bk7258_jpeg_encoder_route_to_cpu1(void)
{
  int ret;

  (void)sys_drv_core_intr_group1_disable(BK7258_JPEGENC_CPU2_CORE_ID,
                                         BK7258_JPEGENC_INTERRUPT_CTRL_BIT);

  ret = sys_drv_core_intr_group1_enable(BK7258_JPEGENC_CPU1_CORE_ID,
                                        BK7258_JPEGENC_INTERRUPT_CTRL_BIT);
  if (ret != 0)
    {
      (void)sys_drv_core_intr_group1_disable(
        BK7258_JPEGENC_CPU1_CORE_ID, BK7258_JPEGENC_INTERRUPT_CTRL_BIT);
      return -EIO;
    }

  return 0;
}

static void bk7258_jpeg_encoder_route_from_cpu1(void)
{
  (void)sys_drv_core_intr_group1_disable(BK7258_JPEGENC_CPU1_CORE_ID,
                                         BK7258_JPEGENC_INTERRUPT_CTRL_BIT);
}

static bool bk7258_jpeg_encoder_active(FAR struct bk7258_jpeg_encoder_s *priv)
{
  irqstate_t flags;
  bool active;

  flags = spin_lock_irqsave(&priv->state_lock);
  active = priv->operation_active;
  spin_unlock_irqrestore(&priv->state_lock, flags);
  return active;
}

static void bk7258_jpeg_encoder_fail_from_callback(
  FAR struct bk7258_jpeg_encoder_s *priv, int error)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->state_lock);
  if (priv->callback_result == 0)
    {
      priv->callback_result = error;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);
}

static int bk7258_jpeg_encoder_schedule_complete(
  FAR struct bk7258_jpeg_encoder_s *priv)
{
  irqstate_t flags;
  bool queue;
  int ret;

  flags = spin_lock_irqsave(&priv->state_lock);
  queue = priv->operation_active && !priv->completion_queued;
  if (queue)
    {
      priv->completion_queued = true;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

  if (!queue)
    {
      return 0;
    }

  ret = work_queue(HPWORK, &priv->complete_work,
                   (worker_t)bk7258_jpeg_encoder_complete_worker,
                   priv, 0);
  if (ret < 0)
    {
      /* A callback cannot perform the blocking hardware cleanup.  Wake the
       * synchronous caller, which owns the fallback stop path. */
      flags = spin_lock_irqsave(&priv->state_lock);
      priv->completion_queued = false;
      priv->completion_result = -EIO;
      spin_unlock_irqrestore(&priv->state_lock, flags);
      (void)nxsem_post(&priv->complete_sem);
    }

  return ret;
}

static void bk7258_jpeg_encoder_line_callback(jpeg_unit_t id, void *param)
{
  FAR struct bk7258_jpeg_encoder_s *priv = param;
  irqstate_t flags;
  bool queue;
  int ret;

  (void)id;
  if (priv == NULL)
    {
      return;
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  queue = priv->operation_active && priv->callback_result == 0;
  if (queue)
    {
      if (priv->pending_lines != UINT32_MAX)
        {
          priv->pending_lines++;
        }
      else
        {
          queue = false;
          priv->callback_result = -EOVERFLOW;
        }
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

  if (!queue)
    {
      (void)bk7258_jpeg_encoder_schedule_complete(priv);
      return;
    }

  ret = work_queue(HPWORK, &priv->line_work,
                   (worker_t)bk7258_jpeg_encoder_line_worker, priv, 0);
  if (ret < 0 && ret != -EBUSY)
    {
      bk7258_jpeg_encoder_fail_from_callback(priv, -EIO);
      (void)bk7258_jpeg_encoder_schedule_complete(priv);
    }
}

static void bk7258_jpeg_encoder_head_callback(jpeg_unit_t id, void *param)
{
  FAR struct bk7258_jpeg_encoder_s *priv = param;
  irqstate_t flags;
  int ret;

  (void)id;
  if (priv == NULL)
    {
      return;
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  if (priv->operation_active && priv->callback_result == 0)
    {
      priv->head_pending = true;
    }
  else
    {
      spin_unlock_irqrestore(&priv->state_lock, flags);
      return;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

  /* The official controller calls bk_yuv_buf_rencode_start() from this ISR.
   * Defer it to the existing HPWORK bridge so this wrapper keeps all SDK
   * calls out of the vendor callback context. */
  ret = work_queue(HPWORK, &priv->line_work,
                   (worker_t)bk7258_jpeg_encoder_line_worker, priv, 0);
  if (ret < 0 && ret != -EBUSY)
    {
      bk7258_jpeg_encoder_fail_from_callback(priv, -EIO);
      (void)bk7258_jpeg_encoder_schedule_complete(priv);
    }
}

static void bk7258_jpeg_encoder_eof_callback(jpeg_unit_t id, void *param)
{
  FAR struct bk7258_jpeg_encoder_s *priv = param;

  (void)id;
  if (priv != NULL)
    {
      (void)bk7258_jpeg_encoder_schedule_complete(priv);
    }
}

static void bk7258_jpeg_encoder_error_callback(jpeg_unit_t id, void *param)
{
  FAR struct bk7258_jpeg_encoder_s *priv = param;

  (void)id;
  if (priv != NULL)
    {
      bk7258_jpeg_encoder_fail_from_callback(priv, -EIO);
      (void)bk7258_jpeg_encoder_schedule_complete(priv);
    }
}

static void bk7258_jpeg_encoder_line_worker(void *arg)
{
  FAR struct bk7258_jpeg_encoder_s *priv = arg;
  irqstate_t flags;
  bool requeue;

  for (;;)
    {
      uint32_t block;
      bool head;
      FAR const uint8_t *src;
      FAR uint8_t *dst;
      bk_err_t sdkret;

      flags = spin_lock_irqsave(&priv->state_lock);
      if (!priv->operation_active ||
          (!priv->head_pending && priv->pending_lines == 0))
        {
          spin_unlock_irqrestore(&priv->state_lock, flags);
          break;
        }

      head = priv->head_pending;
      if (head)
        {
          priv->head_pending = false;
          block = 0;
        }
      else
        {
          priv->pending_lines--;
          block = priv->next_block++;
        }
      spin_unlock_irqrestore(&priv->state_lock, flags);

      if (head)
        {
          sdkret = bk_yuv_buf_rencode_start();
          if (sdkret != BK_OK)
            {
              bk7258_jpeg_encoder_fail_from_callback(priv,
                bk7258_jpeg_encoder_sdk_error(sdkret));
              (void)bk7258_jpeg_encoder_schedule_complete(priv);
              break;
            }
          continue;
        }

      if (block < priv->block_count)
        {
          src = priv->input + (size_t)block * priv->block_bytes;
          dst = priv->line_cache + (block & 1u) * priv->block_bytes;
          memcpy(dst, src, priv->block_bytes);
        }

      if (!bk7258_jpeg_encoder_active(priv))
        {
          break;
        }

      /* The line-clear interrupt releases the next already-buffered
       * 8-line block.  Even when there is no later block to refill, the
       * final buffered block still needs one re-encode trigger before JPEG
       * can raise EOF. */

      if (block > priv->block_count)
        {
          continue;
        }

      sdkret = bk_yuv_buf_rencode_start();
      if (sdkret != BK_OK)
        {
          bk7258_jpeg_encoder_fail_from_callback(priv,
            bk7258_jpeg_encoder_sdk_error(sdkret));
          (void)bk7258_jpeg_encoder_schedule_complete(priv);
          break;
        }
    }

  /* A callback can arrive while this worker is draining.  Requeue after the
   * final check so an EBUSY result from that callback cannot lose a line. */
  flags = spin_lock_irqsave(&priv->state_lock);
  requeue = priv->operation_active &&
            (priv->head_pending || priv->pending_lines != 0);
  spin_unlock_irqrestore(&priv->state_lock, flags);
  if (requeue)
    {
      (void)work_queue(HPWORK, &priv->line_work,
                       (worker_t)bk7258_jpeg_encoder_line_worker, priv, 0);
    }
}

static void bk7258_jpeg_encoder_complete_worker(void *arg)
{
  FAR struct bk7258_jpeg_encoder_s *priv = arg;
  irqstate_t flags;
  uint32_t frame_size = 0;
  int result = 0;
  int callback_result;
  bk_err_t sdkret;

  if (!bk7258_jpeg_encoder_active(priv))
    {
      return;
    }

  sdkret = bk_yuv_buf_stop(JPEG_MODE);
  if (sdkret != BK_OK)
    {
      result = bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_yuv_buf_soft_reset();
  if (sdkret != BK_OK && result == 0)
    {
      result = bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  if (priv->dma_ready)
    {
      sdkret = bk_dma_flush_src_buffer(priv->dma);
      if (sdkret != BK_OK && result == 0)
        {
          result = bk7258_jpeg_encoder_sdk_error(sdkret);
        }
    }

  frame_size = bk_jpeg_enc_get_frame_size();
  if (priv->dma_started)
    {
      sdkret = bk_dma_stop(priv->dma);
      if (sdkret != BK_OK && result == 0)
        {
          result = bk7258_jpeg_encoder_sdk_error(sdkret);
        }
      priv->dma_started = false;
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  callback_result = priv->callback_result;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  if (callback_result != 0 && result == 0)
    {
      result = callback_result;
    }

  if (result == 0 && frame_size > priv->output_capacity)
    {
      result = -ENOSPC;
    }

  if (result != 0)
    {
      /* The official controller soft-resets JPEG before allowing a later
       * frame after any encode error.  Preserve the original error if the
       * recovery reset also fails, but fail closed on the next call. */
      sdkret = bk_jpeg_enc_soft_reset();
      if (sdkret != BK_OK)
        {
          priv->faulted = true;
        }
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->completion_result = result;
  priv->completion_done = true;
  priv->completion_queued = false;
  spin_unlock_irqrestore(&priv->state_lock, flags);
  (void)nxsem_post(&priv->complete_sem);
}

static int bk7258_jpeg_encoder_dma_config(
  FAR struct bk7258_jpeg_encoder_s *priv)
{
  dma_config_t config = {0};
  uint32_t fifo_addr;
  uint64_t cache_end;
  bk_err_t sdkret;

  sdkret = bk_jpeg_enc_get_fifo_addr(&fifo_addr);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  cache_end = (uint64_t)(uintptr_t)priv->line_cache +
              priv->line_cache_size;
  config.mode = DMA_WORK_MODE_REPEAT;
  config.chan_prio = 0;
  config.src.dev = DMA_DEV_JPEG;
  config.src.width = DMA_DATA_WIDTH_32BITS;
  config.src.addr_inc_en = DMA_ADDR_INC_ENABLE;
  config.src.addr_loop_en = DMA_ADDR_LOOP_ENABLE;
  config.src.start_addr = fifo_addr;
  config.dst.dev = DMA_DEV_DTCM;
  config.dst.width = DMA_DATA_WIDTH_32BITS;
  config.dst.addr_inc_en = DMA_ADDR_INC_ENABLE;
  config.dst.addr_loop_en = DMA_ADDR_LOOP_ENABLE;
  config.dst.start_addr = (uint32_t)(uintptr_t)priv->line_cache;
  config.dst.end_addr = (uint32_t)cache_end;

  sdkret = bk_dma_init(priv->dma, &config);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  priv->dma_ready = true;
  sdkret = bk_dma_set_transfer_len(priv->dma,
                                   BK7258_JPEG_ENCODER_DMA_BYTES);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  /* Match the v3.1.1.9 secure-domain JPEG controller.  Without these
   * attributes BK7258 accepts the channel setup but drains only one FIFO
   * word, leaving an apparently valid JPEG frame length with no payload. */

  sdkret = bk_dma_set_src_burst_len(priv->dma, BURST_LEN_SINGLE);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_dma_set_dest_burst_len(priv->dma, BURST_LEN_INC16);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_dma_set_src_sec_attr(priv->dma, DMA_ATTR_SEC);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_dma_set_dest_sec_attr(priv->dma, DMA_ATTR_SEC);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  return 0;
}

static int bk7258_jpeg_encoder_register_callbacks(
  FAR struct bk7258_jpeg_encoder_s *priv)
{
  bk_err_t sdkret;

  /* Mark the set live before the first registration so every partial setup
   * failure takes the same unregister path. */
  priv->callbacks_ready = true;

  sdkret = bk_jpeg_enc_register_isr(JPEG_HEAD_OUTPUT,
                                    bk7258_jpeg_encoder_head_callback, priv);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_jpeg_enc_register_isr(JPEG_LINE_CLEAR,
                                    bk7258_jpeg_encoder_line_callback, priv);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_jpeg_enc_register_isr(JPEG_EOF,
                                    bk7258_jpeg_encoder_eof_callback, priv);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_jpeg_enc_register_isr(JPEG_FRAME_ERR,
                                    bk7258_jpeg_encoder_error_callback, priv);
  if (sdkret != BK_OK)
    {
      return bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  return 0;
}

static void bk7258_jpeg_encoder_unregister_callbacks(
  FAR struct bk7258_jpeg_encoder_s *priv)
{
  if (priv->callbacks_ready)
    {
      (void)bk_jpeg_enc_unregister_isr(JPEG_HEAD_OUTPUT);
      (void)bk_jpeg_enc_unregister_isr(JPEG_LINE_CLEAR);
      (void)bk_jpeg_enc_unregister_isr(JPEG_EOF);
      (void)bk_jpeg_enc_unregister_isr(JPEG_FRAME_ERR);
      priv->callbacks_ready = false;
    }
}

static void bk7258_jpeg_encoder_cleanup_failed(
  FAR struct bk7258_jpeg_encoder_s *priv)
{
  if (priv->dma_started)
    {
      (void)bk_dma_stop(priv->dma);
      priv->dma_started = false;
    }

  work_cancel_sync(HPWORK, &priv->line_work);
  work_cancel_sync(HPWORK, &priv->complete_work);
  priv->operation_active = false;
  bk7258_jpeg_encoder_unregister_callbacks(priv);

  if (priv->jpeg_ready)
    {
      (void)bk_jpeg_enc_deinit();
      priv->jpeg_ready = false;
    }

  if (priv->yuv_ready)
    {
      (void)bk_yuv_buf_deinit();
      priv->yuv_ready = false;
    }

  if (priv->route_ready)
    {
      bk7258_jpeg_encoder_route_from_cpu1();
      priv->route_ready = false;
    }

  if (priv->dma_ready)
    {
      (void)bk_dma_deinit(priv->dma);
      priv->dma_ready = false;
    }

  if (priv->dma != DMA_ID_MAX)
    {
      (void)bk_dma_free(DMA_DEV_JPEG, priv->dma);
      priv->dma = DMA_ID_MAX;
    }

}

static int bk7258_jpeg_encoder_abort_locked(
  FAR struct bk7258_jpeg_encoder_s *priv, int reason)
{
  irqstate_t flags;
  int first_error = 0;
  bk_err_t sdkret;

  /* Stop workers before touching hardware so no deferred line callback can
   * race the timeout recovery sequence. */
  flags = spin_lock_irqsave(&priv->state_lock);
  priv->operation_active = false;
  priv->completion_queued = false;
  priv->head_pending = false;
  priv->pending_lines = 0;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  work_cancel_sync(HPWORK, &priv->line_work);
  work_cancel_sync(HPWORK, &priv->complete_work);

  sdkret = bk_yuv_buf_stop(JPEG_MODE);
  if (sdkret != BK_OK && first_error == 0)
    {
      first_error = bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_yuv_buf_soft_reset();
  if (sdkret != BK_OK && first_error == 0)
    {
      first_error = bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  sdkret = bk_jpeg_enc_soft_reset();
  if (sdkret != BK_OK && first_error == 0)
    {
      first_error = bk7258_jpeg_encoder_sdk_error(sdkret);
    }

  if (priv->dma_started)
    {
      sdkret = bk_dma_stop(priv->dma);
      if (sdkret != BK_OK && first_error == 0)
        {
          first_error = bk7258_jpeg_encoder_sdk_error(sdkret);
        }
      priv->dma_started = false;
    }

  if (first_error < 0)
    {
      priv->faulted = true;
    }

  /* A lost IRQ must always be reported as a timeout; cleanup failures make
   * subsequent calls fail-closed through faulted, but do not hide it. */
  if (reason < 0)
    {
      return reason;
    }

  return first_error;
}

int bk7258_jpeg_encoder_initialize(
  FAR struct bk7258_jpeg_encoder_s **out,
  FAR const struct bk7258_jpeg_encoder_config_s *config)
{
  FAR struct bk7258_jpeg_encoder_s *priv = &g_bk7258_jpeg_encoder;
  yuv_buf_config_t yuv_config = {0};
  jpeg_config_t jpeg_config = {0};
  uint64_t frame_bytes;
  uint64_t block_bytes;
  int ret;
  bk_err_t sdkret;

  if (out == NULL || config == NULL)
    {
      return -EINVAL;
    }

  *out = NULL;
  if (config->width == 0 || config->height == 0 ||
      (config->width % BK7258_JPEG_ENCODER_FLEXA_LINES) != 0 ||
      (config->height % BK7258_JPEG_ENCODER_FLEXA_LINES) != 0 ||
      config->format > BK7258_JPEG_ENCODER_UVYY)
    {
      return -EINVAL;
    }

  frame_bytes = (uint64_t)config->width * config->height *
                BK7258_JPEG_ENCODER_PIXEL_BYTES;
  block_bytes = (uint64_t)config->width *
                BK7258_JPEG_ENCODER_FLEXA_LINES *
                BK7258_JPEG_ENCODER_PIXEL_BYTES;
  if (frame_bytes > UINT32_MAX || block_bytes > UINT32_MAX ||
      block_bytes * 2 > config->line_cache_size ||
      config->line_cache_size == 0 ||
      ((uintptr_t)config->line_cache & 3u) != 0)
    {
      return -EINVAL;
    }

  ret = bk7258_jpeg_encoder_validate_range(config->line_cache,
                                           config->line_cache_size, true);
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

  priv->faulted = false;
  priv->dma_ready = false;
  priv->jpeg_ready = false;
  priv->yuv_ready = false;
  priv->callbacks_ready = false;
  priv->route_ready = false;
  priv->dma_started = false;
  priv->operation_active = false;
  priv->completion_queued = false;
  priv->completion_done = false;
  priv->head_pending = false;
  priv->pending_lines = 0;
  priv->callback_result = 0;
  priv->completion_result = -EIO;
  priv->input = NULL;
  priv->output = NULL;
  priv->input_length = 0;
  priv->output_capacity = 0;
  priv->line_work = (struct work_s){0};
  priv->complete_work = (struct work_s){0};
  spin_lock_init(&priv->state_lock);
  while (nxsem_trywait(&priv->complete_sem) == 0)
    {
    }
  priv->dma = DMA_ID_MAX;
  priv->width = config->width;
  priv->height = config->height;
  priv->format = (yuv_format_t)config->format;
  priv->line_cache = config->line_cache;
  priv->line_cache_size = config->line_cache_size;
  priv->block_bytes = (uint32_t)block_bytes;
  priv->block_count = config->height / BK7258_JPEG_ENCODER_FLEXA_LINES;

  ret = bk7258_media_root_initialize(BK7258_MEDIA_ROOT_JPEG);
  if (ret < 0)
    {
      goto fail;
    }

  priv->dma = bk_fixed_dma_alloc(DMA_DEV_JPEG, BK7258_JPEG_ENCODER_DMA_CHANNEL);
  if (priv->dma == DMA_ID_MAX)
    {
      ret = -EBUSY;
      goto fail;
    }

  yuv_config.work_mode = JPEG_MODE;
  yuv_config.mclk_div = YUV_MCLK_DIV_4;
  yuv_config.x_pixel = config->width / BK7258_JPEG_ENCODER_FLEXA_LINES;
  yuv_config.y_pixel = config->height / BK7258_JPEG_ENCODER_FLEXA_LINES;
  yuv_config.yuv_mode_cfg.yuv_format = priv->format;
  yuv_config.yuv_mode_cfg.vsync = SYNC_HIGH_LEVEL;
  yuv_config.yuv_mode_cfg.hsync = SYNC_HIGH_LEVEL;
  yuv_config.base_addr = config->line_cache;
  yuv_config.emr_base_addr = config->line_cache;
  sdkret = bk_yuv_buf_init(&yuv_config);
  ret = bk7258_jpeg_encoder_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }
  priv->yuv_ready = true;

  sdkret = bk_yuv_buf_enable_nosensor_encode_mode();
  ret = bk7258_jpeg_encoder_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }

  jpeg_config.mode = JPEG_MODE;
  jpeg_config.sensor_fmt = priv->format;
  jpeg_config.x_pixel = yuv_config.x_pixel;
  jpeg_config.y_pixel = yuv_config.y_pixel;
  sdkret = bk_jpeg_enc_init(&jpeg_config);
  ret = bk7258_jpeg_encoder_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }
  priv->jpeg_ready = true;

  sdkret = bk_jpeg_enc_yuv_fmt_sel(priv->format);
  ret = bk7258_jpeg_encoder_sdk_error(sdkret);
  if (ret < 0)
    {
      goto fail;
    }

  ret = bk7258_jpeg_encoder_route_to_cpu1();
  if (ret < 0)
    {
      goto fail;
    }
  priv->route_ready = true;

  ret = bk7258_jpeg_encoder_dma_config(priv);
  if (ret < 0)
    {
      goto fail;
    }

  ret = bk7258_jpeg_encoder_register_callbacks(priv);
  if (ret < 0)
    {
      goto fail;
    }

  priv->initialized = true;
  *out = priv;
  nxmutex_unlock(&priv->api_lock);
  return 0;

  fail:
  bk7258_jpeg_encoder_cleanup_failed(priv);
  priv->initialized = false;
  priv->faulted = false;
  priv->line_cache = NULL;
  priv->line_cache_size = 0;
  priv->input = NULL;
  priv->output = NULL;
  priv->input_length = 0;
  priv->output_capacity = 0;
  priv->dma = DMA_ID_MAX;
  nxmutex_unlock(&priv->api_lock);
  return ret;
}

int bk7258_jpeg_encoder_uninitialize(
  FAR struct bk7258_jpeg_encoder_s *priv)
{
  int ret;
  int first_error = 0;
  bk_err_t sdkret;

  if (priv != &g_bk7258_jpeg_encoder)
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
      return -EINVAL;
    }

  if (priv->operation_active)
    {
      nxmutex_unlock(&priv->api_lock);
      return -EBUSY;
    }

  priv->operation_active = false;
  bk7258_jpeg_encoder_unregister_callbacks(priv);
  work_cancel_sync(HPWORK, &priv->line_work);
  work_cancel_sync(HPWORK, &priv->complete_work);

  if (priv->dma_started)
    {
      sdkret = bk_dma_stop(priv->dma);
      ret = bk7258_jpeg_encoder_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      priv->dma_started = false;
    }

  if (priv->jpeg_ready)
    {
      sdkret = bk_jpeg_enc_deinit();
      ret = bk7258_jpeg_encoder_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->jpeg_ready = false;
        }
    }

  if (priv->yuv_ready)
    {
      sdkret = bk_yuv_buf_deinit();
      ret = bk7258_jpeg_encoder_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->yuv_ready = false;
        }
    }

  if (priv->route_ready)
    {
      bk7258_jpeg_encoder_route_from_cpu1();
      priv->route_ready = false;
    }

  if (priv->dma_ready)
    {
      sdkret = bk_dma_deinit(priv->dma);
      ret = bk7258_jpeg_encoder_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->dma_ready = false;
        }
    }

  if (!priv->dma_ready && priv->dma != DMA_ID_MAX)
    {
      sdkret = bk_dma_free(DMA_DEV_JPEG, priv->dma);
      ret = bk7258_jpeg_encoder_sdk_error(sdkret);
      if (ret < 0 && first_error == 0)
        {
          first_error = ret;
        }
      if (ret >= 0)
        {
          priv->dma = DMA_ID_MAX;
        }
    }

  if (priv->yuv_ready || priv->jpeg_ready || priv->dma_ready)
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

int bk7258_jpeg_encoder_set_compression(
  FAR struct bk7258_jpeg_encoder_s *priv,
  uint32_t min_size,
  uint32_t max_size)
{
  int ret;
  bk_err_t sdkret;

  if (priv != &g_bk7258_jpeg_encoder || min_size == 0 ||
      min_size >= max_size || max_size > UINT16_MAX)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_encoder_check_handle(priv);
  if (ret < 0 || priv->operation_active)
    {
      if (ret >= 0)
        {
          ret = -EBUSY;
        }
      nxmutex_unlock(&priv->api_lock);
      return ret;
    }

  sdkret = bk_jpeg_enc_encode_config(1, max_size, min_size);
  ret = bk7258_jpeg_encoder_sdk_error(sdkret);
  nxmutex_unlock(&priv->api_lock);
  return ret;
}

int bk7258_jpeg_encoder_encode(
  FAR struct bk7258_jpeg_encoder_s *priv,
  FAR const struct bk7258_jpeg_encoder_input_s *input,
  FAR struct bk7258_jpeg_encoder_output_s *output)
{
  uint64_t frame_bytes;
  uint32_t first_bytes;
  uint32_t second_bytes;
  irqstate_t flags;
  bk_err_t sdkret;
  int ret;

  if (priv != &g_bk7258_jpeg_encoder || input == NULL || output == NULL ||
      input->data == NULL || output->data == NULL || output->capacity == 0 ||
      (output->capacity & 3u) != 0 ||
      ((uintptr_t)output->data & 3u) != 0)
    {
      return -EINVAL;
    }

  frame_bytes = (uint64_t)priv->width * priv->height *
                BK7258_JPEG_ENCODER_PIXEL_BYTES;
  if (input->length < frame_bytes)
    {
      return -EINVAL;
    }

  ret = bk7258_jpeg_encoder_validate_range(input->data, input->length,
                                           false);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_encoder_validate_range(output->data, output->capacity,
                                           true);
  if (ret < 0)
    {
      return ret;
    }

  if (bk7258_jpeg_encoder_overlap(input->data, (uint32_t)frame_bytes,
                                  output->data, output->capacity) ||
      bk7258_jpeg_encoder_overlap(input->data, (uint32_t)frame_bytes,
                                  priv->line_cache, priv->line_cache_size) ||
      bk7258_jpeg_encoder_overlap(output->data, output->capacity,
                                  priv->line_cache, priv->line_cache_size))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_encoder_check_handle(priv);
  if (ret < 0)
    {
      goto unlock;
    }

  if (priv->operation_active)
    {
      ret = -EBUSY;
      goto unlock;
    }

  first_bytes = priv->block_bytes;
  second_bytes = priv->block_count > 1 ? priv->block_bytes : 0;
  memcpy(priv->line_cache, input->data, first_bytes);
  if (second_bytes != 0)
    {
      memcpy(priv->line_cache + priv->block_bytes,
             input->data + priv->block_bytes, second_bytes);
    }

  sdkret = bk_dma_set_dest_addr(priv->dma,
                                (uint32_t)(uintptr_t)output->data,
                                (uint32_t)((uintptr_t)output->data +
                                           output->capacity));
  ret = bk7258_jpeg_encoder_sdk_error(sdkret);
  if (ret < 0)
    {
      goto unlock;
    }

  while (nxsem_trywait(&priv->complete_sem) == 0)
    {
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->input = input->data;
  priv->input_length = (uint32_t)frame_bytes;
  priv->output = output->data;
  priv->output_capacity = output->capacity;
  priv->next_block = priv->block_count > 2 ? 2 : priv->block_count;
  priv->pending_lines = 0;
  priv->callback_result = 0;
  priv->completion_result = -EIO;
  priv->completion_done = false;
  priv->head_pending = false;
  priv->completion_queued = false;
  priv->operation_active = true;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  sdkret = bk_dma_start(priv->dma);
  ret = bk7258_jpeg_encoder_sdk_error(sdkret);
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&priv->state_lock);
      priv->operation_active = false;
      spin_unlock_irqrestore(&priv->state_lock, flags);
      goto unlock;
    }
  priv->dma_started = true;

  sdkret = bk_yuv_buf_start(JPEG_MODE);
  ret = bk7258_jpeg_encoder_sdk_error(sdkret);
  if (ret < 0)
    {
      (void)bk_dma_stop(priv->dma);
      priv->dma_started = false;
      flags = spin_lock_irqsave(&priv->state_lock);
      priv->operation_active = false;
      spin_unlock_irqrestore(&priv->state_lock, flags);
      goto unlock;
    }

  ret = nxsem_tickwait_uninterruptible(&priv->complete_sem,
                                       BK7258_JPEG_ENCODER_TIMEOUT_TICKS);
  if (ret < 0)
    {
      ret = bk7258_jpeg_encoder_abort_locked(priv, ret);
      goto unlock;
    }

  work_cancel_sync(HPWORK, &priv->line_work);
  flags = spin_lock_irqsave(&priv->state_lock);
  ret = priv->completion_result;
  bool completion_done = priv->completion_done;
  if (!completion_done && ret >= 0)
    {
      ret = -EIO;
    }
  priv->operation_active = false;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  if (!completion_done)
    {
      /* The completion work could not be queued.  Its synchronous caller
       * owns the only safe fallback stop and reset path. */
      ret = bk7258_jpeg_encoder_abort_locked(priv, -EIO);
    }

  if (ret == 0)
    {
      output->length = bk_jpeg_enc_get_frame_size();
      if (output->length == 0)
        {
          ret = -EIO;
        }
    }

unlock:
  nxmutex_unlock(&priv->api_lock);
  return ret;
}
