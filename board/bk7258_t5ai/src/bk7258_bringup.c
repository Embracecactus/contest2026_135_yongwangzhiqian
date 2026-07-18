/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal board bringup for the Beken BK7258 (T5-AI) Stage N1 port.
 *
 * N1 never reaches nx_start() (the chip __start prints the banner and
 * halts), so no board-level initialisation ever runs at runtime.  We still
 * provide board_app_initialize() because NuttX's board library build
 * expects at least one board source file to exist, and the prototype in
 * include/nuttx/board.h must be satisfied for the link to succeed even if
 * the function is dead code.
 *
 * Stage N2 will replace this with a real bringup (console, MTD, etc.).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <stdint.h>
#include <nuttx/board.h>

/* UART1 MMIO for the boot-trace marker pushed at the top of
 * board_app_initialize().  Freestanding polled putc (polls fifo_status.bit20,
 * writes fifo_port); identical to start.c::bk7258_early_putc and
 * vectors.c::bk7258_fault_putc.  Local to this translation unit so it
 * introduces no new linkage dependency.
 */

#define BK7258_BRG_UART1_FSTAT   (*(volatile uint32_t *)0x45830018u)
#define BK7258_BRG_UART1_FPORT   (*(volatile uint32_t *)0x4583001Cu)
#define BK7258_BRG_UART1_READY   (1u << 20)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Bare MMIO single-byte marker.  Emits 'A' at function entry of
 * board_app_initialize() so board-side observation can confirm the NSH init
 * task reached the board-application bring-up hook during nx_start().
 */

static void bk7258_bringup_diag_putc(unsigned char c)
{
  while ((BK7258_BRG_UART1_FSTAT & BK7258_BRG_UART1_READY) == 0)
    {
    }

  BK7258_BRG_UART1_FPORT = (uint32_t)(c & 0xffu);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Standard NuttX board Application-level initialization hook.  N1 has
 *   nothing to do; return OK so any future caller proceeds.
 *
 * Input Parameters:
 *   arg - Board-specific argument (unused).
 *
 * Returned Value:
 *   Zero (OK) on success.
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  /* Boot-trace marker: reached board_app_initialize() from the NSH init
   * task spawned by nx_start().
   */

  bk7258_bringup_diag_putc('A');

  return 0;
}
