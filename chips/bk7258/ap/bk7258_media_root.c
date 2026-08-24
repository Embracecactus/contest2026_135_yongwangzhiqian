/****************************************************************************
 * board/bk7258/chip/ap/bk7258_media_root.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The v3.1.1.9 AP SDK treats DMA, YUV, JPEG and H.264 driver objects as
 * process-lifetime roots.  SDK common bringup initializes them once and
 * ordinary clients release only their channel or stream instance.  NuttX
 * does not run that vendor-wide bringup, so wrappers acquire the roots here
 * without gaining permission to tear them down.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>

#include <nuttx/mutex.h>

#include <common/bk_err.h>
#include <driver/dma.h>
#include <driver/h264.h>
#include <driver/jpeg_enc.h>
#include <driver/yuv_buf.h>

#include "bk7258_media_root.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_media_root_lock = NXMUTEX_INITIALIZER;
static uint32_t g_bk7258_media_roots;
static uint8_t g_bk7258_media_audio_owner;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_media_root_result(bk_err_t result)
{
  switch (result)
    {
      case BK_OK:
        return 0;

      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case BK_ERR_BUSY:
      case BK_ERR_IN_PROGRESS:
        return -EBUSY;

      case BK_ERR_NOT_INIT:
      case BK_ERR_NO_DEV:
        return -ENODEV;

      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
        return -EINVAL;

      default:
        return -EIO;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_media_root_initialize(uint32_t roots)
{
  bk_err_t sdkret;
  int ret;

  if (roots == 0 || (roots & ~BK7258_MEDIA_ROOT_ALL) != 0)
    {
      return -EINVAL;
    }

  /* Every encoded stream uses the YUV front end and every media engine may
   * allocate a DMA channel.  Make those dependencies part of the ownership
   * interface instead of relying on each caller to reproduce SDK order.
   */

  if ((roots & (BK7258_MEDIA_ROOT_JPEG | BK7258_MEDIA_ROOT_H264)) != 0)
    {
      roots |= BK7258_MEDIA_ROOT_YUV;
    }

  if ((roots & ~BK7258_MEDIA_ROOT_DMA) != 0)
    {
      roots |= BK7258_MEDIA_ROOT_DMA;
    }

  ret = nxmutex_lock(&g_bk7258_media_root_lock);
  if (ret < 0)
    {
      return ret;
    }

  if ((roots & BK7258_MEDIA_ROOT_DMA) != 0 &&
      (g_bk7258_media_roots & BK7258_MEDIA_ROOT_DMA) == 0)
    {
      sdkret = bk_dma_driver_init();
      ret = bk7258_media_root_result(sdkret);
      if (ret < 0)
        {
          goto out;
        }

      g_bk7258_media_roots |= BK7258_MEDIA_ROOT_DMA;
    }

  if ((roots & BK7258_MEDIA_ROOT_YUV) != 0 &&
      (g_bk7258_media_roots & BK7258_MEDIA_ROOT_YUV) == 0)
    {
      sdkret = bk_yuv_buf_driver_init();
      ret = bk7258_media_root_result(sdkret);
      if (ret < 0)
        {
          goto out;
        }

      g_bk7258_media_roots |= BK7258_MEDIA_ROOT_YUV;
    }

  if ((roots & BK7258_MEDIA_ROOT_JPEG) != 0 &&
      (g_bk7258_media_roots & BK7258_MEDIA_ROOT_JPEG) == 0)
    {
      sdkret = bk_jpeg_enc_driver_init();
      ret = bk7258_media_root_result(sdkret);
      if (ret < 0)
        {
          goto out;
        }

      g_bk7258_media_roots |= BK7258_MEDIA_ROOT_JPEG;
    }

  if ((roots & BK7258_MEDIA_ROOT_H264) != 0 &&
      (g_bk7258_media_roots & BK7258_MEDIA_ROOT_H264) == 0)
    {
      sdkret = bk_h264_driver_init();
      ret = bk7258_media_root_result(sdkret);
      if (ret < 0)
        {
          goto out;
        }

      g_bk7258_media_roots |= BK7258_MEDIA_ROOT_H264;
    }

  ret = 0;

out:
  nxmutex_unlock(&g_bk7258_media_root_lock);
  return ret;
}

int bk7258_media_audio_session_acquire(uint8_t owner)
{
  int ret;

  if (owner != BK7258_MEDIA_AUDIO_MIC &&
      owner != BK7258_MEDIA_AUDIO_DAC)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_media_root_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_media_audio_owner != 0)
    {
      ret = -EBUSY;
    }
  else
    {
      g_bk7258_media_audio_owner = owner;
      ret = 0;
    }

  nxmutex_unlock(&g_bk7258_media_root_lock);
  return ret;
}

int bk7258_media_audio_session_release(uint8_t owner)
{
  int ret;

  if (owner != BK7258_MEDIA_AUDIO_MIC &&
      owner != BK7258_MEDIA_AUDIO_DAC)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_media_root_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_media_audio_owner != owner)
    {
      ret = -EPERM;
    }
  else
    {
      g_bk7258_media_audio_owner = 0;
      ret = 0;
    }

  nxmutex_unlock(&g_bk7258_media_root_lock);
  return ret;
}
