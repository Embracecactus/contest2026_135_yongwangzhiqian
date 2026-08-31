/****************************************************************************
 * chips/bk7258/common/
 * bk7258_rpmsgfs.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-side mount wrapper for the stock NuttX RPMsgFS client.  CP remains the
 * sole owner of flash, MTD and the LittleFS mounted at /data.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_RPMSGFS) && defined(CONFIG_BK7258_AP_CORE)

#include <errno.h>
#include <stdbool.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "bk7258_rptun.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_RPMSGFS_MOUNTPOINT  "/cpdata"
#define BK7258_RPMSGFS_OPTIONS     "cpu=cp,fs=/data,timeout=3000"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_rpmsgfs_initialize(void)
{
  static bool initialized;

  if (initialized)
    {
      return OK;
    }

  if (mkdir(BK7258_RPMSGFS_MOUNTPOINT, 0777) < 0 &&
      get_errno() != EEXIST)
    {
      return -get_errno();
    }

  /* mount() only installs the client and registers its RPMsg callback.  It
   * does not perform remote file I/O here, so AP READY is never held behind
   * the stock client's unbounded per-request rpmsg_wait().  The N11 worker
   * starts actual operations only after a bounded CP request on a connected
   * generation.
   */

  if (mount(NULL, BK7258_RPMSGFS_MOUNTPOINT, "rpmsgfs", 0,
            BK7258_RPMSGFS_OPTIONS) < 0)
    {
      return -get_errno();
    }

  initialized = true;
  return OK;
}

#endif /* CONFIG_BK7258_RPMSGFS && CONFIG_BK7258_AP_CORE */
