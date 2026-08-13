/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_appinit.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX application-initialization entry point for the Beken BK7258 board.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "bk7258_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Register application-facing procfs/MTD/filesystem services.  Mandatory
 *   SDK, IPC, PM and AP lifecycle initialization is owned by
 *   board_late_initialize() and does not depend on NSH.
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  (void)arg;
  return bk7258_bringup();
}
