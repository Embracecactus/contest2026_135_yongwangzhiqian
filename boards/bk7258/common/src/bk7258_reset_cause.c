/****************************************************************************
 * boards/bk7258/common/src/
 * bk7258_reset_cause.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BOARDCTL_RESET_CAUSE

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/boardctl.h>
#include <syslog.h>

#include <arch/chip/bk7258_reset_cause.h>

int board_reset_cause(FAR struct boardioc_reset_cause_s *cause)
{
  struct bk7258_reset_cause_raw_s raw;
  int ret;

  if (cause == NULL)
    {
      return -EINVAL;
    }

  memset(cause, 0, sizeof(*cause));
  ret = bk7258_reset_cause_read(&raw);
  if (ret < 0)
    {
      return ret;
    }

  switch (raw.source)
    {
      case BK7258_RESET_SOURCE_POWERON:
        cause->cause = BOARDIOC_RESETCAUSE_SYS_CHIPPOR;
        break;

      case BK7258_RESET_SOURCE_WATCHDOG:
      case BK7258_RESET_SOURCE_NMI_WDT:
        cause->cause = BOARDIOC_RESETCAUSE_SYS_RWDT;
        break;

      case BK7258_RESET_SOURCE_REBOOT:
        cause->cause = BOARDIOC_RESETCAUSE_CORE_SOFT;
        break;

      default:
        cause->cause = BOARDIOC_RESETCAUSE_NONE;
        break;
    }

  syslog(LOG_INFO, "reset cause raw=%" PRIu32 " mapped=%d flag=%d\n",
         raw.source, (int)cause->cause, (int)raw.from_persistent_flag);
  return 0;
}

#endif /* CONFIG_BOARDCTL_RESET_CAUSE */
