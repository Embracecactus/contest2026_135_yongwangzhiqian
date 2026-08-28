/****************************************************************************
 * chips/bk7258/ap/bk7258_sbc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SBC hardware decoder helper.
 *
 * NuttX has no SBC upper half or codec ABI, so this chip layer owns the
 * hardware decoder as a typed singleton helper (same pattern as DMA2D and
 * the JPEG/H.264 helpers).  The SDK supplies the hardware decode; this file
 * owns the context lifetime and the bounded synchronous call boundary.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SBC

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_sbc.h>

#include <driver/sbc.h>

struct bk7258_sbc_priv_s
{
  mutex_t lock;
  sbcdecodercontext_t decoder;
  bool inited;
};

static struct bk7258_sbc_priv_s g_bk7258_sbc =
{
  .lock   = NXMUTEX_INITIALIZER,
  .inited = false,
};

int bk7258_sbc_initialize(void)
{
  FAR struct bk7258_sbc_priv_s *priv = &g_bk7258_sbc;
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

  memset(&priv->decoder, 0, sizeof(priv->decoder));
  sdkret = bk_sbc_decoder_init(&priv->decoder);
  if (sdkret != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  (void)bk_sbc_decoder_interrupt_enable(false);
  priv->inited = true;

  syslog(LOG_INFO, "BK7258 SBC: decoder ready\n");

  nxmutex_unlock(&priv->lock);
  return OK;
}

int bk7258_sbc_decode_frame(const void *data, size_t len,
                            struct bk7258_sbc_decode_result_s *result)
{
  FAR struct bk7258_sbc_priv_s *priv = &g_bk7258_sbc;
  FAR sbcdecodercontext_t *decoder;
  bk_err_t sdkret;
  int ret;

  if (data == NULL || result == NULL)
    {
      return -EINVAL;
    }

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

  decoder = &priv->decoder;
  sdkret = bk_sbc_decoder_frame_decode(decoder, data, len);
  if (sdkret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  result->pcm         = decoder->pcm_sample;
  result->channels    = decoder->channel_number > 0 ?
                        (unsigned int)decoder->channel_number : 1u;
  result->sample_rate = decoder->sample_rate;
  result->pcm_bytes   = (size_t)decoder->pcm_length *
                        result->channels * sizeof(int32_t);

  nxmutex_unlock(&priv->lock);
  return OK;
}

int bk7258_sbc_uninitialize(void)
{
  FAR struct bk7258_sbc_priv_s *priv = &g_bk7258_sbc;
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

  (void)bk_sbc_decoder_deinit();
  priv->inited = false;

  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif /* CONFIG_BK7258_SBC */
