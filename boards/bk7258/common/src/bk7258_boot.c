/****************************************************************************
 * boards/bk7258/common/src/bk7258_boot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX late-initialization entry point for the Beken BK7258 board.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>

#ifdef CONFIG_BK7258_AP_CORE
#  include <arch/chip/bk7258_ap_platform.h>
#endif

#include "bk7258_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void board_late_initialize(void)
{
#ifdef CONFIG_BK7258_AP_CORE
  int ret = bk7258_ap_platform_prepare();

  if (ret < 0)
    {
      /* AP main consumes the cached result and publishes its role-specific
       * failure before parking.  Keep this hook void as required by NuttX.
       */

      _err("bk7258: AP platform preparation failed: %d\n", ret);
    }
#else
  int ret = bk7258_cp_bringup_initialize();

  if (ret < 0)
    {
      /* NuttX defines this hook as void.  The board runner preserves the
       * first mandatory failure so application bring-up can reject services
       * while the initial shell remains available for diagnosis.
       */

      _err("bk7258: CP platform degraded: %d; app bring-up disabled\n", ret);
    }
#endif
}
