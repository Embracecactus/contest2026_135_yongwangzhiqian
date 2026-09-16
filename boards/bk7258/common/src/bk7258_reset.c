/****************************************************************************
 * boards/bk7258/common/src/bk7258_reset.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP boardctl binding for a coordinated whole-device BK7258 reset.
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/chip/bk7258_system_reset.h>

int board_reset(int status)
{
  (void)status;
  bk7258_system_reset(BK7258_RESET_SOURCE_REBOOT);
}
