/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/common/bk7258_lowputc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, Cortex-M33) polled UART1 low-level output for NuttX
 * Stage N2.
 *
 * arm_lowputc(ch) and up_putc(ch) push a single byte out of UART1 by polling
 * the TX-ready bit and writing the FIFO data port -- exactly the freestanding
 * sequence the verified probe (docs/bk7258-t5ai/probe/probe.c) and the N1
 * banner use.  These are the chip-level polled primitives; the serial
 * lower-half in bk7258_serial.c reuses the same MMIO via its own send/txready
 * ops and calls arm_lowputc() for the console's poll path.
 *
 * UART1 starts from the Tier-1 bootloader's 26 MHz XTAL/clock/pinmux setup.
 * The SDK serial lower-half later takes ownership and restores its
 * board-verified 0x0000371b configuration for 460800 8N1.  This file only
 * performs bounded FIFO polling and never changes those clock/config values.
 *
 * Register layout (cp/middleware/soc/bk7258/soc/uart_struct.h):
 *   0x45830018  fifo_status   bit20 fifo_wr_ready (TX FIFO not full)
 *                             bit21 fifo_rd_ready (RX FIFO has data)
 *   0x4583001C  fifo_port     TX write / RX read (shared data word)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_UART1_FIFO_STAT   (*(volatile unsigned int *)0x45830018u)
#define BK7258_UART1_FIFO_PORT   (*(volatile unsigned int *)0x4583001Cu)
#define BK7258_UART1_TX_READY    (1u << 20)   /* fifo_status.bit20 = fifo_wr_ready */
#define BK7258_UART1_TX_EMPTY    (1u << 17)   /* fifo_status.bit17 = tx_fifo_empty */
#define BK7258_UART1_TX_POLL_LIMIT 100000u

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* bk_uart_init() logs through arm_lowputc() immediately before it removes
 * and recreates the GPIO0 UART1 pin mapping.  During that short ownership
 * handoff, wait for each byte to leave the FIFO so the SDK cannot unmap TX
 * with a byte still in flight.  Normal runtime lowputc keeps the cheaper
 * FIFO-write-ready behavior.
 */

static bool g_bk7258_uart_handoff;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_lowputc_handoff
 *
 * Description:
 *   Select fully drained writes while the SDK takes UART1 ownership from the
 *   bootloader/early-console path.  This is intentionally board-private.
 *
 ****************************************************************************/

void bk7258_lowputc_handoff(bool enable)
{
  g_bk7258_uart_handoff = enable;
}

/****************************************************************************
 * Name: arm_lowputc
 *
 * Description:
 *   Output one byte on UART1, polling the TX-ready bit.  This is the
 *   chip-level primitive invoked before the serial driver is registered
 *   (e.g. from up_putc / OS debug output).
 *
 ****************************************************************************/

void arm_lowputc(char ch)
{
  unsigned int count;

  /* The SDK UART initialization path temporarily rewrites the UART clock and
   * configuration while it is still emitting syslog output through this
   * function.  During that transition TX_READY can remain low.  Use the same
   * bounded write-through policy as the board-verified Tier-1 bootloader so a
   * transient console failure cannot trap startup until the NMI watchdog
   * fires; bk7258_uart_setup() restores the board console immediately after
   * the SDK call returns.
   */

  for (count = 0; count < BK7258_UART1_TX_POLL_LIMIT; count++)
    {
      if ((BK7258_UART1_FIFO_STAT & BK7258_UART1_TX_READY) != 0)
        {
          break;
        }
    }

  BK7258_UART1_FIFO_PORT = (unsigned int)((unsigned char)ch);

  if (g_bk7258_uart_handoff)
    {
      for (count = 0; count < BK7258_UART1_TX_POLL_LIMIT; count++)
        {
          if ((BK7258_UART1_FIFO_STAT & BK7258_UART1_TX_EMPTY) != 0)
            {
              break;
            }
        }
    }
}

/****************************************************************************
 * Name: arm_lowputs
 *
 * Description:
 *   Convenience helper: emit a NUL-terminated string via arm_lowputc.
 *
 ****************************************************************************/

void arm_lowputs(const char *str)
{
  while (*str)
    {
      arm_lowputc(*str++);
    }
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Provide priority, low-level access to support OS debug writes.  By NuttX
 *   ARM convention this is defined directly by each chip (no canonical
 *   header declares it); the signature matches nuttx/include/nuttx/arch.h
 *   and every other in-tree Cortex-M chip.
 *
 ****************************************************************************/

void up_putc(int ch)
{
  arm_lowputc((char)ch);
}
