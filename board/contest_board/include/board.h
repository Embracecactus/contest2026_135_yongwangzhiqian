/****************************************************************************
 * Contest 2026 team 135 - RV1126B EVB board header
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARD_H
#define __BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LEDs are not currently supported. */

#define BOARD_NLEDS 0

/* Buttons are not currently supported. */

#define BOARD_NBUTTONS 0

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: boardinitialize
 *
 * Description:
 *   Called from chip start code very early in the boot sequence.  The current
 *   board hook requires no additional early initialization; chip startup
 *   configures the console and clocks before this point.
 *
 ****************************************************************************/

void boardinitialize(void);

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   Called during OS initialization after the OS has been initialized but
 *   before the init daemon is started.  This is where late board resources,
 *   such as filesystems and board-specific drivers, are set up.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void);
#endif

#endif /* __BOARD_H */
