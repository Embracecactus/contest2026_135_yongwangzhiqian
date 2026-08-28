/****************************************************************************
 * chips/bk7258/ap/bk7258_dmic.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 audio DMIC helper.
 *
 * The immutable v3.1.1.9 SDK owns the DMIC block and FIFO; this chip layer
 * owns the board binding, sample-rate policy and the bounded start/stop/
 * FIFO-read call boundary.  A NuttX audio upper half may consume this helper
 * later; the analog-MIC wrapper remains the full /dev/audio path today.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_DMIC

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_dmic.h>

#include <driver/aud_dmic.h>
#include <driver/aud_dmic_types.h>

struct bk7258_dmic_priv_s
{
  mutex_t lock;
  const struct bk7258_dmic_board_s *board;
  bool inited;
  bool started;
};

static struct bk7258_dmic_priv_s g_bk7258_dmic =
{
  .lock    = NXMUTEX_INITIALIZER,
  .board   = NULL,
  .inited  = false,
  .started = false,
};

int bk7258_dmic_initialize(
  FAR const struct bk7258_dmic_board_s *board)
{
  FAR struct bk7258_dmic_priv_s *priv = &g_bk7258_dmic;
  aud_dmic_config_t config;
  bk_err_t sdkret;
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
      board->sample_rate < 8000 || board->sample_rate > 48000)
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

  config.dmic_chl  = board->channel == 0 ? AUD_DMIC_CHL_L :
                     AUD_DMIC_CHL_R;
  config.samp_rate = board->sample_rate;

  sdkret = bk_aud_dmic_init(&config);
  if (sdkret != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  priv->board = board;
  priv->inited = true;

  syslog(LOG_INFO, "BK7258 DMIC: ready board=%s rate=%lu\n",
         board->name, (unsigned long)board->sample_rate);

  nxmutex_unlock(&priv->lock);
  return OK;
}

int bk7258_dmic_start(void)
{
  FAR struct bk7258_dmic_priv_s *priv = &g_bk7258_dmic;
  bk_err_t sdkret;
  int ret;

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

  if (!priv->started)
    {
      sdkret = bk_aud_dmic_start();
      if (sdkret != BK_OK)
        {
          nxmutex_unlock(&priv->lock);
          return -EIO;
        }

      priv->started = true;
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

int bk7258_dmic_stop(void)
{
  FAR struct bk7258_dmic_priv_s *priv = &g_bk7258_dmic;
  bk_err_t sdkret;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->started)
    {
      sdkret = bk_aud_dmic_stop();
      priv->started = false;
      if (sdkret != BK_OK)
        {
          nxmutex_unlock(&priv->lock);
          return -EIO;
        }
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

int bk7258_dmic_read_fifo(uint32_t *sample)
{
  FAR struct bk7258_dmic_priv_s *priv = &g_bk7258_dmic;
  bk_err_t sdkret;
  int ret;

  if (sample == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->inited || !priv->started)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  sdkret = bk_aud_dmic_get_fifo_data(sample);
  nxmutex_unlock(&priv->lock);

  return sdkret == BK_OK ? OK : -EIO;
}

int bk7258_dmic_uninitialize(void)
{
  FAR struct bk7258_dmic_priv_s *priv = &g_bk7258_dmic;
  int ret;

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

  if (priv->started)
    {
      (void)bk_aud_dmic_stop();
      priv->started = false;
    }

  (void)bk_aud_dmic_deinit();
  priv->inited = false;
  priv->board = NULL;

  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif /* CONFIG_BK7258_DMIC */
