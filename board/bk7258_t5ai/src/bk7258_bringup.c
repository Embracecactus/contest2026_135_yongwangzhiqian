/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/src/bk7258_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board bringup for the Beken BK7258 (T5-AI) NuttX port.
 *
 * board_app_initialize() is the NSH application-init hook: when
 * CONFIG_NSH_ARCHINIT=y, nsh_initialize() issues boardctl(BOARDIOC_INIT)
 * and the NSH init task reaches this function during nx_start().  Stage N3
 * mounts procfs at /proc here so ps, ls /proc, and cat of /proc entries
 * work; later stages (MTD, filesystems, SMP) extend this bring-up.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mount.h>
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
 *   Standard NuttX board Application-level initialization hook, reached via
 *   CONFIG_NSH_ARCHINIT from the NSH init task.  Mounts procfs at
 *   CONFIG_NSH_PROC_MOUNTPOINT (default /proc) and returns OK.
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

  /* Mount procfs at the NSH proc mountpoint so ps, ls /proc, and cat of
   * /proc entries work.  CONFIG_NSH_ARCHINIT activates this hook;
   * CONFIG_FS_PROCFS provides the filesystem.
   */

  if (mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL) < 0)
    {
      bk7258_bringup_diag_putc('p');   /* procfs mount failed */
    }
  else
    {
      bk7258_bringup_diag_putc('P');   /* procfs mounted at /proc */
    }

  return 0;
}
