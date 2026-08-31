/****************************************************************************
 * chips/bk7258/common/
 * bk7258_reset_cause.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <syslog.h>

#include <components/system.h>

#include <arch/chip/bk7258_reset_cause.h>

#if defined(CONFIG_BK7258_WDT) && \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC)
#  include "bk7258_reset_marker_internal.h"
#endif

#ifndef BK7258_AON_PMU_R7A_ADDR
#  define BK7258_AON_PMU_R7A_ADDR 0x440001e8u
#endif

_Static_assert((int)BK7258_RESET_SOURCE_POWERON ==
               (int)RESET_SOURCE_POWERON,
               "SDK power-on reset ABI changed");
_Static_assert((int)BK7258_RESET_SOURCE_REBOOT ==
               (int)RESET_SOURCE_REBOOT,
               "SDK reboot reset ABI changed");
_Static_assert((int)BK7258_RESET_SOURCE_WATCHDOG ==
               (int)RESET_SOURCE_WATCHDOG,
               "SDK watchdog reset ABI changed");
_Static_assert((int)BK7258_RESET_SOURCE_NMI_WDT ==
               (int)RESET_SOURCE_NMI_WDT,
               "SDK NMI watchdog reset ABI changed");

int bk7258_reset_cause_read(FAR struct bk7258_reset_cause_raw_s *raw)
{
  FAR volatile uint32_t *r7a =
    (FAR volatile uint32_t *)BK7258_AON_PMU_R7A_ADDR;

  if (raw == NULL)
    {
      return -EINVAL;
    }

  raw->source = (*r7a >> 4) & 0xffu;
  raw->from_persistent_flag = false;

#if defined(CONFIG_BK7258_WDT) && \
    defined(CONFIG_BK7258_WDT_PRETIMEOUT_PANIC)
  {
    uint32_t marker_source = 0;
    int taken = bk7258_reset_marker_previous(&marker_source);

    syslog(LOG_INFO, "reset-cause flag probe taken=%d\n", taken);
    if (taken < 0)
      {
        return taken;
      }

    if (taken > 0 &&
        (marker_source == BK7258_RESET_SOURCE_WATCHDOG ||
         marker_source == BK7258_RESET_SOURCE_NMI_WDT))
      {
        /* The retained PMU source is primary evidence.  A marker may
         * corroborate an explicit watchdog source, or recover a watchdog
         * after an undocumented/unknown hardware latch value.  It must not
         * overwrite POWERON or REBOOT: those are stronger evidence and this
         * rule prevents a stale marker from turning a power loss or planned
         * software reset into a false watchdog report.
         */

        if (raw->source == BK7258_RESET_SOURCE_WATCHDOG ||
            raw->source == BK7258_RESET_SOURCE_NMI_WDT)
          {
            raw->from_persistent_flag = true;
          }
        else if (raw->source != BK7258_RESET_SOURCE_POWERON &&
                 raw->source != BK7258_RESET_SOURCE_REBOOT)
          {
            raw->source = marker_source;
            raw->from_persistent_flag = true;
          }
      }
  }
#endif

  return 0;
}
