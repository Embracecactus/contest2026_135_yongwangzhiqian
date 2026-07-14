/****************************************************************************
 * NuttX - RV1126B BSP
 ****************************************************************************/

#ifndef __BOARD_CHIP_RV1126B_H
#define __BOARD_CHIP_RV1126B_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "rv1126b_config.h"
#include "hardware/rv1126b_memorymap.h"
#include "hardware/rv1126b_uart.h"
#include "hardware/rv1126b_gpio.h"
#include "hardware/rv1126b_timer.h"
#include "hardware/rv1126b_intmux.h"
#include "hardware/rv1126b_cru.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: rv1126b_clockconfig
 *
 * Description:
 *   Verify that the preconfigured system clocks are ready.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_BOARD_EARLY_INITIALIZE
void rv1126b_clockconfig(void);
#endif

/****************************************************************************
 * Name: rv1126b_lowsetup
 *
 * Description:
 *   Early UART5 initialization for low-level debug output.
 *
 ****************************************************************************/

void rv1126b_lowsetup(void);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __BOARD_CHIP_RV1126B_H */
