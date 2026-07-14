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

#endif /* __SRC_RV1126B_EVB_H */
