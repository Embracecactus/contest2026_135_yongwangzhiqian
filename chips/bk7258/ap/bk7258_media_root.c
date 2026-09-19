/****************************************************************************
 * chips/bk7258/ap/bk7258_media_root.c
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
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/mutex.h>

#ifdef CONFIG_BK7258_PM_CLOCK
#  include <arch/chip/bk7258_pm.h>
#endif

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
static uint8_t g_bk7258_media_audio_owners;

#ifdef CONFIG_BK7258_PM_CLOCK
static bool g_bk7258_media_audio_pm_held;
static bool g_bk7258_media_audio_pm_uncertain;
static uint8_t g_bk7258_media_audio_frequency_owners;
static uint8_t g_bk7258_media_audio_frequency_uncertain;
static int g_bk7258_media_audio_frequency_error;
#endif

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

#ifdef CONFIG_BK7258_PM_CLOCK
/* CLOCK_GET can be committed by CP even when AP loses the reply.  A following
 * CLOCK_PUT first recovers that pending tuple in the PM client and then
 * releases it; -EALREADY also proves that CP owns no AUDIO reference.  Keep an
 * uncertain state only when that convergence operation itself cannot finish.
 */

static int bk7258_media_audio_pm_release(void)
{
  int ret;

  ret = bk7258_pm_clock_put(BK7258_PM_CLOCK_AUDIO);
  if (ret == OK || ret == -EALREADY)
    {
      g_bk7258_media_audio_pm_held = false;
      g_bk7258_media_audio_pm_uncertain = false;
      return OK;
    }

  g_bk7258_media_audio_pm_held = false;
  g_bk7258_media_audio_pm_uncertain = true;
  return ret;
}
#endif

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
#ifdef CONFIG_BK7258_PM_CLOCK
  int cleanup_ret;
#endif
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

  if ((g_bk7258_media_audio_owners & owner) != 0)
    {
      ret = -EBUSY;
    }
  else if (g_bk7258_media_audio_owners != 0)
    {
      /* The SDK audio root supports ADC and DAC together.  Official Media
       * also keeps its playback graph reserved while the next capture opens,
       * so the composite CP clock spans the first through the last owner. */

      g_bk7258_media_audio_owners |= owner;
      ret = 0;
    }
  else
    {
#ifdef CONFIG_BK7258_PM_CLOCK
      /* A prior request may have timed out after CP committed it.  Converge
       * that state before creating a new session so AP and CP start from the
       * same zero-reference boundary.
       */

      if (g_bk7258_media_audio_pm_held ||
          g_bk7258_media_audio_pm_uncertain)
        {
          cleanup_ret = bk7258_media_audio_pm_release();
          if (cleanup_ret < 0)
            {
              ret = cleanup_ret;
              goto out;
            }
        }

      ret = bk7258_pm_clock_get(BK7258_PM_CLOCK_AUDIO);
      if (ret < 0)
        {
          /* The request may have reached CP before its response was lost.
           * Compensate immediately; a later acquire will retry convergence if
           * this cleanup also times out.
           */

          g_bk7258_media_audio_pm_held = false;
          g_bk7258_media_audio_pm_uncertain = true;
          cleanup_ret = bk7258_media_audio_pm_release();
          goto out;
        }

      g_bk7258_media_audio_pm_held = true;
      g_bk7258_media_audio_pm_uncertain = false;
#endif
      g_bk7258_media_audio_owners = owner;
      ret = 0;
    }

#ifdef CONFIG_BK7258_PM_CLOCK
out:
#endif
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

  if ((g_bk7258_media_audio_owners & owner) == 0)
    {
      ret = -EPERM;
    }
#ifdef CONFIG_BK7258_PM_CLOCK
  else if (((g_bk7258_media_audio_frequency_owners |
             g_bk7258_media_audio_frequency_uncertain) & owner) != 0)
    {
      /* Frequency ownership is nested inside the composite clock session. */

      ret = -EBUSY;
    }
#endif
  else if ((g_bk7258_media_audio_owners & ~owner) != 0)
    {
      g_bk7258_media_audio_owners &= ~owner;
      ret = 0;
    }
  else
    {
#ifdef CONFIG_BK7258_PM_CLOCK
      ret = bk7258_media_audio_pm_release();
      if (ret < 0)
        {
          goto out;
        }
#endif
      g_bk7258_media_audio_owners = 0;
      ret = 0;
    }

#ifdef CONFIG_BK7258_PM_CLOCK
out:
#endif
  nxmutex_unlock(&g_bk7258_media_root_lock);
  return ret;
}

int bk7258_media_audio_frequency_acquire(uint8_t owner)
{
#ifdef CONFIG_BK7258_PM_CLOCK
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

  if ((g_bk7258_media_audio_owners & owner) == 0)
    {
      ret = -EPERM;
    }
  else if ((g_bk7258_media_audio_frequency_owners & owner) != 0 ||
           (g_bk7258_media_audio_frequency_uncertain & owner) != 0)
    {
      ret = -EBUSY;
    }
  else if (g_bk7258_media_audio_frequency_uncertain != 0)
    {
      /* Every concurrent owner must participate in convergence.  The final
       * release sends DEFAULT after a lost reply that may have committed. */

      g_bk7258_media_audio_frequency_uncertain |= owner;
      ret = g_bk7258_media_audio_frequency_error;
    }
  else if (g_bk7258_media_audio_frequency_owners != 0)
    {
      g_bk7258_media_audio_frequency_owners |= owner;
      ret = 0;
    }
  else
    {
      ret = bk7258_pm_frequency_vote(BK7258_PM_FREQ_CLIENT_AUDIO,
                                     BK7258_PM_OPP_480M);
      if (ret < 0)
        {
          g_bk7258_media_audio_frequency_uncertain = owner;
          g_bk7258_media_audio_frequency_error = ret;
        }
      else
        {
          g_bk7258_media_audio_frequency_owners = owner;
        }
    }

  nxmutex_unlock(&g_bk7258_media_root_lock);
  return ret;
#else
  (void)owner;
  return 0;
#endif
}

int bk7258_media_audio_frequency_release(uint8_t owner)
{
#ifdef CONFIG_BK7258_PM_CLOCK
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

  if ((g_bk7258_media_audio_frequency_uncertain & owner) != 0)
    {
      if ((g_bk7258_media_audio_frequency_uncertain & ~owner) != 0)
        {
          g_bk7258_media_audio_frequency_uncertain &= ~owner;
          ret = 0;
        }
      else
        {
          ret = bk7258_pm_frequency_vote(BK7258_PM_FREQ_CLIENT_AUDIO,
                                         BK7258_PM_OPP_DEFAULT);
          if (ret == 0)
            {
              g_bk7258_media_audio_frequency_uncertain = 0;
              g_bk7258_media_audio_frequency_error = 0;
            }
        }
    }
  else if ((g_bk7258_media_audio_frequency_owners & owner) == 0)
    {
      ret = -EPERM;
    }
  else if ((g_bk7258_media_audio_frequency_owners & ~owner) != 0)
    {
      g_bk7258_media_audio_frequency_owners &= ~owner;
      ret = 0;
    }
  else
    {
      ret = bk7258_pm_frequency_vote(BK7258_PM_FREQ_CLIENT_AUDIO,
                                     BK7258_PM_OPP_DEFAULT);
      if (ret == 0)
        {
          g_bk7258_media_audio_frequency_owners = 0;
        }
    }

  nxmutex_unlock(&g_bk7258_media_root_lock);
  return ret;
#else
  (void)owner;
  return 0;
#endif
}
