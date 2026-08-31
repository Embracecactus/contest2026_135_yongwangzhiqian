/****************************************************************************
 * chips/bk7258/common/bk7258_lowputc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 polled, compile-time selected UART console.
 *
 * arm_lowputc(ch) and up_putc(ch) push a single byte out by polling
 * the TX-ready bit and writing the FIFO data port -- exactly the freestanding
 * sequence used by verified platform bring-up and the N1 banner.  These are
 * the chip-level polled primitives; the serial
 * lower-half in bk7258_serial.c reuses the same MMIO via its own send/txready
 * ops and calls arm_lowputc() for the console's poll path.
 *
 * The UART starts from the Tier-1 bootloader's 26 MHz XTAL setup when BL1
 * uses the same console, and can also establish its own selected pinmux.
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
#include <arch/chip/bk7258_console.h>

#ifdef BK7258_HAVE_UART_CONSOLE

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SYS_CLK_SELECT_REG   0x44010020u
#define BK7258_SYS_CLK_ENABLE_REG   0x44010030u
#define BK7258_SYS_GPIO0_7_FUNC_REG    0x440100c0u
#define BK7258_SYS_GPIO8_15_FUNC_REG   0x440100c4u
#define BK7258_SYS_GPIO24_31_FUNC_REG  0x440100ccu
#define BK7258_SYS_GPIO40_47_FUNC_REG  0x440100d4u
#define BK7258_GPIO_CTRL_BASE          0x44000400u
#define BK7258_GPIO_UART_CTRL_MASK     0x0000007cu
#define BK7258_GPIO_UART_CTRL          0x00000078u

#if defined(CONFIG_BK7258_CONSOLE_UART0)
#  define BK7258_CONSOLE_FUNC_REG      BK7258_SYS_GPIO8_15_FUNC_REG
#  define BK7258_CONSOLE_FUNC_MASK     0x0000ff00u
#  define BK7258_CONSOLE_FUNC_VALUE    0x00000000u
#  define BK7258_CONSOLE_TX_PIN        11u
#  define BK7258_CONSOLE_RX_PIN        10u
#elif defined(CONFIG_BK7258_CONSOLE_UART1)
#  define BK7258_CONSOLE_FUNC_REG      BK7258_SYS_GPIO0_7_FUNC_REG
#  define BK7258_CONSOLE_FUNC_MASK     0x000000ffu
#  define BK7258_CONSOLE_FUNC_VALUE    0x00000000u
#  define BK7258_CONSOLE_TX_PIN        0u
#  define BK7258_CONSOLE_RX_PIN        1u
#elif defined(CONFIG_BK7258_CONSOLE_UART2) && \
      defined(CONFIG_BK7258_UART2_PINS_P40_P41)
#  define BK7258_CONSOLE_FUNC_REG      BK7258_SYS_GPIO40_47_FUNC_REG
#  define BK7258_CONSOLE_FUNC_MASK     0x000000ffu
#  define BK7258_CONSOLE_FUNC_VALUE    0x00000000u
#  define BK7258_CONSOLE_TX_PIN        41u
#  define BK7258_CONSOLE_RX_PIN        40u
#else
#  define BK7258_CONSOLE_FUNC_REG      BK7258_SYS_GPIO24_31_FUNC_REG
#  define BK7258_CONSOLE_FUNC_MASK     0xff000000u
#  define BK7258_CONSOLE_FUNC_VALUE    0x11000000u
#  define BK7258_CONSOLE_TX_PIN        31u
#  define BK7258_CONSOLE_RX_PIN        30u
#endif

#define BK7258_GPIO_CTRL(pin) \
  (BK7258_GPIO_CTRL_BASE + ((uintptr_t)(pin) << 2))

_Static_assert(BK7258_CONSOLE_CLK_DIV <= 0xffffu,
               "BK7258 UART console divider exceeds hardware field");

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* bk_uart_init() logs through arm_lowputc() immediately before it removes
 * and recreates the selected UART pin mapping.  During that short ownership
 * handoff, wait for each byte to leave the FIFO so the SDK cannot unmap TX
 * with a byte still in flight.  Normal runtime lowputc keeps the cheaper
 * FIFO-write-ready behavior.
 */

static bool g_bk7258_uart_handoff;
static unsigned int g_bk7258_uart_config = BK7258_CONSOLE_CONFIG_VALUE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_lowputc_console_valid(void)
{
  return getreg32(BK7258_CONSOLE_GLOBAL_CTRL) ==
           BK7258_UART_GLOBAL_ENABLE &&
         getreg32(BK7258_CONSOLE_CONFIG) == g_bk7258_uart_config &&
         (getreg32(BK7258_CONSOLE_FIFO_CONFIG) &
          BK7258_UART_RX_THRESHOLD_MASK) == BK7258_UART_RX_THRESHOLD;
}

static void bk7258_lowputc_restore_pinmux(void)
{
  unsigned int regval;
  uintptr_t address;

  regval = getreg32(BK7258_CONSOLE_FUNC_REG);
  regval = (regval & ~BK7258_CONSOLE_FUNC_MASK) |
           BK7258_CONSOLE_FUNC_VALUE;
  putreg32(regval, BK7258_CONSOLE_FUNC_REG);

  address = BK7258_GPIO_CTRL(BK7258_CONSOLE_TX_PIN);
  regval = getreg32(address);
  putreg32((regval & ~BK7258_GPIO_UART_CTRL_MASK) |
           BK7258_GPIO_UART_CTRL, address);

  address = BK7258_GPIO_CTRL(BK7258_CONSOLE_RX_PIN);
  regval = getreg32(address);
  putreg32((regval & ~BK7258_GPIO_UART_CTRL_MASK) |
           BK7258_GPIO_UART_CTRL, address);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_lowputc_handoff
 *
 * Description:
 *   Select fully drained writes while the SDK takes UART ownership from the
 *   bootloader/early-console path.  This is intentionally board-private.
 *
 ****************************************************************************/

void bk7258_lowputc_handoff(bool enable)
{
  g_bk7258_uart_handoff = enable;
}

int bk7258_lowputc_set_format(uint32_t baud, unsigned int data_bits,
                              unsigned int parity, unsigned int stop_bits)
{
  unsigned int divider;
  unsigned int config;

  if (baud < 400u || baud > 5200000u || data_bits < 5u ||
      data_bits > 8u || parity > 2u || stop_bits < 1u || stop_bits > 2u)
    {
      return -1;
    }

  divider = (BK7258_UART_XTAL_HZ / baud) - 1u;
  if (divider > 0xffffu)
    {
      return -1;
    }

  config = 0x3u | ((data_bits - 5u) << 3) | (divider << 8);
  if (parity != 0u)
    {
      config |= 1u << 5;
      if (parity == 1u)
        {
          config |= 1u << 6;
        }
    }

  if (stop_bits == 2u)
    {
      config |= 1u << 7;
    }

  g_bk7258_uart_config = config;
  return 0;
}

/****************************************************************************
 * Name: bk7258_lowputc_restore_console
 *
 * Description:
 *   Restore the selected UART console hardware invariant without depending on
 *   SDK driver state.  Preserve the caller's interrupt-enable value so the
 *   helper remains valid before and after the NuttX serial lower half is
 *   registered.  Mask and clear first: PHY/controller startup can leave the
 *   RX-enable bits live while the UART configuration itself has been reset.
 *
 ****************************************************************************/

void bk7258_lowputc_restore_console(void)
{
  unsigned int int_enable = getreg32(BK7258_CONSOLE_INT_ENABLE);
  unsigned int count;
  unsigned int regval;

  putreg32(0, BK7258_CONSOLE_INT_ENABLE);
  putreg32(BK7258_UART_INT_STATUS_ALL, BK7258_CONSOLE_INT_STATUS);

  bk7258_lowputc_restore_pinmux();

  regval = getreg32(BK7258_SYS_CLK_ENABLE_REG);
  regval |= BK7258_CONSOLE_CLK_ENABLE_BIT;
  putreg32(regval, BK7258_SYS_CLK_ENABLE_REG);

  regval = getreg32(BK7258_SYS_CLK_SELECT_REG);
  regval &= ~BK7258_CONSOLE_CLK_SELECT_BIT;
  putreg32(regval, BK7258_SYS_CLK_SELECT_REG);

  putreg32(BK7258_UART_GLOBAL_ENABLE, BK7258_CONSOLE_GLOBAL_CTRL);
  putreg32(g_bk7258_uart_config, BK7258_CONSOLE_CONFIG);
  putreg32(BK7258_UART_RX_THRESHOLD, BK7258_CONSOLE_FIFO_CONFIG);

  /* A peripheral reset can leave one pre-reset/partial byte in the hardware
   * RX FIFO.  It is not reported until the first real command arrives, at
   * which point NSH sees a garbage prefix.  The UART block has no FIFO-reset
   * bit, so consume the documented RX data port while fifo_rd_ready is set.
   * Any byte received while the configuration invariant was invalid is not
   * trustworthy; bound the drain independently of the reported FIFO count.
   */

  for (count = 0; count < BK7258_UART_RX_DRAIN_LIMIT; count++)
    {
      if ((getreg32(BK7258_CONSOLE_FIFO_STATUS) &
           BK7258_UART_RX_READY) == 0)
        {
          break;
        }

      (void)getreg32(BK7258_CONSOLE_FIFO_PORT);
    }

  putreg32(BK7258_UART_INT_STATUS_ALL, BK7258_CONSOLE_INT_STATUS);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  putreg32(int_enable, BK7258_CONSOLE_INT_ENABLE);
}

/****************************************************************************
 * Name: bk7258_lowputc_ensure_console
 *
 * Description:
 *   Repair the selected UART when an SDK path invalidates the console
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
 *   Output one byte on the selected UART, polling the TX-ready bit.
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

  for (count = 0; count < BK7258_UART_TX_POLL_LIMIT; count++)
    {
      if ((getreg32(BK7258_CONSOLE_FIFO_STATUS) &
           BK7258_UART_TX_READY) != 0)
        {
          break;
        }
    }

  putreg32((unsigned int)((unsigned char)ch), BK7258_CONSOLE_FIFO_PORT);

  if (g_bk7258_uart_handoff)
    {
      for (count = 0; count < BK7258_UART_TX_POLL_LIMIT; count++)
        {
          if ((getreg32(BK7258_CONSOLE_FIFO_STATUS) &
               BK7258_UART_TX_EMPTY) != 0)
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

#else /* BK7258_HAVE_UART_CONSOLE */

/* AP images and deliberately silent CP profiles still need the ARM low-level
 * output hooks when generic NuttX serial/syslog support is enabled.  They do
 * not own any physical UART pins, so keep the hooks side-effect free.
 */

void arm_lowputc(char ch)
{
  (void)ch;
}

void arm_lowputs(const char *str)
{
  (void)str;
}

void up_putc(int ch)
{
  (void)ch;
}

#endif /* BK7258_HAVE_UART_CONSOLE */
