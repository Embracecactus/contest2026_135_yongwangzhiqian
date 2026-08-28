/****************************************************************************
 * chips/bk7258/ap/bk7258_dma.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 general-purpose DMA (GDMA) character device.
 *
 * The AP SDK owns the channel state machine (bk_dma_alloc/init/start/stop/
 * free); NuttX has no generic DMA framework, so this wrapper publishes a
 * single memory-to-memory transfer engine as /dev/dma0:
 *
 *   - open:  bk_dma_driver_init() + bk_dma_alloc(private user token)
 *   - ioctl: BKIOC_DMA_TRANSFER (synchronous mem-to-mem copy)
 *            BKIOC_DMA_GET_STATUS
 *   - close: bk_dma_free() + bk_dma_driver_deinit()
 *
 * The SDK channel user token is private to this driver, so existing in-tree
 * owners (AUD/MIC/JPEG/H264) are never disturbed.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_DMA

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <driver/dma.h>

#include <arch/chip/bk7258_dma.h>

#define BK7258_DMA_USER          DMA_DEV_LA
#define BK7258_DMA_POLL_DELAY_MS 1u
#define BK7258_DMA_POLL_MAX_MS   2000u

struct bk7258_dma_priv_s
{
  mutex_t lock;
  dma_id_t dma_id;
  bool inited;
  bool opened;
};

static struct bk7258_dma_priv_s g_bk7258_dma =
{
  .lock   = NXMUTEX_INITIALIZER,
  .dma_id = DMA_ID_MAX,
  .inited = false,
  .opened = false,
};

static int bk7258_dma_open(FAR struct file *filep);
static int bk7258_dma_close(FAR struct file *filep);
static int bk7258_dma_ioctl(FAR struct file *filep, int cmd,
                            unsigned long arg);

static const struct file_operations g_bk7258_dma_fops =
{
  .open  = bk7258_dma_open,
  .close = bk7258_dma_close,
  .ioctl = bk7258_dma_ioctl,
};

static int bk7258_dma_errno(bk_err_t sdkret)
{
  switch (sdkret)
    {
      case BK_OK:
        return OK;

      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
      case BK_ERR_DMA_INVALID_ADDR:
      case BK_ERR_DMA_TRANS_LEN:
        return -EINVAL;

      case BK_ERR_DMA_ID:
        return -ENODEV;

      case BK_ERR_DMA_NOT_INIT:
      case BK_ERR_DMA_ID_NOT_INIT:
        return -ENXIO;

      case BK_ERR_DMA_ID_REINIT:
        return -EBUSY;

      default:
        return -EIO;
    }
}

static int bk7258_dma_open(FAR struct file *filep)
{
  FAR struct bk7258_dma_priv_s *priv = &g_bk7258_dma;
  dma_id_t id;
  bk_err_t sdkret;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  if (priv->opened)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  sdkret = bk_dma_driver_init();
  if (sdkret != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return bk7258_dma_errno(sdkret);
    }

  id = bk_dma_alloc(BK7258_DMA_USER);
  if (id >= DMA_ID_MAX)
    {
      (void)bk_dma_driver_deinit();
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  priv->dma_id = id;
  priv->opened = true;

  syslog(LOG_INFO, "BK7258 DMA: open %s channel=%d\n",
         BK7258_DMA_DEVPATH, (int)id);

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_dma_close(FAR struct file *filep)
{
  FAR struct bk7258_dma_priv_s *priv = &g_bk7258_dma;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->opened && priv->dma_id < DMA_ID_MAX)
    {
      (void)bk_dma_stop(priv->dma_id);
      (void)bk_dma_free(BK7258_DMA_USER, priv->dma_id);
      (void)bk_dma_driver_deinit();
    }

  priv->dma_id = DMA_ID_MAX;
  priv->opened = false;

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_dma_transfer(FAR struct bk7258_dma_priv_s *priv,
                               FAR const struct bk7258_dma_xfer_s *xfer)
{
  dma_data_width_t width;
  dma_config_t config;
  uint32_t timeout;
  bk_err_t sdkret;

  if (xfer == NULL || xfer->length == 0 ||
      xfer->length > BK7258_DMA_MAX_TRANSFER)
    {
      return -EINVAL;
    }

  switch (xfer->width)
    {
      case 8:
        width = DMA_DATA_WIDTH_8BITS;
        break;

      case 16:
        width = DMA_DATA_WIDTH_16BITS;
        break;

      case 32:
        width = DMA_DATA_WIDTH_32BITS;
        break;

      default:
        return -EINVAL;
    }

  if ((xfer->length % (xfer->width / 8u)) != 0)
    {
      return -EINVAL;
    }

  memset(&config, 0, sizeof(config));
  config.mode                        = DMA_WORK_MODE_SINGLE;
  config.chan_prio                   = 0;
  config.src.dev                     = DMA_DEV_LA;
  config.src.width                   = width;
  config.src.addr_inc_en             = DMA_ADDR_INC_ENABLE;
  config.src.addr_loop_en            = DMA_ADDR_LOOP_DISABLE;
  config.src.start_addr              = xfer->src_addr;
  config.src.end_addr                = xfer->src_addr + xfer->length - 1;
  config.dst.dev                     = DMA_DEV_LA;
  config.dst.width                   = width;
  config.dst.addr_inc_en             = DMA_ADDR_INC_ENABLE;
  config.dst.addr_loop_en            = DMA_ADDR_LOOP_DISABLE;
  config.dst.start_addr              = xfer->dst_addr;
  config.dst.end_addr                = xfer->dst_addr + xfer->length - 1;
  config.trans_type                  = DMA_TRANS_DEFAULT;

  (void)bk_dma_stop(priv->dma_id);
  (void)bk_dma_deinit(priv->dma_id);

  sdkret = bk_dma_init(priv->dma_id, &config);
  if (sdkret != BK_OK)
    {
      return bk7258_dma_errno(sdkret);
    }

  sdkret = bk_dma_start(priv->dma_id);
  if (sdkret != BK_OK)
    {
      (void)bk_dma_deinit(priv->dma_id);
      return bk7258_dma_errno(sdkret);
    }

  /* Synchronous completion: the single-shot channel clears its enable bit
   * when the transfer finishes.  Poll with a bounded budget.
   */

  timeout = BK7258_DMA_POLL_MAX_MS / BK7258_DMA_POLL_DELAY_MS;
  while (bk_dma_get_enable_status(priv->dma_id) != 0)
    {
      if (timeout-- == 0)
        {
          (void)bk_dma_stop(priv->dma_id);
          (void)bk_dma_deinit(priv->dma_id);
          return -ETIMEDOUT;
        }

      nxsig_usleep(BK7258_DMA_POLL_DELAY_MS * 1000);
    }

  (void)bk_dma_deinit(priv->dma_id);
  return OK;
}

static int bk7258_dma_ioctl(FAR struct file *filep, int cmd,
                            unsigned long arg)
{
  FAR struct bk7258_dma_priv_s *priv = &g_bk7258_dma;
  FAR const struct bk7258_dma_xfer_s *xfer;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->opened || priv->dma_id >= DMA_ID_MAX)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  switch (cmd)
    {
      case BKIOC_DMA_TRANSFER:
        xfer = (FAR const struct bk7258_dma_xfer_s *)(uintptr_t)arg;
        if (xfer == NULL)
          {
            nxmutex_unlock(&priv->lock);
            return -EINVAL;
          }

        ret = bk7258_dma_transfer(priv, xfer);
        nxmutex_unlock(&priv->lock);
        return ret;

      case BKIOC_DMA_GET_STATUS:
        *(FAR uint32_t *)(uintptr_t)arg =
          bk_dma_get_enable_status(priv->dma_id) != 0 ?
          BK7258_DMA_STATUS_BUSY : BK7258_DMA_STATUS_IDLE;
        nxmutex_unlock(&priv->lock);
        return OK;

      default:
        nxmutex_unlock(&priv->lock);
        return -ENOTTY;
    }
}

int bk7258_dma_initialize(void)
{
  FAR struct bk7258_dma_priv_s *priv = &g_bk7258_dma;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  ret = register_driver(BK7258_DMA_DEVPATH, &g_bk7258_dma_fops, 0666,
                        NULL);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  priv->inited = true;

  syslog(LOG_INFO, "BK7258 DMA: ready %s\n", BK7258_DMA_DEVPATH);

  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif /* CONFIG_BK7258_DMA */
