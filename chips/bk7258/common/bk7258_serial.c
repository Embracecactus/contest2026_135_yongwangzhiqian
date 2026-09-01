/****************************************************************************
 * chips/bk7258/common/
 * bk7258_serial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Configurable SDK-backed UART0/UART1/UART2 NuttX serial lower halves.
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <termios.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>
#include <nuttx/serial/serial.h>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/uart_types.h>
#include <soc/reg_base.h>

#include "arm_internal.h"
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_console.h>
#include <arch/chip/bk7258_debug.h>

#if defined(CONFIG_BK7258_UART0) || defined(CONFIG_BK7258_UART1) || \
    defined(CONFIG_BK7258_UART2)
#  define BK7258_HAVE_UART_DEVICE 1
#endif

#ifdef BK7258_HAVE_UART_DEVICE

#define BK7258_UART0_FLOW_THRESHOLD 127u
#define BK7258_UART_CONFIG_OFFSET       0x10u
#define BK7258_UART_FIFO_STATUS_OFFSET  0x18u
#define BK7258_UART_INT_ENABLE_OFFSET   0x20u
#define BK7258_UART_INT_STATUS_OFFSET   0x24u
#define BK7258_UART_FLOW_CONTROL_OFFSET 0x28u

/* v3.1.1.9 exports these GPIO mapper entry points but omits their public
 * prototypes from the packaged wrapper header.
 */

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev);

struct bk7258_uart_s
{
  uart_id_t id;
  uint32_t baud;
  uint8_t data_bits;
  uint8_t parity;
  uint8_t stop_bits;
  bool flow_control;
  bool initialized;
  bool rx_enabled;
  int rxbyte;
};

static const struct uart_ops_s g_bk7258_uart_ops;
static bool g_bk7258_uart_driver_initialized;

int bk7258_uart_debug_snapshot(unsigned int uart,
                              struct bk7258_uart_debug_snapshot_s *snapshot)
{
  uintptr_t base;

  if (snapshot == NULL)
    {
      return -EINVAL;
    }

  switch (uart)
    {
      case 0:
        base = SOC_UART0_REG_BASE;
        break;
      case 1:
        base = SOC_UART1_REG_BASE;
        break;
      case 2:
        base = SOC_UART2_REG_BASE;
        break;
      default:
        return -EINVAL;
    }

  snapshot->config = getreg32(base + BK7258_UART_CONFIG_OFFSET);
  snapshot->fifo_status = getreg32(base + BK7258_UART_FIFO_STATUS_OFFSET);
  snapshot->int_enable = getreg32(base + BK7258_UART_INT_ENABLE_OFFSET);
  snapshot->int_status = getreg32(base + BK7258_UART_INT_STATUS_OFFSET);
  snapshot->flow_control = getreg32(base + BK7258_UART_FLOW_CONTROL_OFFSET);
  return OK;
}

#define BK7258_DECLARE_UART(n, uartid, initial_baud, initial_bits, \
                            initial_parity, initial_stop, initial_flow) \
  static struct bk7258_uart_s g_bk7258_uart##n##priv = \
  { \
    .id = uartid, \
    .baud = initial_baud, \
    .data_bits = initial_bits, \
    .parity = initial_parity, \
    .stop_bits = initial_stop, \
    .flow_control = initial_flow, \
    .rxbyte = -1, \
  }; \
  static char g_bk7258_uart##n##rx[CONFIG_BK7258_UART_RXBUFSIZE]; \
  static char g_bk7258_uart##n##tx[CONFIG_BK7258_UART_TXBUFSIZE]; \
  static struct uart_dev_s g_bk7258_uart##n##dev = \
  { \
    .ops = &g_bk7258_uart_ops, \
    .priv = &g_bk7258_uart##n##priv, \
    .recv = \
      { \
        .size = CONFIG_BK7258_UART_RXBUFSIZE, \
        .buffer = g_bk7258_uart##n##rx, \
      }, \
    .xmit = \
      { \
        .size = CONFIG_BK7258_UART_TXBUFSIZE, \
        .buffer = g_bk7258_uart##n##tx, \
      }, \
  }

#ifdef CONFIG_BK7258_UART0
#  ifdef CONFIG_BK7258_UART0_FLOW_CONTROL
#    define BK7258_UART0_INITIAL_FLOW true
#  else
#    define BK7258_UART0_INITIAL_FLOW false
#  endif
BK7258_DECLARE_UART(0, UART_ID_0, CONFIG_BK7258_UART0_BAUD,
                    CONFIG_BK7258_UART0_DATA_BITS,
                    CONFIG_BK7258_UART0_PARITY,
                    CONFIG_BK7258_UART0_STOP_BITS,
                    BK7258_UART0_INITIAL_FLOW);
#endif

#ifdef CONFIG_BK7258_UART1
BK7258_DECLARE_UART(1, UART_ID_1, CONFIG_BK7258_UART1_BAUD,
                    CONFIG_BK7258_UART1_DATA_BITS,
                    CONFIG_BK7258_UART1_PARITY,
                    CONFIG_BK7258_UART1_STOP_BITS, false);
#endif

#ifdef CONFIG_BK7258_UART2
BK7258_DECLARE_UART(2, UART_ID_2, CONFIG_BK7258_UART2_BAUD,
                    CONFIG_BK7258_UART2_DATA_BITS,
                    CONFIG_BK7258_UART2_PARITY,
                    CONFIG_BK7258_UART2_STOP_BITS, false);
#endif

#if defined(CONFIG_BK7258_CONSOLE_UART0)
#  define CONSOLE_DEV g_bk7258_uart0dev
#elif defined(CONFIG_BK7258_CONSOLE_UART1)
#  define CONSOLE_DEV g_bk7258_uart1dev
#elif defined(CONFIG_BK7258_CONSOLE_UART2)
#  define CONSOLE_DEV g_bk7258_uart2dev
#endif

#ifdef BK7258_HAVE_UART_CONSOLE
static bool bk7258_uart_is_console(struct uart_dev_s *dev)
{
  return dev == &CONSOLE_DEV;
}
#endif

static uart_data_bits_t bk7258_uart_data_bits(uint8_t bits)
{
  return (uart_data_bits_t)(bits - 5u);
}

static uart_parity_t bk7258_uart_parity(uint8_t parity)
{
  return (uart_parity_t)parity;
}

static uart_stop_bits_t bk7258_uart_stop_bits(uint8_t stop_bits)
{
  return stop_bits == 2u ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
}

static int bk7258_uart_driver_initialize(void)
{
  if (g_bk7258_uart_driver_initialized)
    {
      return OK;
    }

  if (bk_gpio_driver_init() != BK_OK || bk_uart_driver_init() != BK_OK)
    {
      return -EIO;
    }

  g_bk7258_uart_driver_initialized = true;
  return OK;
}

static int bk7258_uart_apply_pin_route(struct bk7258_uart_s *priv)
{
#ifdef CONFIG_BK7258_UART0_FLOW_CONTROL
  if (priv->id == UART_ID_0 &&
      (gpio_dev_unmap(GPIO_12) != BK_OK ||
       gpio_dev_unmap(GPIO_13) != BK_OK ||
       gpio_dev_map(GPIO_12, GPIO_DEV_UART0_RTS) != BK_OK ||
       gpio_dev_map(GPIO_13, GPIO_DEV_UART0_CTS) != BK_OK ||
       bk_gpio_pull_down(GPIO_12) != BK_OK ||
       bk_gpio_pull_down(GPIO_13) != BK_OK))
    {
      return -EIO;
    }
#endif

#if defined(CONFIG_BK7258_UART2) && \
    defined(CONFIG_BK7258_UART2_PINS_P40_P41)
  if (priv->id == UART_ID_2)
    {
      /* bk_uart_init() establishes the official P31/P30 default first.
       * Move ownership atomically to the alternate P41/P40 chip mux and do
       * not leave duplicate UART2 outputs active.
       */

      if (gpio_dev_unmap(GPIO_31) != BK_OK ||
          gpio_dev_unmap(GPIO_30) != BK_OK ||
          gpio_dev_unmap(GPIO_41) != BK_OK ||
          gpio_dev_unmap(GPIO_40) != BK_OK ||
          gpio_dev_map(GPIO_41, GPIO_DEV_UART2_TXD) != BK_OK ||
          gpio_dev_map(GPIO_40, GPIO_DEV_UART2_RXD) != BK_OK)
        {
          return -EIO;
        }
    }
#else
  (void)priv;
#endif

  return OK;
}

static int bk7258_uart_apply_format(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;
  bk_err_t flow_result = BK_OK;

  if (priv->id == UART_ID_0)
    {
      flow_result = priv->flow_control ?
        bk_uart_set_hw_flow_ctrl(priv->id,
                                BK7258_UART0_FLOW_THRESHOLD) :
        bk_uart_disable_hw_flow_ctrl(priv->id);
    }

  if (bk_uart_set_baud_rate(priv->id, priv->baud) != BK_OK ||
      bk_uart_set_data_bits(priv->id,
                            bk7258_uart_data_bits(priv->data_bits)) != BK_OK ||
      bk_uart_set_parity(priv->id,
                         bk7258_uart_parity(priv->parity)) != BK_OK ||
      bk_uart_set_stop_bits(priv->id,
                            bk7258_uart_stop_bits(priv->stop_bits)) != BK_OK ||
      flow_result != BK_OK)
    {
      return -EIO;
    }

#ifdef BK7258_HAVE_UART_CONSOLE
  if (bk7258_uart_is_console(dev))
    {
      if (bk7258_lowputc_set_format(priv->baud, priv->data_bits,
                                    priv->parity, priv->stop_bits) < 0)
        {
          return -EINVAL;
        }

      bk7258_lowputc_restore_console();
    }
#endif

  return OK;
}

static void bk7258_uart_rollback_setup(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  /* The SDK deinitializer releases the UART clock/FIFO state but does not
   * unmap its GPIOs.  Tuya performs that GPIO cleanup explicitly.  Remove
   * every route this wrapper may have established so a failed setup cannot
   * retain pin ownership (especially the alternate UART2 route). */

  (void)bk_uart_deinit(priv->id);

  if (priv->id == UART_ID_0)
    {
      (void)gpio_dev_unmap(GPIO_11);
      (void)gpio_dev_unmap(GPIO_10);
#ifdef CONFIG_BK7258_UART0_FLOW_CONTROL
      (void)gpio_dev_unmap(GPIO_12);
      (void)gpio_dev_unmap(GPIO_13);
#endif
    }
  else if (priv->id == UART_ID_1)
    {
      (void)gpio_dev_unmap(GPIO_0);
      (void)gpio_dev_unmap(GPIO_1);
    }
  else if (priv->id == UART_ID_2)
    {
      (void)gpio_dev_unmap(GPIO_31);
      (void)gpio_dev_unmap(GPIO_30);
#ifdef CONFIG_BK7258_UART2_PINS_P40_P41
      (void)gpio_dev_unmap(GPIO_41);
      (void)gpio_dev_unmap(GPIO_40);
#endif
    }

#ifdef BK7258_HAVE_UART_CONSOLE
  if (bk7258_uart_is_console(dev))
    {
      /* Keep the polled console usable after the SDK ownership handoff is
       * rolled back. */

      bk7258_lowputc_restore_console();
    }
#endif
}

static void bk7258_uart_sdk_isr(uart_id_t id, void *param)
{
  struct uart_dev_s *dev = param;

  (void)id;
  uart_recvchars(dev);
}

static int bk7258_uart_setup(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;
  uart_config_t config;
  bk_err_t result;
  int ret;

  if (priv->initialized)
    {
      return OK;
    }

  ret = bk7258_uart_driver_initialize();
  if (ret < 0)
    {
      return ret;
    }

  config.baud_rate = priv->baud;
  config.data_bits = bk7258_uart_data_bits(priv->data_bits);
  config.parity = bk7258_uart_parity(priv->parity);
  config.stop_bits = bk7258_uart_stop_bits(priv->stop_bits);
  config.flow_ctrl = priv->flow_control ? UART_FLOWCTRL_CTS_RTS :
                                          UART_FLOWCTRL_DISABLE;
  config.src_clk = UART_SCLK_XTAL_26M;
  config.rx_dma_en = UART_DMA_DISABLE;
  config.tx_dma_en = UART_DMA_DISABLE;

#ifdef BK7258_HAVE_UART_CONSOLE
  if (bk7258_uart_is_console(dev))
    {
      bk7258_lowputc_handoff(true);
    }
#endif

  result = bk_uart_init(priv->id, &config);

#ifdef BK7258_HAVE_UART_CONSOLE
  if (bk7258_uart_is_console(dev))
    {
      bk7258_lowputc_handoff(false);
    }
#endif

  if (result != BK_OK)
    {
      return -EIO;
    }

  ret = bk7258_uart_apply_pin_route(priv);
  if (ret < 0)
    {
      goto fail;
    }

  ret = bk7258_uart_apply_format(dev);
  if (ret < 0)
    {
      goto fail;
    }

  if (bk_uart_disable_sw_fifo(priv->id) != BK_OK ||
      bk_uart_set_rx_full_threshold(priv->id, 1) != BK_OK)
    {
      ret = -EIO;
      goto fail;
    }

  priv->rxbyte = -1;
  priv->initialized = true;
  return OK;

fail:
  bk7258_uart_rollback_setup(dev);
  return ret;
}

int bk7258_uart_runtime_reinitialize(unsigned int uart)
{
  struct uart_dev_s *dev;
  struct bk7258_uart_s *priv;
  int ret;

  /* arm_serialinit() has to register the NuttX device before the AP RPMsg
   * clock service exists.  The v3.1.1.9 SDK ignores the failed early clock
   * vote and nevertheless marks the UART initialized, so a later
   * bk_uart_init() becomes a no-op.  Re-enter the public SDK lifecycle once
   * from normal task context, after RPTUN is connected and before the first
   * open.  Do not perform this from uart_ops_s::setup(): NuttX calls setup
   * with the UART spinlock held and interrupts disabled, while the AP clock
   * request can wait for an RPMsg reply.
   */

  switch (uart)
    {
#ifdef CONFIG_BK7258_UART0
      case 0:
        dev = &g_bk7258_uart0dev;
        break;
#endif
#ifdef CONFIG_BK7258_UART1
      case 1:
        dev = &g_bk7258_uart1dev;
        break;
#endif
#ifdef CONFIG_BK7258_UART2
      case 2:
        dev = &g_bk7258_uart2dev;
        break;
#endif
      default:
        return -ENODEV;
    }

  priv = dev->priv;
  if (!priv->initialized)
    {
      return -EAGAIN;
    }

  ret = nxmutex_lock(&dev->closelock);
  if (ret < 0)
    {
      return ret;
    }

  if (dev->open_count != 0)
    {
      ret = -EBUSY;
      goto out;
    }

  if (!priv->initialized || bk_uart_deinit(priv->id) != BK_OK)
    {
      ret = -EIO;
      goto out;
    }

  priv->initialized = false;
  priv->rx_enabled = false;
  priv->rxbyte = -1;
  ret = bk7258_uart_setup(dev);

out:
  nxmutex_unlock(&dev->closelock);
  return ret;
}

static void bk7258_uart_shutdown(struct uart_dev_s *dev)
{
  /* Device ownership is static for the firmware lifetime. */

  (void)dev;
}

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
  priv->rx_enabled = false;
}

#ifdef CONFIG_SERIAL_TERMIOS
static int bk7258_uart_termios_get(struct bk7258_uart_s *priv,
                                   struct termios *termiosp)
{
  if (termiosp == NULL)
    {
      return -EINVAL;
    }

  termiosp->c_cflag = 0;
  switch (priv->data_bits)
    {
      case 5:
        termiosp->c_cflag |= CS5;
        break;
      case 6:
        termiosp->c_cflag |= CS6;
        break;
      case 7:
        termiosp->c_cflag |= CS7;
        break;
      default:
        termiosp->c_cflag |= CS8;
        break;
    }

  if (priv->parity != 0u)
    {
      termiosp->c_cflag |= PARENB;
      if (priv->parity == 1u)
        {
          termiosp->c_cflag |= PARODD;
        }
    }

  if (priv->stop_bits == 2u)
    {
      termiosp->c_cflag |= CSTOPB;
    }

#if defined(CONFIG_SERIAL_IFLOWCONTROL) && \
    defined(CONFIG_SERIAL_OFLOWCONTROL)
  if (priv->flow_control)
    {
      termiosp->c_cflag |= CRTS_IFLOW | CCTS_OFLOW;
    }
#endif

  cfsetispeed(termiosp, priv->baud);
  cfsetospeed(termiosp, priv->baud);
  return OK;
}

static int bk7258_uart_termios_set(struct uart_dev_s *dev,
                                   const struct termios *termiosp)
{
  struct bk7258_uart_s *priv = dev->priv;
  uint32_t baud;
  uint8_t data_bits;
  uint8_t parity;
  uint8_t stop_bits;
  bool flow_control = false;
  bool rx_enabled;
  unsigned int count;

  if (termiosp == NULL)
    {
      return -EINVAL;
    }

  baud = cfgetispeed(termiosp);
  if (baud < 400u || baud > 5200000u)
    {
      return -EINVAL;
    }

  switch (termiosp->c_cflag & CSIZE)
    {
      case CS5:
        data_bits = 5u;
        break;
      case CS6:
        data_bits = 6u;
        break;
      case CS7:
        data_bits = 7u;
        break;
      case CS8:
        data_bits = 8u;
        break;
      default:
        return -EINVAL;
    }

  parity = (termiosp->c_cflag & PARENB) == 0 ? 0u :
           ((termiosp->c_cflag & PARODD) != 0 ? 1u : 2u);
  stop_bits = (termiosp->c_cflag & CSTOPB) != 0 ? 2u : 1u;

#if defined(CONFIG_SERIAL_IFLOWCONTROL) && \
    defined(CONFIG_SERIAL_OFLOWCONTROL)
  if ((termiosp->c_cflag & (CRTS_IFLOW | CCTS_OFLOW)) != 0)
    {
      if ((termiosp->c_cflag & (CRTS_IFLOW | CCTS_OFLOW)) !=
          (CRTS_IFLOW | CCTS_OFLOW) || priv->id != UART_ID_0)
        {
          return -EINVAL;
        }

      flow_control = true;
    }
#else
  flow_control = priv->flow_control;
#endif

  /* v3.1.1.9 has no runtime flow-control setter.  Refuse a partial hardware
   * transition; baud/data/parity/stop changes remain fully supported.
   */

  if (flow_control != priv->flow_control)
    {
      return -ENOTSUP;
    }

  for (count = 0; count < BK7258_UART_TX_POLL_LIMIT; count++)
    {
      if (bk_uart_is_tx_over(priv->id))
        {
          break;
        }
    }

  if (count == BK7258_UART_TX_POLL_LIMIT)
    {
      return -EBUSY;
    }

  rx_enabled = priv->rx_enabled;
  (void)bk_uart_disable_rx_interrupt(priv->id);
  priv->baud = baud;
  priv->data_bits = data_bits;
  priv->parity = parity;
  priv->stop_bits = stop_bits;

  if (bk7258_uart_apply_format(dev) < 0)
    {
      return -EIO;
    }

  if (rx_enabled)
    {
      (void)bk_uart_enable_rx_interrupt(priv->id);
    }

  return OK;
}
#endif

static int bk7258_uart_ioctl(struct file *filep, int cmd,
                             unsigned long arg)
{
  struct inode *inode = filep->f_inode;
  struct uart_dev_s *dev = inode->i_private;
  struct bk7258_uart_s *priv = dev->priv;

#ifdef CONFIG_SERIAL_TERMIOS
  switch (cmd)
    {
      case TCGETS:
        return bk7258_uart_termios_get(priv, (struct termios *)arg);
      case TCSETS:
        return bk7258_uart_termios_set(dev,
                                       (const struct termios *)arg);
      default:
        break;
    }
#else
  (void)priv;
  (void)cmd;
  (void)arg;
#endif

  return -ENOTTY;
}

static bool bk7258_uart_rxavailable(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (priv->rxbyte < 0)
    {
      uint8_t byte;
      int nread = bk_uart_read_bytes(priv->id, &byte, 1, 0);

      priv->rxbyte = nread == 1 ? byte : -1;
    }

  return priv->rxbyte >= 0;
}

static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct bk7258_uart_s *priv = dev->priv;
  int ch = priv->rxbyte;

  priv->rxbyte = -1;
  if (status != NULL)
    {
      *status = 0;
    }

  return ch;
}

static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (enable)
    {
      priv->rx_enabled = bk_uart_enable_rx_interrupt(priv->id) == BK_OK;
    }
  else
    {
      (void)bk_uart_disable_rx_interrupt(priv->id);
      priv->rx_enabled = false;
    }
}

static void bk7258_uart_send(struct uart_dev_s *dev, int ch)
{
  struct bk7258_uart_s *priv = dev->priv;
  uint8_t byte = (uint8_t)ch;

#ifdef BK7258_HAVE_UART_CONSOLE
  if (bk7258_uart_is_console(dev))
    {
      bk7258_lowputc_ensure_console();
    }
#endif

  (void)bk_uart_write_bytes(priv->id, &byte, 1);
}

static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable)
{
  if (enable)
    {
      uart_xmitchars(dev);
    }
}

static bool bk7258_uart_txready(struct uart_dev_s *dev)
{
  (void)dev;
  return true;
}

static bool bk7258_uart_txempty(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  return bk_uart_is_tx_over(priv->id);
}

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

#ifdef BK7258_HAVE_UART_CONSOLE
void bk7258_uart_recover_console(void)
{
  struct bk7258_uart_s *priv = CONSOLE_DEV.priv;

  if (!priv->initialized)
    {
      return;
    }

  (void)bk_uart_disable_rx_interrupt(priv->id);
  (void)bk7258_uart_apply_pin_route(priv);
  (void)bk7258_uart_apply_format(&CONSOLE_DEV);
  (void)bk_uart_set_enable_tx(priv->id, true);
  (void)bk_uart_set_enable_rx(priv->id, true);
  (void)bk_uart_disable_sw_fifo(priv->id);
  (void)bk_uart_set_rx_full_threshold(priv->id, 1);
  (void)bk_uart_register_rx_isr(priv->id, bk7258_uart_sdk_isr,
                                &CONSOLE_DEV);
  if (priv->rx_enabled)
    {
      (void)bk_uart_enable_rx_interrupt(priv->id);
    }

  priv->rxbyte = -1;
}
#endif

static int bk7258_uart_pm_prepare_one(struct bk7258_uart_s *priv)
{
  unsigned int count;

  if (!priv->initialized)
    {
      return OK;
    }

  for (count = 0; count < BK7258_UART_TX_POLL_LIMIT; count++)
    {
      if (bk_uart_is_tx_over(priv->id))
        {
          return OK;
        }
    }

  return -EBUSY;
}

int bk7258_uart_pm_prepare(void)
{
  int ret;

#ifdef CONFIG_BK7258_UART0
  ret = bk7258_uart_pm_prepare_one(&g_bk7258_uart0priv);
  if (ret < 0)
    {
      return ret;
    }
#endif
#ifdef CONFIG_BK7258_UART1
  ret = bk7258_uart_pm_prepare_one(&g_bk7258_uart1priv);
  if (ret < 0)
    {
      return ret;
    }
#endif
#ifdef CONFIG_BK7258_UART2
  ret = bk7258_uart_pm_prepare_one(&g_bk7258_uart2priv);
  if (ret < 0)
    {
      return ret;
    }
#endif

  return OK;
}

static void bk7258_uart_pm_restore_one(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (!priv->initialized)
    {
      return;
    }

  (void)bk_uart_disable_rx_interrupt(priv->id);
  (void)bk7258_uart_apply_pin_route(priv);
  (void)bk7258_uart_apply_format(dev);
  (void)bk_uart_set_enable_tx(priv->id, true);
  (void)bk_uart_set_enable_rx(priv->id, true);
  (void)bk_uart_disable_sw_fifo(priv->id);
  (void)bk_uart_set_rx_full_threshold(priv->id, 1);
  (void)bk_uart_register_rx_isr(priv->id, bk7258_uart_sdk_isr, dev);
  if (priv->rx_enabled)
    {
      (void)bk_uart_enable_rx_interrupt(priv->id);
    }
}

void bk7258_uart_pm_restore(void)
{
#ifdef CONFIG_BK7258_UART0
  bk7258_uart_pm_restore_one(&g_bk7258_uart0dev);
#endif
#ifdef CONFIG_BK7258_UART1
  bk7258_uart_pm_restore_one(&g_bk7258_uart1dev);
#endif
#ifdef CONFIG_BK7258_UART2
  bk7258_uart_pm_restore_one(&g_bk7258_uart2dev);
#endif
}

#endif /* BK7258_HAVE_UART_DEVICE */

#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void)
{
#ifdef BK7258_HAVE_UART_CONSOLE
  CONSOLE_DEV.isconsole = true;
#endif
}
#endif

#ifdef USE_SERIALDRIVER
#ifdef BK7258_HAVE_UART_DEVICE
static void bk7258_uart_register(struct uart_dev_s *dev, const char *path)
{
  if (bk7258_uart_setup(dev) >= 0)
    {
      (void)uart_register(path, dev);
    }
}
#endif

void arm_serialinit(void)
{
#ifdef CONFIG_BK7258_UART0
  bk7258_uart_register(&g_bk7258_uart0dev, "/dev/ttyS0");
#endif
#ifdef CONFIG_BK7258_UART1
  bk7258_uart_register(&g_bk7258_uart1dev, "/dev/ttyS1");
#endif
#ifdef CONFIG_BK7258_UART2
  bk7258_uart_register(&g_bk7258_uart2dev, "/dev/ttyS2");
#endif

#ifdef BK7258_HAVE_UART_CONSOLE
  CONSOLE_DEV.isconsole = true;
  if (bk7258_uart_setup(&CONSOLE_DEV) >= 0)
    {
      (void)uart_register("/dev/console", &CONSOLE_DEV);
    }
#endif
}
#endif
