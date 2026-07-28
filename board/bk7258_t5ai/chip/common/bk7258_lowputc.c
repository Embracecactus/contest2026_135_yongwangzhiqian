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
 * UART1 configuration (pinmux, 26 MHz XTAL, clock gate, global_ctrl, CFG
 * divider) is INHERITED from the Tier-1 bootloader and is never reprogrammed
 * here; the observed CFG is 0x00003719 (clk_div=0x37=55) which yields
 * 26 MHz / (clk_div + 1) = 26 MHz / 56 = 464286 Hz ~= 460800 baud.
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

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_UART1_FIFO_STAT   (*(volatile unsigned int *)0x45830018u)
#define BK7258_UART1_FIFO_PORT   (*(volatile unsigned int *)0x4583001Cu)
#define BK7258_UART1_TX_READY    (1u << 20)   /* fifo_status.bit20 = fifo_wr_ready */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

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
  while ((BK7258_UART1_FIFO_STAT & BK7258_UART1_TX_READY) == 0)
    {
    }

  BK7258_UART1_FIFO_PORT = (unsigned int)((unsigned char)ch);
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
