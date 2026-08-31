/****************************************************************************
 * boards/bk7258/common/src/bk7258_ap_entry.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned AP initial application.  The symbol name remains the existing
 * CONFIG_INIT_ENTRYPOINT ABI; ownership is determined by this board source.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_CORE

#include <errno.h>
#include <stdbool.h>
#ifdef CONFIG_LIBC_LOCALTIME
#  include <stdlib.h>
#  include <time.h>
#endif
#include <syslog.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_ap_lifecycle.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_LIBC_LOCALTIME
static int bk7258_board_timezone_initialize(void)
{
  if (setenv("TZ", CONFIG_BK7258_AP_TIMEZONE, true) < 0)
    {
      return -errno;
    }

  tzset();
  return OK;
}

#ifdef CONFIG_BK7258_RTC
static void bk7258_board_time_report(void)
{
  struct timespec realtime;
  struct tm utc;
  struct tm local;
  time_t now;

  if (clock_gettime(CLOCK_REALTIME, &realtime) < 0)
    {
      syslog(LOG_ERR, "bk7258: CLOCK_REALTIME unavailable: %d\n", errno);
      return;
    }

  now = realtime.tv_sec;
  if (gmtime_r(&now, &utc) == NULL ||
      localtime_r(&now, &local) == NULL)
    {
      syslog(LOG_ERR, "bk7258: calendar conversion failed\n");
      return;
    }

  syslog(LOG_INFO,
         "bk7258: time utc=%04d-%02d-%02dT%02d:%02d:%02dZ "
         "local=%04d-%02d-%02dT%02d:%02d:%02d %s\n",
         utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
         utc.tm_hour, utc.tm_min, utc.tm_sec,
         local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
         local.tm_hour, local.tm_min, local.tm_sec,
         local.tm_zone != NULL ? local.tm_zone : "?");
}
#endif
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_main(int argc, FAR char *argv[])
{
  uint32_t failure = BK7258_AP_ERROR_BAD_BOOT_STATE;
#ifdef CONFIG_BK7258_AP_APPLICATION_LIFECYCLE
  bool application_ready = true;
#endif
  int ret;

  (void)argc;
  (void)argv;

  ret = bk7258_ap_lifecycle_startup(&failure);
  if (ret < 0)
    {
      bk7258_ap_lifecycle_fail_and_park(failure);
    }

  ret = bk7258_board_ap_initialize();
  if (ret < 0)
    {
      bk7258_ap_lifecycle_fail_and_park(BK7258_AP_ERROR_PERIPHERALS);
    }

#ifdef CONFIG_LIBC_LOCALTIME
  ret = bk7258_board_timezone_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "bk7258: timezone setup failed: %d\n", ret);
      bk7258_ap_lifecycle_fail_and_park(BK7258_AP_ERROR_BAD_BOOT_STATE);
    }

  syslog(LOG_INFO, "bk7258: timezone=%s\n", CONFIG_BK7258_AP_TIMEZONE);
#  ifdef CONFIG_BK7258_RTC
  bk7258_board_time_report();
#  endif
#endif

#ifdef CONFIG_BK7258_AP_APPLICATION_LIFECYCLE
  ret = bk7258_ap_application_prepare();
  if (ret < 0)
    {
      syslog(LOG_ERR, "bk7258: application prepare failed: %d\n", ret);
      application_ready = false;
    }
#endif

  ret = bk7258_ap_lifecycle_publish_ready(&failure);
  if (ret < 0)
    {
      bk7258_ap_lifecycle_fail_and_park(failure);
    }

#ifdef CONFIG_BK7258_AP_APPLICATION_LIFECYCLE
  if (application_ready)
    {
      ret = bk7258_ap_application_start();
      if (ret < 0)
        {
          syslog(LOG_ERR, "bk7258: application start failed: %d\n", ret);
        }
    }
#endif

  bk7258_ap_lifecycle_supervise();
}

#endif /* CONFIG_BK7258_AP_CORE */
