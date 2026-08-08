/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_rtc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) RTC — NuttX rtc_lowerhalf_s wrapper.
 *
 * Wraps the Beken AON RTC driver (bk_rtc_*) as a NuttX RTC lower half.
 * The AON RTC keeps time across low-power states; the SDK anchors it to a
 * boot-time offset (s_boot_time_us) so bk_rtc_gettimeofday() returns epoch
 * seconds + microseconds.
 *
 * AP role — the 4 bk_rtc_* symbols live exclusively in the AP libdriver.a
 * (verified with `nm`), so this wrapper is AP-only like I2C/SPI/SDIO.
 *
 * The SDK's driver/aon_rtc.h includes "sys/time.h" from its own POSIX
 * layer, which does not ship in the bundle.  To avoid that include path we
 * declare the two SDK entry points here (their symbols ARE in libdriver.a)
 * and use NuttX's struct timeval (include/sys/time.h), which is layout
 * compatible with the SDK's.
 *
 * SDK call mapping:
 *   rdtime()      -> bk_rtc_gettimeofday(&tv, NULL); gmtime_r(&tv.tv_sec)
 *   settime()     -> timegm((struct tm *)rtctime); bk_rtc_settimeofday(&tv)
 *   havesettime() -> local flag (SDK exposes no such query)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_RTC

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <nuttx/timers/rtc.h>

#include <common/bk_err.h>

#include <arch/chip/bk7258_rtc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SDK AON RTC driver entry points.  Declared here instead of including
 * <driver/aon_rtc.h> because that header pulls the SDK's own
 * "sys/time.h" (not shipped in the bundle) and would clash with NuttX's.
 * The symbols themselves are present in the AP libdriver.a.
 */

extern bk_err_t bk_rtc_gettimeofday(struct timeval *tv, void *ptz);
extern bk_err_t bk_rtc_settimeofday(const struct timeval *tv,
                                    const struct timezone *tz);

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_rtc_priv_s
{
  struct rtc_lowerhalf_s dev;   /* NuttX RTC lower-half anchor */
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
  .settime_called   = false,
};

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
  struct timeval tv;
  time_t sec;
  bk_err_t ret;

  if (rtctime == NULL)
    {
      return -EINVAL;
    }

  memset(&tv, 0, sizeof(tv));
  ret = bk_rtc_gettimeofday(&tv, NULL);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  sec = tv.tv_sec;

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
  struct timeval tv;
  time_t sec;
  bk_err_t ret;

  if (rtctime == NULL)
    {
      return -EINVAL;
    }

  sec = timegm((FAR struct tm *)(uintptr_t)rtctime);

  tv.tv_sec  = sec;
  tv.tv_usec = 0;

  ret = bk_rtc_settimeofday(&tv, NULL);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  ((FAR struct bk7258_rtc_priv_s *)lower)->settime_called = true;
  return OK;
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
  return rtc_initialize(0, &g_bk7258_rtc.dev);
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
  struct timeval tv;
  time_t sec;

  if (tp == NULL || bk_rtc_gettimeofday(&tv, NULL) != BK_OK)
    {
      return -EIO;
    }

  sec = tv.tv_sec;
  return gmtime_r(&sec, tp) == NULL ? -EINVAL : OK;
}

int up_rtc_settime(FAR const struct timespec *tp)
{
  struct timeval tv;

  if (tp == NULL)
    {
      return -EINVAL;
    }

  tv.tv_sec = tp->tv_sec;
  tv.tv_usec = tp->tv_nsec / 1000;
  return bk_rtc_settimeofday(&tv, NULL) == BK_OK ? OK : -EIO;
}

#endif /* CONFIG_BK7258_RTC */
