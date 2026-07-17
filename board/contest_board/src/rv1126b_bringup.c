/****************************************************************************
 * Contest 2026 team 135 - RV1126B EVB late board initialization
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <sys/mount.h>
#include <syslog.h>
#include <nuttx/board.h>

#include "rv1126b_evb.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_bringup
 *
 * Description:
 *   Perform late board initialization: mount /proc filesystem, register
 *   device drivers, and perform any other setup needed after the OS is
 *   running but before user-space starts.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int rv1126b_bringup(void)
{
  int ret = OK;

  /* Mount the /proc filesystem */

#ifdef CONFIG_FS_PROCFS
  ret = mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount /proc: %d\n", ret);
    }
#endif

  /* Initialize RPTun for AMP communication with Linux A-core */

#ifdef CONFIG_RPTUN
  ret = rv1126b_rptun_init("ap");
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: rv1126b_rptun_init failed: %d\n", ret);
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Perform application-specific initialization.  This function is called
 *   by boardctl() when the boardctl BOARDIOC_INIT command is received.
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  (void)arg;

  return OK;
}
