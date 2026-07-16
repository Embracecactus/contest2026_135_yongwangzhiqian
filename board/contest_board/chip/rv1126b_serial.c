/****************************************************************************
 * NuttX - RV1126B Interrupt-Driven Serial Driver
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version
 * 2.0 (the "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/serial/serial.h>
#include <nuttx/fs/fs.h>

#include "riscv_internal.h"
#include "chip.h"
#include "hardware/rv1126b_memorymap.h"
#include "rv1126b_config.h"
#include "hardware/rv1126b_uart.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* UART5 configuration (SDK-verified console UART) */

#define CONSOLE_UART_BASE       RV1126B_UART5_BASE
#define CONSOLE_UART_IRQ        RV1126B_IRQ_UART5

/* UART register access macros */

#define UART_RBR(base)          ((base) + RV1126B_UART_RBR_OFFSET)
#define UART_THR(base)          ((base) + RV1126B_UART_THR_OFFSET)
#define UART_DLL(base)          ((base) + RV1126B_UART_DLL_OFFSET)
#define UART_DLH(base)          ((base) + RV1126B_UART_DLH_OFFSET)
#define UART_IER(base)          ((base) + RV1126B_UART_IER_OFFSET)
#define UART_IIR(base)          ((base) + RV1126B_UART_IIR_OFFSET)
#define UART_FCR(base)          ((base) + RV1126B_UART_FCR_OFFSET)
#define UART_LCR(base)          ((base) + RV1126B_UART_LCR_OFFSET)
#define UART_LSR(base)          ((base) + RV1126B_UART_LSR_OFFSET)
#define UART_USR(base)          ((base) + RV1126B_UART_USR_OFFSET)
#define UART_SRR(base)          ((base) + RV1126B_UART_SRR_OFFSET)

/* Default baud rate -- derived from Kconfig via rv1126b_config.h */

#define CONSOLE_DEFAULT_BAUD    CONSOLE_BAUD_RATE

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rv1126b_uart_s
{
  uint32_t base;        /* UART register base address */
  uint32_t irq;         /* UART IRQ number */
  uint32_t baud;        /* Current baud rate */
  uint32_t clk_rate;    /* Input clock rate */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* UART operations */

static int  rv1126b_uart_setup(struct uart_dev_s *dev);
static void rv1126b_uart_shutdown(struct uart_dev_s *dev);
static int  rv1126b_uart_attach(struct uart_dev_s *dev);
static void rv1126b_uart_detach(struct uart_dev_s *dev);
static int  rv1126b_uart_ioctl(struct file *filep, int cmd,
                               unsigned long arg);
static int  rv1126b_uart_receive(struct uart_dev_s *dev, unsigned int *status);
static void rv1126b_uart_send(struct uart_dev_s *dev, int ch);
static void rv1126b_uart_rxint(struct uart_dev_s *dev, bool enable);
static void rv1126b_uart_txint(struct uart_dev_s *dev, bool enable);
static bool rv1126b_uart_txready(struct uart_dev_s *dev);
static bool rv1126b_uart_txempty(struct uart_dev_s *dev);
static bool rv1126b_uart_rxavailable(struct uart_dev_s *dev);

/* Interrupt handler */

static int  rv1126b_uart_interrupt(int irq, void *context, void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_uart_ops =
{
  .setup      = rv1126b_uart_setup,
  .shutdown   = rv1126b_uart_shutdown,
  .attach     = rv1126b_uart_attach,
  .detach     = rv1126b_uart_detach,
  .ioctl      = rv1126b_uart_ioctl,
  .receive    = rv1126b_uart_receive,
  .send       = rv1126b_uart_send,
  .rxint      = rv1126b_uart_rxint,
  .txint      = rv1126b_uart_txint,
  .txready    = rv1126b_uart_txready,
  .txempty    = rv1126b_uart_txempty,
  .rxavailable = rv1126b_uart_rxavailable,
};

/* UART5 console device structure */

static struct rv1126b_uart_s g_console_priv =
{
  .base     = CONSOLE_UART_BASE,
  .irq      = CONSOLE_UART_IRQ,
  .baud     = CONSOLE_DEFAULT_BAUD,
  .clk_rate = CONSOLE_CLK_RATE,
};

static char g_console_rxbuffer[256];
static char g_console_txbuffer[256];

static struct uart_dev_s g_console_port =
{
  .isconsole = true,
  .ops       = &g_uart_ops,
  .priv      = &g_console_priv,
  .recv      =
  {
    .size    = sizeof(g_console_rxbuffer),
    .buffer  = g_console_rxbuffer,
  },
  .xmit      =
  {
    .size    = sizeof(g_console_txbuffer),
    .buffer  = g_console_txbuffer,
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_uart_setup
 *
 * Description:
 *   Configure the UART for the current baud rate and format.
 *   Enable the UART clock and configure pins if not already done.
 *
 ****************************************************************************/

static int rv1126b_uart_setup(struct uart_dev_s *dev)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;
  uint32_t divisor;
  irqstate_t flags;

  flags = enter_critical_section();

  /* Software reset the UART */

  putreg32(RV1126B_UART_SRR_UR | RV1126B_UART_SRR_RFR |
           RV1126B_UART_SRR_XFR, UART_SRR(base));

  /* Disable all interrupts */

  putreg32(0, UART_IER(base));

  /* Enable FIFO, reset TX and RX FIFOs */

  putreg32(RV1126B_UART_FCR_FIFOE | RV1126B_UART_FCR_RFIFOR |
           RV1126B_UART_FCR_XFIFOR, UART_FCR(base));
  putreg32(RV1126B_UART_FCR_FIFOE | RV1126B_UART_FCR_TFT_TWO |
           RV1126B_UART_FCR_RT_HALF, UART_FCR(base));

  /* Set baud rate */

  divisor = priv->clk_rate / 16 / priv->baud;

  /* Enable DLAB to access divisor latch registers */

  putreg32(RV1126B_UART_LCR_DLAB, UART_LCR(base));
  putreg32(divisor & 0xff, UART_DLL(base));
  putreg32((divisor >> 8) & 0xff, UART_DLH(base));

  /* Set line control: 8 data bits, no parity, 1 stop bit (8N1) */

  putreg32(RV1126B_UART_LCR_DLS_8 | RV1126B_UART_LCR_STOP_1,
           UART_LCR(base));

  leave_critical_section(flags);

  return OK;
}

/****************************************************************************
 * Name: rv1126b_uart_shutdown
 *
 * Description:
 *   Disable the UART and release resources.
 *
 ****************************************************************************/

static void rv1126b_uart_shutdown(struct uart_dev_s *dev)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;
  irqstate_t flags;

  flags = enter_critical_section();

  /* Disable all interrupts */

  putreg32(0, UART_IER(base));

  /* Software reset the UART */

  putreg32(RV1126B_UART_SRR_UR | RV1126B_UART_SRR_RFR |
           RV1126B_UART_SRR_XFR, UART_SRR(base));

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: rv1126b_uart_attach
 *
 * Description:
 *   Configure and enable the UART interrupt.
 *
 ****************************************************************************/

static int rv1126b_uart_attach(struct uart_dev_s *dev)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  int ret;

  /* Attach the interrupt handler */

  ret = irq_attach(priv->irq, rv1126b_uart_interrupt, dev);
  if (ret == OK)
    {
      /* Enable the IRQ at the interrupt controller */

      up_enable_irq(priv->irq);
    }

  return ret;
}

/****************************************************************************
 * Name: rv1126b_uart_detach
 *
 * Description:
 *   Disable the UART interrupt.
 *
 ****************************************************************************/

static void rv1126b_uart_detach(struct uart_dev_s *dev)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;

  /* Disable the IRQ at the interrupt controller */

  up_disable_irq(priv->irq);

  /* Detach the interrupt handler */

  irq_detach(priv->irq);
}

/****************************************************************************
 * Name: rv1126b_uart_ioctl
 *
 * Description:
 *   No UART-specific ioctl commands are supported.
 *
 ****************************************************************************/

static int rv1126b_uart_ioctl(struct file *filep, int cmd,
                              unsigned long arg)
{
  (void)filep;
  (void)cmd;
  (void)arg;

  return -ENOTTY;
}

/****************************************************************************
 * Name: rv1126b_uart_receive
 *
 * Description:
 *   Receive one character from the UART.  This function is called from
 *   the UART interrupt handler when data is available.
 *
 ****************************************************************************/

static int rv1126b_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;
  uint32_t regval;

  /* Read the line status to get error flags */

  *status = getreg32(UART_LSR(base));

  /* Read the received character */

  regval = getreg32(UART_RBR(base));

  return (int)(regval & 0xff);
}

/****************************************************************************
 * Name: rv1126b_uart_send
 *
 * Description:
 *   Send one character via the UART.  This function is called from the
 *   UART interrupt handler when the transmit holding register is empty.
 *
 ****************************************************************************/

static void rv1126b_uart_send(struct uart_dev_s *dev, int ch)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;

  /* Write the character to the transmit holding register */

  putreg32((uint32_t)ch, UART_THR(base));
}

/****************************************************************************
 * Name: rv1126b_uart_rxint
 *
 * Description:
 *   Enable or disable the RX interrupt.
 *
 ****************************************************************************/

static void rv1126b_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;
  uint32_t regval;
  irqstate_t flags;

  flags = up_irq_save();
  regval = getreg32(UART_IER(base));

  if (enable)
    {
      regval |= RV1126B_UART_IER_ERBFI;  /* Enable Received Data Available */
    }
  else
    {
      regval &= ~RV1126B_UART_IER_ERBFI;
    }

  putreg32(regval, UART_IER(base));
  up_irq_restore(flags);
}

/****************************************************************************
 * Name: rv1126b_uart_txint
 *
 * Description:
 *   Enable or disable the TX interrupt.
 *
 ****************************************************************************/

static void rv1126b_uart_txint(struct uart_dev_s *dev, bool enable)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;
  uint32_t regval;
  irqstate_t flags;

  flags = up_irq_save();
  regval = getreg32(UART_IER(base));

  if (enable)
    {
      regval |= RV1126B_UART_IER_ETBEI;  /* Enable THR Empty */
      putreg32(regval, UART_IER(base));
      up_irq_restore(flags);

      /* Prime transmission.  uart_xmitchars() may safely disable ETBEI
       * through this callback after the software TX buffer is drained.
       */

      uart_xmitchars(dev);
    }
  else
    {
      regval &= ~RV1126B_UART_IER_ETBEI;
      putreg32(regval, UART_IER(base));
      up_irq_restore(flags);
    }
}

/****************************************************************************
 * Name: rv1126b_uart_txready
 *
 * Description:
 *   Return true if the TX FIFO is not full (ready to accept a character).
 *
 ****************************************************************************/

static bool rv1126b_uart_txready(struct uart_dev_s *dev)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;

  return (getreg32(UART_USR(base)) & RV1126B_UART_USR_TFNF) != 0;
}

/****************************************************************************
 * Name: rv1126b_uart_txempty
 *
 * Description:
 *   Return true if the TX FIFO is empty (all characters transmitted).
 *
 ****************************************************************************/

static bool rv1126b_uart_txempty(struct uart_dev_s *dev)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;

  return (getreg32(UART_USR(base)) & RV1126B_UART_USR_TFE) != 0;
}

/****************************************************************************
 * Name: rv1126b_uart_rxavailable
 *
 * Description:
 *   Return true if the RX FIFO has data available.
 *
 ****************************************************************************/

static bool rv1126b_uart_rxavailable(struct uart_dev_s *dev)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;

  return (getreg32(UART_USR(base)) & RV1126B_UART_USR_RFNE) != 0;
}

/****************************************************************************
 * Name: rv1126b_uart_interrupt
 *
 * Description:
 *   UART interrupt handler.  This is called when any UART interrupt occurs.
 *
 ****************************************************************************/

static int rv1126b_uart_interrupt(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = (struct uart_dev_s *)arg;

  (void)irq;
  (void)context;
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)dev->priv;
  uint32_t base = priv->base;
  uint32_t iir;
  uint32_t id;

  /* Read the interrupt identification register */

  iir = getreg32(UART_IIR(base));

  /* Check if there is an interrupt pending */

  if (iir & RV1126B_UART_IIR_NONE)
    {
      return OK;  /* No interrupt pending */
    }

  id = iir & RV1126B_UART_IIR_ID_MASK;

  /* Dispatch based on interrupt type */

  switch (id)
  {
    case RV1126B_UART_IIR_RDA:    /* Received Data Available */
    case RV1126B_UART_IIR_CTI:    /* Character Timeout */
    {
      uart_recvchars(dev);
      break;
    }

    case RV1126B_UART_IIR_THRE:   /* Transmit Holding Register Empty */
    {
      uart_xmitchars(dev);
      break;
    }

    case RV1126B_UART_IIR_RLS:    /* Receiver Line Status */
    {
      /* Read LSR to clear the interrupt */

      getreg32(UART_LSR(base));
      break;
    }

    case RV1126B_UART_IIR_BUSY:   /* Busy Detect */
    {
      /* Read USR to clear the interrupt */

      getreg32(UART_USR(base));
      break;
    }

    default:
      break;
  }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_earlyserialinit
 *
 * Description:
 *   Performs the low level UART initialization early in debug so that the
 *   serial console will be available during bootup.
 *
 ****************************************************************************/

void riscv_earlyserialinit(void)
{
  /* The UART5 clock and pins were already configured in rv1126b_lowsetup().
   * Here we just need to setup the console UART with default parameters.
   */

  g_console_port.isconsole = true;
  rv1126b_uart_setup(&g_console_port);
}

/****************************************************************************
 * Name: riscv_serialinit
 *
 * Description:
 *   Register the UART driver as /dev/console and /dev/ttyS0.
 *
 ****************************************************************************/

void riscv_serialinit(void)
{
  /* Register the console as /dev/console */

  uart_register("/dev/console", &g_console_port);

  /* Register the same UART as /dev/ttyS0 */

  uart_register("/dev/ttyS0", &g_console_port);
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Output one character to the console UART.
 *   This is used by the OS for debug output after the serial driver is
 *   initialized.
 *
 ****************************************************************************/

void up_putc(int ch)
{
  struct rv1126b_uart_s *priv = (struct rv1126b_uart_s *)g_console_port.priv;
  uint32_t base = priv->base;

  /* Wait until TX FIFO is not full */

  while (!(getreg32(UART_USR(base)) & RV1126B_UART_USR_TFNF))
    {
    }

  /* Write the character */

  putreg32((uint32_t)ch, UART_THR(base));
}
