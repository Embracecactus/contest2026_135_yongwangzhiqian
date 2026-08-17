/****************************************************************************
 * board/bk7258/chip/ap/bk7258_slcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 segment-LCD lower half.
 *
 * NuttX has no segment-LCD upper half, so this wrapper publishes the
 * controller as the bounded character device /dev/slcd0.  A write transfers
 * up to 32 segment values (one byte per segment, each bit a COM); private
 * ioctls provide single-segment and enable-mask updates.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SLCD

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_sdk_abi.h>
#include <arch/chip/bk7258_slcd.h>

#define BK7258_SLCD_DEVPATH       "/dev/slcd0"
#define BK7258_SLCD_SEGMENTS      32u

#define BK7258_SLCDIOC_SET_SEG    0x1001
#define BK7258_SLCDIOC_SET_COM    0x1002
#define BK7258_SLCDIOC_SET_SEG_EN 0x1003

struct bk7258_slcd_seg_arg_s
{
  uint8_t seg;
  uint8_t value;
};

struct bk7258_slcd_priv_s
{
  mutex_t lock;
  const struct bk7258_slcd_board_s *board;
  bool inited;
  bool opened;
};

static struct bk7258_slcd_priv_s g_bk7258_slcd =
{
  .lock   = NXMUTEX_INITIALIZER,
  .board  = NULL,
  .inited = false,
  .opened = false,
};

static int bk7258_slcd_open(FAR struct file *filep);
static int bk7258_slcd_close(FAR struct file *filep);
static ssize_t bk7258_slcd_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen);
static int bk7258_slcd_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg);

static const struct file_operations g_bk7258_slcd_fops =
{
  .open  = bk7258_slcd_open,
  .close = bk7258_slcd_close,
  .write = bk7258_slcd_write,
  .ioctl = bk7258_slcd_ioctl,
};

static void bk7258_slcd_write_segments(const uint8_t *segments)
{
  unsigned int group;

  for (group = 0; group < BK7258_SLCD_SEGMENTS / 4; group++)
    {
      uint32_t value = (uint32_t)segments[group * 4 + 0] |
                       (uint32_t)segments[group * 4 + 1] << 8 |
                       (uint32_t)segments[group * 4 + 2] << 16 |
                       (uint32_t)segments[group * 4 + 3] << 24;

      switch (group)
        {
          case 0:
            bk_slcd_set_seg00_03_value(value);
            break;
          case 1:
            bk_slcd_set_seg04_07_value(value);
            break;
          case 2:
            bk_slcd_set_seg08_11_value(value);
            break;
          case 3:
            bk_slcd_set_seg12_15_value(value);
            break;
          case 4:
            bk_slcd_set_seg16_19_value(value);
            break;
          case 5:
            bk_slcd_set_seg20_23_value(value);
            break;
          case 6:
            bk_slcd_set_seg24_27_value(value);
            break;
          default:
            bk_slcd_set_seg28_31_value(value);
            break;
        }
    }
}

static int bk7258_slcd_open(FAR struct file *filep)
{
  FAR struct bk7258_slcd_priv_s *priv = &g_bk7258_slcd;
  FAR const struct bk7258_slcd_board_s *board;
  slcd_config_t config;
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
  config.com_num   = (slcd_com_num_t)board->com_num;
  config.slcd_bias = (slcd_bias_t)board->bias;
  config.slcd_rate = (slcd_rate_t)board->rate;

  bk_slcd_driver_init(config);
  bk_slcd_set_com_port_enable(board->com_enable);
  bk_slcd_set_seg_port_enable(board->seg_enable);
  priv->opened = true;

  syslog(LOG_INFO, "BK7258 SLCD: open board=%s com=%u\n",
         board->name, board->com_num);

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_slcd_close(FAR struct file *filep)
{
  FAR struct bk7258_slcd_priv_s *priv = &g_bk7258_slcd;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  priv->opened = false;
  bk_slcd_driver_deinit();

  nxmutex_unlock(&priv->lock);
  return OK;
}

static ssize_t bk7258_slcd_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen)
{
  FAR struct bk7258_slcd_priv_s *priv = &g_bk7258_slcd;
  uint8_t segments[BK7258_SLCD_SEGMENTS];
  int ret;

  (void)filep;

  if (buffer == NULL || buflen == 0 || buflen > BK7258_SLCD_SEGMENTS)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->opened)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  memset(segments, 0, sizeof(segments));
  memcpy(segments, buffer, buflen);
  bk7258_slcd_write_segments(segments);

  nxmutex_unlock(&priv->lock);
  return (ssize_t)buflen;
}

static int bk7258_slcd_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg)
{
  FAR struct bk7258_slcd_priv_s *priv = &g_bk7258_slcd;
  FAR struct bk7258_slcd_seg_arg_s *seg_arg;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->opened)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  switch (cmd)
    {
      case BK7258_SLCDIOC_SET_SEG:
        seg_arg = (FAR struct bk7258_slcd_seg_arg_s *)arg;
        if (seg_arg == NULL || seg_arg->seg >= BK7258_SLCD_SEGMENTS)
          {
            ret = -EINVAL;
          }
        else
          {
            bk_slcd_set_seg_value((slcd_seg_id_t)seg_arg->seg,
                                  seg_arg->value);
            ret = OK;
          }
        break;

      case BK7258_SLCDIOC_SET_COM:
        bk_slcd_set_com_port_enable((uint8_t)arg);
        ret = OK;
        break;

      case BK7258_SLCDIOC_SET_SEG_EN:
        bk_slcd_set_seg_port_enable((uint32_t)arg);
        ret = OK;
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_slcd_initialize(void)
{
  FAR struct bk7258_slcd_priv_s *priv = &g_bk7258_slcd;
  FAR const struct bk7258_slcd_board_s *board;
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

  board = bk7258_board_slcd_config();
  if (board == NULL || board->name == NULL ||
      (board->com_num != 4 && board->com_num != 8))
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

  ret = register_driver(BK7258_SLCD_DEVPATH, &g_bk7258_slcd_fops, 0666,
                        NULL);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  priv->board = board;
  priv->inited = true;

  syslog(LOG_INFO, "BK7258 SLCD: ready %s\n", BK7258_SLCD_DEVPATH);

  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif /* CONFIG_BK7258_SLCD */
