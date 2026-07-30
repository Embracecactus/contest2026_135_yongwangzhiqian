/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/common/bk7258_serial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, Cortex-M33) UART1 serial lower-half.
 *
 * This driver is a wrapper over the Beken SDK UART API.  The SDK owns UART
 * clock, pinmux, register, and interrupt handling while the NuttX serial
 * upper half owns the RX and TX ring buffers.  The SDK software RX FIFO is
 * disabled so its ISR leaves received bytes in the hardware FIFO before
 * invoking bk7258_uart_sdk_isr().
 *
 * TX remains synchronously polled through bk_uart_write_bytes().  Early boot
 * output remains available through bk7258_lowputc.c; the full SDK UART setup
 * is deferred to arm_serialinit(), after NuttX memory and semaphore services
 * are available.  CONFIG_BK7258_SDK_IRQ_BRIDGE provides the SDK-to-NuttX IRQ
 * registration path used by bk_uart_init().
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <nuttx/fs/fs.h>
#include <nuttx/serial/serial.h>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/uart_types.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_UART_RXBUFSIZE       256
#define BK7258_UART_TXBUFSIZE       256
#define BK7258_CONSOLE_UART_ID      UART_ID_1
#define BK7258_UART_BAUD_RATE       460800u

#define BK7258_SYS_CLK_SELECT_REG   0x44010020u
#define BK7258_SYS_CLK_ENABLE_REG   0x44010030u
#define BK7258_SYS_UART1_CLK_SELECT (1u << 13)
#define BK7258_SYS_UART1_CLK_ENABLE (1u << 10)
#define BK7258_UART1_GLOBAL_CTRL    0x45830008u
#define BK7258_UART1_CONFIG         0x45830010u
#define BK7258_UART1_GLOBAL_ENABLE  0x00000001u
#define BK7258_UART1_CONFIG_460800  0x0000371bu

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* The SDK read API consumes a byte, while the NuttX upper half separates
 * rxavailable() from receive().  Keep one byte of lookahead per port.
 */

struct bk7258_uart_s
{
  uart_id_t id;
  int rxbyte;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_uart_s g_bk7258_uart1priv =
{
  .id     = BK7258_CONSOLE_UART_ID,
  .rxbyte = -1,
};

static char g_uart1rxbuffer[BK7258_UART_RXBUFSIZE];
static char g_uart1txbuffer[BK7258_UART_TXBUFSIZE];

static const struct uart_ops_s g_bk7258_uart_ops;

static struct uart_dev_s g_uart1port =
{
  .isconsole = false,
  .ops       = &g_bk7258_uart_ops,
  .priv      = &g_bk7258_uart1priv,
  .recv      =
    {
      .size   = BK7258_UART_RXBUFSIZE,
      .buffer = g_uart1rxbuffer,
    },
  .xmit      =
    {
      .size   = BK7258_UART_TXBUFSIZE,
      .buffer = g_uart1txbuffer,
    },
};

#define CONSOLE_DEV  g_uart1port

/* TEMP cold-reset diagnostic: emit a short raw-UART tag so the last
 * completed startup checkpoint remains visible without /dev/console,
 * printf or syslog.  Delete after diagnosis.
 */

static void cold_ckpt(const char *tag)
{
  up_putc('\r');
  up_putc('\n');
  while (*tag)
    {
      up_putc(*tag++);
    }

  up_putc('\r');
  up_putc('\n');
}

/* The Tier-1 bootloader establishes the board-verified UART1 console at
 * 26 MHz / 56 ~= 460800 baud.  The generic SDK init rewrites the UART clock
 * and configuration after its GPIO messages; on a physical cold boot that
 * can make every later diagnostic byte disappear even if bk_uart_init()
 * returns.  Reassert the board console invariant after the SDK has created
 * its software state, before emitting the return checkpoint.
 */

static void bk7258_uart_restore_console(void)
{
  uint32_t regval;

  regval = getreg32(BK7258_SYS_CLK_ENABLE_REG);
  regval |= BK7258_SYS_UART1_CLK_ENABLE;
  putreg32(regval, BK7258_SYS_CLK_ENABLE_REG);

  regval = getreg32(BK7258_SYS_CLK_SELECT_REG);
  regval &= ~BK7258_SYS_UART1_CLK_SELECT;
  putreg32(regval, BK7258_SYS_CLK_SELECT_REG);

  putreg32(BK7258_UART1_GLOBAL_ENABLE, BK7258_UART1_GLOBAL_CTRL);
  putreg32(BK7258_UART1_CONFIG_460800, BK7258_UART1_CONFIG);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void bk7258_uart_sdk_isr(uart_id_t id, void *param);
static int  bk7258_uart_setup(struct uart_dev_s *dev);
static void bk7258_uart_shutdown(struct uart_dev_s *dev);
static int  bk7258_uart_attach(struct uart_dev_s *dev);
static void bk7258_uart_detach(struct uart_dev_s *dev);
static int  bk7258_uart_ioctl(struct file *filep, int cmd,
                              unsigned long arg);
static int  bk7258_uart_receive(struct uart_dev_s *dev,
                                unsigned int *status);
static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_rxavailable(struct uart_dev_s *dev);
static void bk7258_uart_send(struct uart_dev_s *dev, int ch);
static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_txready(struct uart_dev_s *dev);
static bool bk7258_uart_txempty(struct uart_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* RX is interrupt-driven through the SDK IRQ bridge.  TX uses the standard
 * NuttX synchronous-drain pattern over the SDK's blocking polled write API.
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

/****************************************************************************
 * Name: bk7258_uart_sdk_isr
 ****************************************************************************/

static void bk7258_uart_sdk_isr(uart_id_t id, void *param)
{
  struct uart_dev_s *dev = (struct uart_dev_s *)param;

  (void)id;
  uart_recvchars(dev);
}

/****************************************************************************
 * Name: bk7258_uart_setup
 ****************************************************************************/

static int bk7258_uart_setup(struct uart_dev_s *dev)
{
  static bool initialized;
  struct bk7258_uart_s *priv = dev->priv;
  bk_err_t result;
  const uart_config_t config =
    {
      .baud_rate = BK7258_UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_NONE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_FLOWCTRL_DISABLE,
      .src_clk = UART_SCLK_XTAL_26M,
      .rx_dma_en = UART_DMA_DISABLE,
      .tx_dma_en = UART_DMA_DISABLE,
    };

  cold_ckpt("U0"); /* UART setup entry */

  if (initialized)
    {
      cold_ckpt("U4"); /* Already initialized */
      return OK;
    }

  /* The SDK UART pinmux path uses GPIO HAL state initialized only by
   * bk_gpio_driver_init().  Initializing UART first can dereference the
   * uninitialized GPIO peripheral-mode table on a true cold boot, while a
   * retained warm state can hide the ordering defect.
   */

  if (bk_gpio_driver_init() != BK_OK)
    {
      cold_ckpt("EG"); /* GPIO driver init failed */
      return -EIO;
    }

  cold_ckpt("G1"); /* GPIO HAL/pinmux state initialized */

  if (bk_uart_driver_init() != BK_OK)
    {
      cold_ckpt("E1"); /* Driver init failed */
      return -EIO;
    }

  cold_ckpt("U1"); /* Driver init returned */

  result = bk_uart_init(priv->id, &config);
  bk7258_uart_restore_console();
  if (result != BK_OK)
    {
      cold_ckpt("E2"); /* UART init failed */
      return -EIO;
    }

  cold_ckpt("U2"); /* UART init returned and console restored */

  /* NuttX owns the receive ring.  Keep bytes in the hardware FIFO so the SDK
   * callback can hand them directly to uart_recvchars().
   */

  if (bk_uart_disable_sw_fifo(priv->id) != BK_OK)
    {
      cold_ckpt("E3"); /* FIFO disable failed */
      return -EIO;
    }

  cold_ckpt("U3"); /* FIFO disabled */

  if (bk_uart_set_rx_full_threshold(priv->id, 1) != BK_OK)
    {
      cold_ckpt("E4"); /* RX threshold failed */
      return -EIO;
    }

  priv->rxbyte = -1;
  initialized = true;
  cold_ckpt("U4"); /* UART setup done */
  return OK;
}

/****************************************************************************
 * Name: bk7258_uart_shutdown
 ****************************************************************************/

static void bk7258_uart_shutdown(struct uart_dev_s *dev)
{
  /* The console keeps the SDK UART initialized across close/reopen cycles. */

  (void)dev;
}

/****************************************************************************
 * Name: bk7258_uart_attach / detach
 ****************************************************************************/

static int bk7258_uart_attach(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;
  bk_err_t ret;

  ret = bk_uart_register_rx_isr(priv->id, bk7258_uart_sdk_isr, dev);
  return ret == BK_OK ? OK : -EIO;
}

static void bk7258_uart_detach(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  (void)bk_uart_disable_rx_interrupt(priv->id);
  (void)bk_uart_register_rx_isr(priv->id, NULL, NULL);
}

/****************************************************************************
 * Name: bk7258_uart_ioctl
 ****************************************************************************/

static int bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  (void)filep;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

/****************************************************************************
 * Name: bk7258_uart_rxavailable / receive
 ****************************************************************************/

static bool bk7258_uart_rxavailable(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (priv->rxbyte < 0)
    {
      uint8_t byte;
      int nread;

      nread = bk_uart_read_bytes(priv->id, &byte, 1, 0);
      priv->rxbyte = nread == 1 ? byte : -1;
    }

  return priv->rxbyte >= 0;
}

static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct bk7258_uart_s *priv = dev->priv;
  int ch = priv->rxbyte;

  priv->rxbyte = -1;
  if (status)
    {
      *status = 0;
    }

  return ch;
}

/****************************************************************************
 * Name: bk7258_uart_rxint
 ****************************************************************************/

static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (enable)
    {
      (void)bk_uart_enable_rx_interrupt(priv->id);
    }
  else
    {
      (void)bk_uart_disable_rx_interrupt(priv->id);
    }
}

/****************************************************************************
 * Name: bk7258_uart_send
 ****************************************************************************/

static void bk7258_uart_send(struct uart_dev_s *dev, int ch)
{
  struct bk7258_uart_s *priv = dev->priv;
  uint8_t byte = (uint8_t)ch;

  (void)bk_uart_write_bytes(priv->id, &byte, 1);
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
  /* bk_uart_write_bytes() waits for hardware FIFO space. */

  (void)dev;
  return true;
}

static bool bk7258_uart_txempty(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  return bk_uart_is_tx_over(priv->id);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_earlyserialinit
 ****************************************************************************/

#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void)
{
  /* Early output uses bk7258_lowputc.c.  SDK setup is deferred until
   * arm_serialinit(), when allocation and semaphore services are available.
   */

  CONSOLE_DEV.isconsole = true;
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
  cold_ckpt("S0"); /* arm_serialinit entry */
  CONSOLE_DEV.isconsole = true;

  if (bk7258_uart_setup(&CONSOLE_DEV) < 0)
    {
      cold_ckpt("SE"); /* UART setup rejected */
      return;
    }

  (void)uart_register("/dev/console", &CONSOLE_DEV);
  cold_ckpt("U5"); /* /dev/console registered */
}
#endif
