/****************************************************************************
 * Contest 2026 team 135 - RV1126B EVB local board header
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __SRC_RV1126B_EVB_H
#define __SRC_RV1126B_EVB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include "../include/board.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_bringup
 *
 * Description:
 *   Perform late board initialization, including the /proc mount when
 *   enabled.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int rv1126b_bringup(void);

/****************************************************************************
 * Name: rv1126b_rptun_init
 *
 * Description:
 *   Initialize the RV1126B RPTun driver for AMP communication with
 *   the Linux A-core.  Sets up mailbox notification and shared memory.
 *
 * Input Parameters:
 *   peername - Name of the remote CPU (typically "ap")
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int rv1126b_rptun_init(const char *peername);

#endif /* __SRC_RV1126B_EVB_H */
