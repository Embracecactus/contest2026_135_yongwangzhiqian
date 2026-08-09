/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_lowputc.c
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
 * board-verified 0x0000371b configuration for 460800 8N1.  Some SDK
 * subsystems reset the shared UART block asynchronously after their public
 * initialization call has returned.  The polled path therefore verifies the
 * same fixed board invariant before use and repairs it with direct MMIO when
 * necessary.
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

#define BK7258_SYS_CLK_SELECT_REG   0x44010020u
#define BK7258_SYS_CLK_ENABLE_REG   0x44010030u
#define BK7258_SYS_UART1_CLK_SELECT (1u << 13)
#define BK7258_SYS_UART1_CLK_ENABLE (1u << 10)

#define BK7258_UART1_GLOBAL_CTRL    0x45830008u
#define BK7258_UART1_CONFIG         0x45830010u
#define BK7258_UART1_FIFO_CONFIG    0x45830014u
#define BK7258_UART1_FIFO_STAT_REG  0x45830018u
#define BK7258_UART1_FIFO_PORT_REG  0x4583001cu
#define BK7258_UART1_INT_ENABLE     0x45830020u
#define BK7258_UART1_INT_STATUS     0x45830024u

#define BK7258_UART1_GLOBAL_ENABLE  0x00000001u
#define BK7258_UART1_CONFIG_460800  0x0000371bu
#define BK7258_UART1_RX_THRESHOLD   0x00000100u
#define BK7258_UART1_RX_THRESHOLD_M 0x0000ff00u
#define BK7258_UART1_INT_STATUS_ALL 0x000000ffu

#define BK7258_UART1_FIFO_STAT \
  (*(volatile unsigned int *)BK7258_UART1_FIFO_STAT_REG)
#define BK7258_UART1_FIFO_PORT \
  (*(volatile unsigned int *)BK7258_UART1_FIFO_PORT_REG)
#define BK7258_UART1_TX_READY    (1u << 20)   /* fifo_status.bit20 = fifo_wr_ready */
#define BK7258_UART1_TX_EMPTY    (1u << 17)   /* fifo_status.bit17 = tx_fifo_empty */
#define BK7258_UART1_RX_READY    (1u << 21)   /* fifo_status.bit21 = fifo_rd_ready */
#define BK7258_UART1_RX_DRAIN_LIMIT 256u
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
 * Private Functions
 ****************************************************************************/

static bool bk7258_lowputc_console_valid(void)
{
  return getreg32(BK7258_UART1_GLOBAL_CTRL) ==
           BK7258_UART1_GLOBAL_ENABLE &&
         getreg32(BK7258_UART1_CONFIG) == BK7258_UART1_CONFIG_460800 &&
         (getreg32(BK7258_UART1_FIFO_CONFIG) &
          BK7258_UART1_RX_THRESHOLD_M) == BK7258_UART1_RX_THRESHOLD;
}

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
 * Name: bk7258_lowputc_restore_console
 *
 * Description:
 *   Restore the fixed UART1 console hardware invariant without depending on
 *   SDK driver state.  Preserve the caller's interrupt-enable value so the
 *   helper remains valid before and after the NuttX serial lower half is
 *   registered.  Mask and clear first: PHY/controller startup can leave the
 *   RX-enable bits live while the UART configuration itself has been reset.
 *
 ****************************************************************************/

void bk7258_lowputc_restore_console(void)
{
  unsigned int int_enable = getreg32(BK7258_UART1_INT_ENABLE);
  unsigned int count;
  unsigned int regval;

  putreg32(0, BK7258_UART1_INT_ENABLE);
  putreg32(BK7258_UART1_INT_STATUS_ALL, BK7258_UART1_INT_STATUS);

  regval = getreg32(BK7258_SYS_CLK_ENABLE_REG);
  regval |= BK7258_SYS_UART1_CLK_ENABLE;
  putreg32(regval, BK7258_SYS_CLK_ENABLE_REG);

  regval = getreg32(BK7258_SYS_CLK_SELECT_REG);
  regval &= ~BK7258_SYS_UART1_CLK_SELECT;
  putreg32(regval, BK7258_SYS_CLK_SELECT_REG);

  putreg32(BK7258_UART1_GLOBAL_ENABLE, BK7258_UART1_GLOBAL_CTRL);
  putreg32(BK7258_UART1_CONFIG_460800, BK7258_UART1_CONFIG);
  putreg32(BK7258_UART1_RX_THRESHOLD, BK7258_UART1_FIFO_CONFIG);

  /* A peripheral reset can leave one pre-reset/partial byte in the hardware
   * RX FIFO.  It is not reported until the first real command arrives, at
   * which point NSH sees a garbage prefix.  The UART block has no FIFO-reset
   * bit, so consume the documented RX data port while fifo_rd_ready is set.
   * Any byte received while the configuration invariant was invalid is not
   * trustworthy; bound the drain independently of the reported FIFO count.
   */

  for (count = 0; count < BK7258_UART1_RX_DRAIN_LIMIT; count++)
    {
      if ((getreg32(BK7258_UART1_FIFO_STAT_REG) &
           BK7258_UART1_RX_READY) == 0)
        {
          break;
        }

      (void)getreg32(BK7258_UART1_FIFO_PORT_REG);
    }

  putreg32(BK7258_UART1_INT_STATUS_ALL, BK7258_UART1_INT_STATUS);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  putreg32(int_enable, BK7258_UART1_INT_ENABLE);
}

/****************************************************************************
 * Name: bk7258_lowputc_ensure_console
 *
 * Description:
 *   Repair UART1 only when an SDK path has invalidated the board console
 *   invariant.  The full serial lower half uses the same final-point-of-use
 *   check before entering bk_uart_write_bytes().
 *
 ****************************************************************************/

void bk7258_lowputc_ensure_console(void)
{
  if (!bk7258_lowputc_console_valid())
    {
      bk7258_lowputc_restore_console();
    }
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
   * bounded write-through policy as the verified Tier-1 bootloader so a
   * transient console failure cannot trap startup until the NMI watchdog
   * fires.  A later SDK worker can also reset the UART after its public init
   * call returned, so repair the invariant at the final point of use.
   */

  bk7258_lowputc_ensure_console();

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
