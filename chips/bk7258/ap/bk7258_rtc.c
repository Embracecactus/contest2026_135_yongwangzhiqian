/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/bk7258_rtc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 RTC — NuttX rtc_lowerhalf_s wrapper.
 *
 * Wraps the Beken AON RTC free-running counter as a NuttX RTC lower half.
 * Epoch time is represented as an AP-local signed offset from that counter.
 *
 * AP role — the 4 bk_rtc_* symbols live exclusively in the AP libdriver.a
 * (verified with `nm`), so this wrapper is AP-only like I2C/SPI/SDIO.
 *
 * The SDK's driver/aon_rtc.h includes "sys/time.h" from its own POSIX
 * layer, which does not ship in the bundle.  The board-private SDK ABI header
 * therefore owns the archive declaration while this lower half uses NuttX's
 * time representation.
 *
 * The SDK bk_rtc_settimeofday() implementation persists its offset through
 * EasyFlash.  Flash is CP-owned in this port, so calling that API from AP
 * would violate the ownership boundary.  This lower half deliberately uses
 * bk_aon_rtc_get_us() plus an AP-RAM offset and does not persist across an AP
 * restart.  A future persistent-time service must cross RPMsg to CP.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_RTC

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <nuttx/timers/rtc.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_rtc.h>
#include <arch/chip/bk7258_sdk_abi.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_rtc_priv_s
{
  struct rtc_lowerhalf_s dev;   /* NuttX RTC lower-half anchor */
  spinlock_t lock;              /* protect the 64-bit epoch offset */
  int64_t epoch_offset_us;      /* epoch usec minus AON counter usec */
  bool settime_called;          /* SDK has no "time set" query */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_rtc_rdtime(FAR struct rtc_lowerhalf_s *lower,
                             FAR struct rtc_time *rtctime);
static int bk7258_rtc_settime(FAR struct rtc_lowerhalf_s *lower,
                              FAR const struct rtc_time *rtctime);
static bool bk7258_rtc_havesettime(FAR struct rtc_lowerhalf_s *lower);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rtc_ops_s g_bk7258_rtc_ops =
{
  .rdtime       = bk7258_rtc_rdtime,
  .settime      = bk7258_rtc_settime,
  .havesettime  = bk7258_rtc_havesettime,
};

static struct bk7258_rtc_priv_s g_bk7258_rtc =
{
  .dev.ops          = &g_bk7258_rtc_ops,
  .lock             = SP_UNLOCKED,
  .epoch_offset_us  = 0,
  .settime_called   = false,
};
static bool g_bk7258_rtc_registered;

static int bk7258_rtc_getepoch(FAR struct bk7258_rtc_priv_s *priv,
                               FAR time_t *seconds)
{
  irqstate_t flags;
  int64_t epoch_us;
  int64_t offset;

  if (seconds == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  offset = priv->epoch_offset_us;
  spin_unlock_irqrestore(&priv->lock, flags);

  epoch_us = (int64_t)bk_aon_rtc_get_us() + offset;
  if (epoch_us < 0)
    {
      return -ERANGE;
    }

  *seconds = (time_t)(epoch_us / 1000000ll);
  return OK;
}

static int bk7258_rtc_setepoch(FAR struct bk7258_rtc_priv_s *priv,
                               time_t seconds, long nanoseconds)
{
  irqstate_t flags;
  int64_t desired_us;

  if (seconds < 0 || nanoseconds < 0 || nanoseconds >= 1000000000l)
    {
      return -EINVAL;
    }

  desired_us = (int64_t)seconds * 1000000ll + nanoseconds / 1000;
  flags = spin_lock_irqsave(&priv->lock);
  priv->epoch_offset_us = desired_us - (int64_t)bk_aon_rtc_get_us();
  priv->settime_called = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_rtc_rdtime
 *
 * Read the current epoch time from the SDK AON RTC and convert it into
 * the broken-out rtc_time (cast-compatible with struct tm).
 ****************************************************************************/

static int bk7258_rtc_rdtime(FAR struct rtc_lowerhalf_s *lower,
                             FAR struct rtc_time *rtctime)
{
  time_t sec;
  int ret;

  if (rtctime == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_rtc_getepoch((FAR struct bk7258_rtc_priv_s *)lower,
                            &sec);
  if (ret < 0)
    {
      return ret;
    }

  /* rtc_time is required to be cast-compatible with struct tm. */
  if (gmtime_r(&sec, (FAR struct tm *)rtctime) == NULL)
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_rtc_settime
 *
 * Convert the broken-out rtc_time back into an epoch and push it into the
 * SDK AON RTC.  Marks the time as set for havesettime().
 ****************************************************************************/

static int bk7258_rtc_settime(FAR struct rtc_lowerhalf_s *lower,
                              FAR const struct rtc_time *rtctime)
{
  time_t sec;

  if (rtctime == NULL)
    {
      return -EINVAL;
    }

  sec = timegm((FAR struct tm *)(uintptr_t)rtctime);

  return bk7258_rtc_setepoch((FAR struct bk7258_rtc_priv_s *)lower,
                             sec, 0);
}

/****************************************************************************
 * Name: bk7258_rtc_havesettime
 ****************************************************************************/

static bool bk7258_rtc_havesettime(FAR struct rtc_lowerhalf_s *lower)
{
  FAR struct bk7258_rtc_priv_s *priv =
    (FAR struct bk7258_rtc_priv_s *)lower;

  return priv->settime_called;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_rtc_initialize(void)
{
  int ret;

  if (g_bk7258_rtc_registered)
    {
      return OK;
    }

  ret = rtc_initialize(0, &g_bk7258_rtc.dev);
  if (ret >= 0)
    {
      g_bk7258_rtc_registered = true;
    }

  return ret;
}

/* CONFIG_RTC is a system clock source as well as the /dev/rtc upper half.
 * Supply the architecture hooks selected by BK7258_RTC so the generic clock
 * code does not depend on an unrelated SoC RTC implementation.
 */

int up_rtc_initialize(void)
{
  return OK;
}

int up_rtc_getdatetime(FAR struct tm *tp)
{
  time_t sec;
  int ret;

  if (tp == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_rtc_getepoch(&g_bk7258_rtc, &sec);
  if (ret < 0)
    {
      return ret;
    }

  return gmtime_r(&sec, tp) == NULL ? -EINVAL : OK;
}

int up_rtc_settime(FAR const struct timespec *tp)
{
  if (tp == NULL)
    {
      return -EINVAL;
    }

  return bk7258_rtc_setepoch(&g_bk7258_rtc, tp->tv_sec, tp->tv_nsec);
}

#endif /* CONFIG_BK7258_RTC */
