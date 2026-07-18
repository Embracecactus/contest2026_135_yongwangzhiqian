/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_serial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, Cortex-M33) serial lower-half for UART1.
 *
 * Stage N2 console driver.  Implements the NuttX uart_ops_s / uart_dev_s
 * "lower half" on top of the BK7258 UART1 FIFO registers.  RX is
 * interrupt-driven: rxint() opens three interrupt gates (peripheral-local
 * int_enable -> on-chip SYS_CPU0 int ctrl -> Cortex-M33 NVIC), the ISR
 * clears the RX status bits and calls uart_recvchars() to drain the FIFO
 * into the upper-half receive ring.  TX stays polled: the existing
 * synchronous-drain hack in txint() works well for an interactive NSH
 * console, so it is intentionally retained.
 *
 * UART1 configuration (pinmux, 26 MHz XTAL, clock gate, CFG divider) is
 * INHERITED from the Tier-1 bootloader -- this driver never touches
 * UART1_CFG (0x45830010).  Observed CFG 0x00003719 (clk_div=0x37) gives
 * 26 MHz / 56 ~= 460800 baud (board-side to be confirmed).
 *
 * Register layout (cp/middleware/soc/bk7258/soc/uart_struct.h):
 *   0x45830018  fifo_status   bit20 fifo_wr_ready (TX FIFO not full)
 *                             bit21 fifo_rd_ready (RX FIFO has data)
 *   0x4583001C  fifo_port     bits[0:7] TX write, bits[8:15] RX read
 *   0x45830020  int_enable    bit1 rx_fifo_need_read, bit6 rx_finish
 *   0x45830024  int_status    same bit layout, WRITE-1-TO-CLEAR
 *
 * On-chip interrupt controller (between UART peripheral and NVIC):
 *   0x44010080  SYS_CPU0_INT_0_31_EN   bit15 = UART1
 *
 * arm_serialinit() is invoked automatically by nuttx/arch/arm/src/common/
 * arm_initialize.c (up_initialize); we only provide it here.  Similarly
 * arm_earlyserialinit() is called from __start under USE_EARLYSERIALINIT.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/fs/fs.h>
#include <nuttx/serial/serial.h>
#include <arch/irq.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* UART1 MMIO. */

#define BK7258_UART1_BASE        0x45830000u
#define BK7258_UART1_FIFO_STAT   (*(volatile uint32_t *)(BK7258_UART1_BASE + 0x18u))
#define BK7258_UART1_FIFO_PORT   (*(volatile uint32_t *)(BK7258_UART1_BASE + 0x1Cu))
#define BK7258_UART1_CFG         (*(volatile uint32_t *)(BK7258_UART1_BASE + 0x10u))
#define BK7258_UART1_FIFO_CFG    (*(volatile uint32_t *)(BK7258_UART1_BASE + 0x14u))

#define BK7258_UART_CFG_RX_ENABLE (1u << 1)   /* CFG bit[1] rx_enable */

#define BK7258_UART1_TX_READY    (1u << 20)   /* fifo_wr_ready */
#define BK7258_UART1_RX_READY    (1u << 21)   /* fifo_rd_ready */

/* UART1 interrupt register block (cp/middleware/soc/bk7258/soc/uart_struct.h).
 *   0x45830020  int_enable    bit1 rx_fifo_need_read  (primary RX trigger)
 *                             bit6 rx_finish
 *   0x45830024  int_status    same bit layout; WRITE-1-TO-CLEAR
 *
 * int_status is always written with a constant mask (never RMW) so that
 * unrelated bits are left untouched.
 */

#define BK7258_UART1_INT_ENABLE   (*(volatile uint32_t *)(BK7258_UART1_BASE + 0x20u))
#define BK7258_UART1_INT_STATUS   (*(volatile uint32_t *)(BK7258_UART1_BASE + 0x24u))

#define BK7258_UART_INT_RX_NEED_READ  (1u << 1)
#define BK7258_UART_INT_RX_OVERFLOW   (1u << 2)
#define BK7258_UART_INT_RX_PARITY     (1u << 3)
#define BK7258_UART_INT_RX_STOPBIT    (1u << 4)
#define BK7258_UART_INT_RX_FINISH     (1u << 6)

/* Constant mask written to int_status to clear every RX-related bit.  = 0x76 */

#define BK7258_UART_INT_RX_CLEAR \
  (BK7258_UART_INT_RX_NEED_READ | BK7258_UART_INT_RX_OVERFLOW | \
   BK7258_UART_INT_RX_PARITY    | BK7258_UART_INT_RX_STOPBIT | \
   BK7258_UART_INT_RX_FINISH)

/* On-chip interrupt controller gate between the UART peripheral and the
 * Cortex-M33 NVIC.  BK7258 adds this extra gating layer (documented in the
 * Armino SDK as SYS_CPU0_INT_0_31_EN); bit15 selects UART1.  Without this
 * gate the UART ISR is never taken even when the NVIC line and the
 * peripheral-local int_enable are both set.
 */

#define BK7258_SYS_CPU0_INT_EN    (*(volatile uint32_t *)0x44010080u)
#define BK7258_SYS_CPU0_INT_UART1 (1u << 15)

/* Freestanding polled single-byte marker for boot tracing at the entry of
 * arm_serialinit().  Mirrors start.c::bk7258_early_putc and
 * vectors.c::bk7258_fault_putc (poll fifo_status.bit20, write fifo_port).
 * Local to this translation unit so no new linkage dependency is added.
 */

#define BK7258_SIO_UART1_FSTAT   (*(volatile uint32_t *)0x45830018u)
#define BK7258_SIO_UART1_FPORT   (*(volatile uint32_t *)0x4583001Cu)
#define BK7258_SIO_UART1_READY   (1u << 20)

/* RX/TX ring buffer sizes.  Fixed (no per-port Kconfig needed); matches the
 * CMSDK default of 256 bytes, ample for an interactive NSH console.
 */

#define BK7258_UART1_RXBUFSIZE   256
#define BK7258_UART1_TXBUFSIZE   256

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Chip-private state for the UART port.  Only the base address is needed
 * today (UART1 is fixed); kept as a struct for parity with the CMSDK driver
 * and future extension to UART2/3.
 */

struct bk7258_uart_s
{
  uint32_t uartbase;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_uart_s g_bk7258_uart1priv =
{
  .uartbase = BK7258_UART1_BASE,
};

/* RX/TX ring buffers backing the console port. */

static char g_uart1rxbuffer[BK7258_UART1_RXBUFSIZE];
static char g_uart1txbuffer[BK7258_UART1_TXBUFSIZE];

/* Forward declaration: g_bk7258_uart_ops is defined below (after the op
 * prototypes); g_uart1port references its address in a static initialiser.
 */

static const struct uart_ops_s g_bk7258_uart_ops;

/* The single console port.  isconsole is set true in arm_earlyserialinit().
 * The upper-half serial driver initialises all semaphores/spinlocks.
 */

static struct uart_dev_s g_uart1port =
{
  .isconsole = false,
  .ops       = &g_bk7258_uart_ops,
  .priv      = &g_bk7258_uart1priv,
  .recv =
    {
      .size   = BK7258_UART1_RXBUFSIZE,
      .buffer = g_uart1rxbuffer,
    },
  .xmit =
    {
      .size   = BK7258_UART1_TXBUFSIZE,
      .buffer = g_uart1txbuffer,
    },
};

#define CONSOLE_DEV  g_uart1port

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  bk7258_uart_isr(int irq, FAR void *context, FAR void *arg);
static int  bk7258_uart_setup(struct uart_dev_s *dev);
static void bk7258_uart_shutdown(struct uart_dev_s *dev);
static int  bk7258_uart_attach(struct uart_dev_s *dev);
static void bk7258_uart_detach(struct uart_dev_s *dev);
static int  bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg);
static int  bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status);
static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_rxavailable(struct uart_dev_s *dev);
static void bk7258_uart_send(struct uart_dev_s *dev, int ch);
static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_txready(struct uart_dev_s *dev);
static bool bk7258_uart_txempty(struct uart_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* uart_ops_s.  RX is interrupt-driven (rxint opens the three interrupt
 * gates: peripheral-local -> on-chip int controller -> NVIC, then attaches
 * bk7258_uart_isr via attach()); TX stays polled because the existing
 * synchronous-drain hack in txint() works well for the NSH console.
 */

static const struct uart_ops_s g_bk7258_uart_ops =
{
  .setup       = bk7258_uart_setup,
  .shutdown    = bk7258_uart_shutdown,
  .attach      = bk7258_uart_attach,
  .detach      = bk7258_uart_detach,
  .ioctl       = bk7258_uart_ioctl,
  .receive     = bk7258_uart_receive,
  .rxint       = bk7258_uart_rxint,
  .rxavailable = bk7258_uart_rxavailable,
  .send        = bk7258_uart_send,
  .txint       = bk7258_uart_txint,
  .txready     = bk7258_uart_txready,
  .txempty     = bk7258_uart_txempty,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Bare MMIO single-byte marker.  Emits 'C' at function entry of
 * arm_serialinit() so board-side observation can confirm the /dev/console
 * registration path was reached during the nx_start() walk.
 */

static void bk7258_serial_diag_putc(unsigned char c)
{
  while ((BK7258_SIO_UART1_FSTAT & BK7258_SIO_UART1_READY) == 0)
    {
    }

  BK7258_SIO_UART1_FPORT = (uint32_t)(c & 0xffu);
}

/****************************************************************************
 * Name: bk7258_uart_isr
 *
 * Description:
 *   UART1 RX interrupt service routine.  Clears the RX interrupt bits in
 *   int_status (write-1-to-clear, constant mask) and then drains the RX
 *   FIFO into the upper-half receive ring via uart_recvchars(), which
 *   posts recvsem to wake any task blocked in read().
 *
 *   The 'arg' cookie is the uart_dev_s pointer handed to irq_attach() by
 *   bk7258_uart_attach().
 *
 ****************************************************************************/

static int bk7258_uart_isr(int irq, FAR void *context, FAR void *arg)
{
  FAR struct uart_dev_s *dev = (FAR struct uart_dev_s *)arg;

  /* Write-1-clear the RX interrupt bits.  Constant mask only -- do not
   * RMW the whole word, which would race against other bit fields.
   */

  BK7258_UART1_INT_STATUS = BK7258_UART_INT_RX_CLEAR;

  /* Drain the RX FIFO -> uart_recvchars() loops while rxavailable() and
   * pushes each byte into dev->recv, then posts recvsem.
   */

  uart_recvchars(dev);
  return OK;
}

/****************************************************************************
 * Name: bk7258_uart_setup
 ****************************************************************************/

static int bk7258_uart_setup(struct uart_dev_s *dev)
{
  /* UART1 was configured by the Tier-1 bootloader: pinmux (GPIO0 TXD /
   * GPIO1 RXD via 0x44000400/404), 26 MHz XTAL, clock gate, and the CFG
   * baud divider are all inherited and must NOT be rewritten (rewriting
   * CFG would clobber clk_div -> wrong baud).  BUT the bootloader only
   * ever printed, so it left CFG.rx_enable (bit1) = 0 -- the observed
   * bootloader CFG 0x00003719 sets tx_enable + data_bits + clk_div=0x37
   * but clears rx_enable.  Console input needs RX on, so OR in only bit1,
   * preserving every other field.
   */

  BK7258_UART1_CFG |= BK7258_UART_CFG_RX_ENABLE;

  /* The RX FIFO threshold (fifo_config bits[8:15]) gates when the
   * rx_fifo_need_read interrupt asserts.  Its reset default (0) makes the
   * condition "FIFO >= 0" true always, so the interrupt is asserted
   * continuously the instant RX is enabled -- an ISR storm even with the
   * FIFO empty (observed: 'R' prints forever at boot).  Set the threshold
   * to 1 byte so the interrupt fires exactly when a byte is available and
   * de-asserts once the FIFO is drained.  RMW only bits[8:15]; preserve
   * tx_threshold (bits[0:7]) and rx_stop_detect_time (bits[16:17]).
   */

  BK7258_UART1_FIFO_CFG = (BK7258_UART1_FIFO_CFG & ~(0xffu << 8)) |
                          (1u << 8);

  return OK;
}

/****************************************************************************
 * Name: bk7258_uart_shutdown
 ****************************************************************************/

static void bk7258_uart_shutdown(struct uart_dev_s *dev)
{
  /* Polled: nothing to disable. */
}

/****************************************************************************
 * Name: bk7258_uart_attach / detach
 ****************************************************************************/

static int bk7258_uart_attach(struct uart_dev_s *dev)
{
  /* Bind the UART1 vector slot (BK7258_IRQ_UART1 = 31) to our ISR.  The
   * ISR cookie is the upper-half uart_dev_s so it can call uart_recvchars().
   * The NVIC line itself is enabled later in rxint() together with the two
   * upstream gates.
   */

  return irq_attach(BK7258_IRQ_UART1, bk7258_uart_isr, dev);
}

static void bk7258_uart_detach(struct uart_dev_s *dev)
{
  /* No interrupts to detach. */
}

/****************************************************************************
 * Name: bk7258_uart_ioctl
 ****************************************************************************/

static int bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  /* No special ioctls for the polled BK7258 UART.  Defer everything to the
   * upper-half default (which returns -ENOTTY for unknown commands).
   */

  return -ENOTTY;
}

/****************************************************************************
 * Name: bk7258_uart_receive
 ****************************************************************************/

static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  if (status)
    {
      *status = 0;  /* no framing/parity error bits tracked */
    }

  /* fifo_port is the shared RX/TX data word.  bits [0:7]  = TX byte written
   * by the CPU; bits [8:15] = RX byte popped on read.  Reading bit8:15
   * returns the next byte from the RX FIFO.
   */

  return (int)((BK7258_UART1_FIFO_PORT >> 8) & 0xffu);
}

/****************************************************************************
 * Name: bk7258_uart_rxint
 *
 * Description:
 *   Enable/disable RX interrupts on UART1.  Three interrupt gates sit
 *   between the RX FIFO edge and the CPU, and ALL of them must be opened
 *   (in order) for the ISR to fire:
 *
 *     1. peripheral-local: BK7258_UART1_INT_ENABLE  (rx_need_read | rx_finish)
 *     2. on-chip int ctrl: BK7258_SYS_CPU0_INT_EN   (bit15 = UART1)
 *     3. Cortex-M33 NVIC:  up_enable_irq(BK7258_IRQ_UART1)
 *
 *   Race-fix: after the gates are open, if a byte is already waiting in
 *   the RX FIFO (received between the last polled drain and now) it would
 *   only trigger an IRQ on the NEXT byte's edge -- on a quiet console line
 *   that edge may never come, so the byte is lost.  Drain it now.  The
 *   enclosing critical section masks the IRQ, so this synchronous drain
 *   cannot race the real ISR.
 *
 ****************************************************************************/

static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  irqstate_t flags = enter_critical_section();

  if (enable)
    {
      /* Three interrupt gates sit between the RX FIFO edge and the CPU and
       * all must be opened (in order) for the ISR to fire:
       *   1. peripheral-local: BK7258_UART1_INT_ENABLE bit1 (rx_need_read)
       *   2. on-chip int ctrl: BK7258_SYS_CPU0_INT_EN bit15 (UART1)
       *   3. Cortex-M33 NVIC:  up_enable_irq(BK7258_IRQ_UART1)
       * Only rx_fifo_need_read is armed: with the RX FIFO threshold set to
       * 1 in setup(), it fires on the first byte and de-asserts on drain,
       * so rx_finish (bit6) is redundant.  TX stays polled.
       */

      BK7258_UART1_INT_ENABLE |= BK7258_UART_INT_RX_NEED_READ;
      BK7258_SYS_CPU0_INT_EN  |= BK7258_SYS_CPU0_INT_UART1;
      up_enable_irq(BK7258_IRQ_UART1);

      /* Race-fix: drain any byte that pre-dated the gate opening. */

      if (uart_rxavailable(dev))
        {
          uart_recvchars(dev);
        }
    }
  else
    {
      BK7258_UART1_INT_ENABLE &= ~BK7258_UART_INT_RX_NEED_READ;
      up_disable_irq(BK7258_IRQ_UART1);
    }

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: bk7258_uart_rxavailable
 ****************************************************************************/

static bool bk7258_uart_rxavailable(struct uart_dev_s *dev)
{
  return (BK7258_UART1_FIFO_STAT & BK7258_UART1_RX_READY) != 0;
}

/****************************************************************************
 * Name: bk7258_uart_send
 ****************************************************************************/

static void bk7258_uart_send(struct uart_dev_s *dev, int ch)
{
  /* Wait for the TX FIFO to accept a byte, then push it.  Identical to
   * arm_lowputc(); reuse the polled primitive.
   */

  arm_lowputc((char)ch);
}

/****************************************************************************
 * Name: bk7258_uart_txint
 ****************************************************************************/

static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable)
{
  /* There is no TX interrupt; fake one by draining the ring synchronously
   * whenever the upper half enables transmission.  This makes the console
   * write path blocking-polled (matches the CMSDK polled fallback).
   */

  if (enable)
    {
      uart_xmitchars(dev);
    }
}

/****************************************************************************
 * Name: bk7258_uart_txready / txempty
 ****************************************************************************/

static bool bk7258_uart_txready(struct uart_dev_s *dev)
{
  return (BK7258_UART1_FIFO_STAT & BK7258_UART1_TX_READY) != 0;
}

static bool bk7258_uart_txempty(struct uart_dev_s *dev)
{
  /* The BK7258 status register exposes "TX FIFO not full" (fifo_wr_ready);
   * there is no documented separate "TX FIFO empty" bit, so report the same
   * condition.  This is conservative for the upper-half drain logic.
   */

  return (BK7258_UART1_FIFO_STAT & BK7258_UART1_TX_READY) != 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_earlyserialinit
 *
 * Description:
 *   Perform low-level UART initialisation early in boot so the console is
 *   available during startup.  Called from __start() under
 *   USE_EARLYSERIALINIT, before nx_start().  Marks the console port and
 *   "sets it up" (polled: a no-op beyond marking isconsole).
 *
 ****************************************************************************/

#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void)
{
  CONSOLE_DEV.isconsole = true;
  bk7258_uart_setup(&CONSOLE_DEV);
}
#endif

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   Register the serial console.  Called automatically from up_initialize()
 *   (arm_initialize.c) after arm_earlyserialinit().
 *
 ****************************************************************************/

#ifdef USE_SERIALDRIVER
void arm_serialinit(void)
{
  /* Boot-trace marker: reached arm_serialinit() inside nx_start() -- the
   * /dev/console registration path is being executed.
   */

  bk7258_serial_diag_putc('C');

  (void)uart_register("/dev/console", &CONSOLE_DEV);
}
#endif
