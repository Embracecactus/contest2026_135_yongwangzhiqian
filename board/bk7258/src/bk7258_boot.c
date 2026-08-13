/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_boot.c
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

#include "bk7258_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void board_late_initialize(void)
{
  int ret = bk7258_platform_initialize();

  if (ret < 0)
    {
      _err("bk7258: mandatory platform initialization failed: %d\n", ret);
    }
}
