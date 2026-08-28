/****************************************************************************
 * board/bk7258/chip/ap/bk7258_lin.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 LIN lower-half.
 *
 * NuttX has no LIN upper-half, so this wrapper publishes the controller as a
 * bounded character device /dev/lin0.  Each read/write addresses one LIN
 * frame by identifier; the first implementation supports one node, fixed
 * 1..8 byte data frames, classic/enhanced checksum and master or slave mode.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LIN

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_lin.h>
#include "bk7258_sdk_abi.h"

#define BK7258_LIN_DEVPATH      "/dev/lin0"
#define BK7258_LIN_MAX_FRAME    8
#define BK7258_LIN_RX_TIMEOUT_MS 1000u

struct bk7258_lin_priv_s
{
  mutex_t lock;
  const struct bk7258_lin_board_s *board;
  bool inited;
  bool opened;
};

static struct bk7258_lin_priv_s g_bk7258_lin =
{
  .lock    = NXMUTEX_INITIALIZER,
  .board   = NULL,
  .inited  = false,
  .opened  = false,
};

static int bk7258_lin_open(FAR struct file *filep);
static int bk7258_lin_close(FAR struct file *filep);
static ssize_t bk7258_lin_read(FAR struct file *filep, FAR char *buffer,
                               size_t buflen);
static ssize_t bk7258_lin_write(FAR struct file *filep,
                                FAR const char *buffer, size_t buflen);
static int bk7258_lin_ioctl(FAR struct file *filep, int cmd,
                            unsigned long arg);

static const struct file_operations g_bk7258_lin_fops =
{
  .open  = bk7258_lin_open,
  .close = bk7258_lin_close,
  .read  = bk7258_lin_read,
  .write = bk7258_lin_write,
  .ioctl = bk7258_lin_ioctl,
};

static int bk7258_lin_errno(bk_err_t sdkret)
{
  switch (sdkret)
    {
      case BK_OK:
        return OK;

      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
      case BK_ERR_LIN_HAL_INVALID_ARG:
        return -EINVAL;

      case BK_ERR_LIN_NOT_INIT:
        return -ENODEV;

      case BK_ERR_LIN_TIMEOUT_ERROR:
        return -ETIMEDOUT;

      case BK_ERR_LIN_BIT_ERROR:
      case BK_ERR_LIN_CHK_ERROR:
      case BK_ERR_LIN_PARITY_ERROR:
        return -EIO;

      default:
        return -EIO;
    }
}

static int bk7258_lin_open(FAR struct file *filep)
{
  FAR struct bk7258_lin_priv_s *priv = &g_bk7258_lin;
  FAR const struct bk7258_lin_board_s *board;
  lin_config_t cfg;
  bk_err_t sdkret;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->inited || priv->board == NULL)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  if (priv->opened)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  board = priv->board;

  sdkret = bk_lin_driver_init();
  if (sdkret != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return bk7258_lin_errno(sdkret);
    }

  sdkret = bk_lin_gpio_init((lin_channel_t)board->channel);
  if (sdkret != BK_OK)
    {
      (void)bk_lin_driver_deinit();
      nxmutex_unlock(&priv->lock);
      return bk7258_lin_errno(sdkret);
    }

  memset(&cfg, 0, sizeof(cfg));
  cfg.chn            = (lin_channel_t)board->channel;
  cfg.dev            = board->mode == BK7258_LIN_MODE_MASTER ?
                       LIN_MASTER : LIN_SLAVE;
  cfg.length         = (lin_data_len_t)board->data_length;
  cfg.checksum       = board->checksum == BK7258_LIN_CHECKSUM_ENHANCED ?
                       LIN_ENHANCED : LIN_CLASSIC;
  cfg.rate           = board->rate;
  cfg.bus_inactiv_time = LIN_BUS_INACTIVITY_4S;
  cfg.wup_repeat_time  = LIN_WUP_REPEAT_180MS;

  sdkret = bk_lin_cfg(&cfg);
  if (sdkret != BK_OK)
    {
      (void)bk_lin_driver_deinit();
      nxmutex_unlock(&priv->lock);
      return bk7258_lin_errno(sdkret);
    }

  sdkret = bk_lin_set_dev(cfg.dev);
  if (sdkret != BK_OK)
    {
      (void)bk_lin_driver_deinit();
      nxmutex_unlock(&priv->lock);
      return bk7258_lin_errno(sdkret);
    }

  if (board->mode == BK7258_LIN_MODE_MASTER)
    {
      sdkret = bk_lin_set_rate(board->rate);
      if (sdkret != BK_OK)
        {
          (void)bk_lin_driver_deinit();
          nxmutex_unlock(&priv->lock);
          return bk7258_lin_errno(sdkret);
        }
    }

  sdkret = bk_lin_set_data_length((lin_data_len_t)board->data_length);
  if (sdkret != BK_OK)
    {
      (void)bk_lin_driver_deinit();
      nxmutex_unlock(&priv->lock);
      return bk7258_lin_errno(sdkret);
    }

  sdkret = bk_lin_set_enh_check(board->checksum ==
                                BK7258_LIN_CHECKSUM_ENHANCED ?
                                LIN_ENHANCED : LIN_CLASSIC);
  if (sdkret != BK_OK)
    {
      (void)bk_lin_driver_deinit();
      nxmutex_unlock(&priv->lock);
      return bk7258_lin_errno(sdkret);
    }

  (void)bk_lin_interrupt_enable();
  priv->opened = true;

  syslog(LOG_INFO, "BK7258 LIN: open board=%s mode=%s rate=%g\n",
         board->name,
         board->mode == BK7258_LIN_MODE_MASTER ? "master" : "slave",
         board->rate);

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_lin_close(FAR struct file *filep)
{
  FAR struct bk7258_lin_priv_s *priv = &g_bk7258_lin;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  priv->opened = false;
  (void)bk_lin_interrupt_disable();
  (void)bk_lin_driver_deinit();

  nxmutex_unlock(&priv->lock);
  return OK;
}

static ssize_t bk7258_lin_read(FAR struct file *filep, FAR char *buffer,
                               size_t buflen)
{
  FAR struct bk7258_lin_priv_s *priv = &g_bk7258_lin;
  uint8_t frame[BK7258_LIN_MAX_FRAME];
  bk_err_t sdkret;
  int ret;

  (void)filep;

  if (buffer == NULL || buflen == 0 || buflen > BK7258_LIN_MAX_FRAME)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->opened || priv->board == NULL)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  sdkret = bk_lin_rx((lin_id_t)0, frame, buflen, BK7258_LIN_RX_TIMEOUT_MS);
  nxmutex_unlock(&priv->lock);

  if (sdkret != BK_OK)
    {
      return bk7258_lin_errno(sdkret);
    }

  memcpy(buffer, frame, buflen);
  return (ssize_t)buflen;
}

static ssize_t bk7258_lin_write(FAR struct file *filep,
                                FAR const char *buffer, size_t buflen)
{
  FAR struct bk7258_lin_priv_s *priv = &g_bk7258_lin;
  bk_err_t sdkret;
  int ret;

  (void)filep;

  if (buffer == NULL || buflen == 0 || buflen > BK7258_LIN_MAX_FRAME)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->opened || priv->board == NULL)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  sdkret = bk_lin_send((FAR uint8_t *)buffer, buflen);
  nxmutex_unlock(&priv->lock);

  return sdkret == BK_OK ? (ssize_t)buflen : bk7258_lin_errno(sdkret);
}

static int bk7258_lin_ioctl(FAR struct file *filep, int cmd,
                            unsigned long arg)
{
  (void)filep;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

int bk7258_lin_initialize(FAR const struct bk7258_lin_board_s *board)
{
  FAR struct bk7258_lin_priv_s *priv = &g_bk7258_lin;
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

  if (board == NULL || board->name == NULL ||
      board->data_length < 1 || board->data_length > BK7258_LIN_MAX_FRAME)
    {
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  if (board->control_pins_initialize != NULL)
    {
      ret = board->control_pins_initialize(board);
      if (ret < 0)
        {
          nxmutex_unlock(&priv->lock);
          return ret;
        }
    }

  ret = register_driver(BK7258_LIN_DEVPATH, &g_bk7258_lin_fops, 0666,
                        NULL);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  priv->board = board;
  priv->inited = true;

  syslog(LOG_INFO, "BK7258 LIN: ready %s\n", BK7258_LIN_DEVPATH);

  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif /* CONFIG_BK7258_LIN */
