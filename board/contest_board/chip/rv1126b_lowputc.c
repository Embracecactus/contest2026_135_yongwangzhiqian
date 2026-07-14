/****************************************************************************
 * NuttX - RV1126B Early Polled UART Driver
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

#include <nuttx/arch.h>

#include "riscv_internal.h"
#include "hardware/rv1126b_memorymap.h"
#include "rv1126b_config.h"
#include "hardware/rv1126b_uart.h"
#include "hardware/rv1126b_cru.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* UART5 base address (SDK-verified console UART) */

#define UART_BASE           RV1126B_UART5_BASE

#define RV1126B_HIWORD_UPDATE(mask, val) \
  ((((uint32_t)(mask)) << 16) | ((uint32_t)(val) & (uint32_t)(mask)))
#define RV1126B_UART_TX_TIMEOUT 100000

/* UART register access macros */

#define UART_RBR(base)      ((base) + RV1126B_UART_RBR_OFFSET)
#define UART_THR(base)      ((base) + RV1126B_UART_THR_OFFSET)
#define UART_DLL(base)      ((base) + RV1126B_UART_DLL_OFFSET)
#define UART_DLH(base)      ((base) + RV1126B_UART_DLH_OFFSET)
#define UART_IER(base)      ((base) + RV1126B_UART_IER_OFFSET)
#define UART_IIR(base)      ((base) + RV1126B_UART_IIR_OFFSET)
#define UART_FCR(base)      ((base) + RV1126B_UART_FCR_OFFSET)
#define UART_LCR(base)      ((base) + RV1126B_UART_LCR_OFFSET)
#define UART_MCR(base)      ((base) + RV1126B_UART_MCR_OFFSET)
#define UART_LSR(base)      ((base) + RV1126B_UART_LSR_OFFSET)
#define UART_USR(base)      ((base) + RV1126B_UART_USR_OFFSET)
#define UART_SRR(base)      ((base) + RV1126B_UART_SRR_OFFSET)

/* Clock configuration:
 * UART5 source clock = 24 MHz (from oscillator)
 * Baud rate and divisor are derived from Kconfig via rv1126b_config.h
 * (CONSOLE_CLK_RATE, CONSOLE_BAUD_RATE, CONSOLE_DIVISOR).
 * Default: 1500000 baud, divisor = 24000000 / 16 / 1500000 = 1.
 */

/* CRU register addresses for UART5 clock gating */

#define PERICRU_BASE        RV1126B_PERICRU_BASE
#define BUSCRU_BASE         RV1126B_BUSCRU_BASE
#define TOPCRU_BASE         RV1126B_TOPCRU_BASE

/* UART5 clock gate registers (same GATE_CON registers as UART4, different
 * bit positions).
 */

#define CONSOLE_SCLK_GATE_REG  (TOPCRU_BASE + 0x804)  /* GATE_CON01 */
#define CONSOLE_PCLK_GATE_REG  (BUSCRU_BASE + 0x810)  /* GATE_CON04 */

/* UART5 soft reset registers (same SOFTRST_CON05 register as UART4,
 * different bit positions).
 */

#define CONSOLE_SOFTRST_REG    (BUSCRU_BASE + 0xa14)  /* SOFTRST_CON05 */

/* GPIO4 IOC base for UART5 M0 pin mux (TX=GPIO4_PA6, RX=GPIO4_PA7) */

#define GPIO4_IOC_BASE      RV1126B_GPIO4_IOC_BASE

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_uart5_enable_clock
 *
 * Description:
 *   Enable the UART5 peripheral and source clocks via CRU gate registers.
 *
 ****************************************************************************/

static void rv1126b_uart5_enable_clock(void)
{
  /* Select 24 MHz oscillator as UART5 source clock.
   * CLKSEL_CON14 at TOPCRU + 0x338:
   *   Bits [15:13] = SCLK_UART5_SRC_SEL (0 = XIN_OSC0 / 24 MHz)
   *   Bits [12:8]  = SCLK_UART5_SRC_DIV (0 = divide by 1)
   * Use Rockchip high-word write-mask semantics so unrelated fields are not
   * affected.
   */

  putreg32(RV1126B_HIWORD_UPDATE((0x7u << 13) | (0x1fu << 8), 0),
           TOPCRU_BASE + 0x338);

  /* Enable UART5 source clock gate (SCLK_UART5_SRC_GATE in GATE_CON01)
   * Bit 14 of GATE_CON01 at TOPCRU + 0x804.
   * Gate bits are active-high disables, so write data 0 to enable.
   */

  putreg32(RV1126B_HIWORD_UPDATE(RV1126B_SCLK_UART5_SRC_GATE, 0),
           CONSOLE_SCLK_GATE_REG);

  /* Enable UART5 peripheral clock gate (PCLK_UART5_GATE in GATE_CON04)
   * Bit 13 of GATE_CON04 at BUSCRU + 0x810.
   */

  putreg32(RV1126B_HIWORD_UPDATE(RV1126B_PCLK_UART5_GATE, 0),
           CONSOLE_PCLK_GATE_REG);
}

/****************************************************************************
 * Name: rv1126b_uart5_deassert_reset
 *
 * Description:
 *   De-assert UART5 soft reset via CRU reset registers.
 *
 ****************************************************************************/

static void rv1126b_uart5_deassert_reset(void)
{
  /* De-assert UART5 presetn and sresetn (SOFTRST_CON05 at BUSCRU + 0xA14)
   * Bits 8-9: PRESETN_UART5, SRESETN_UART5.
   * Rockchip SOFTRST convention: data 1 asserts reset, data 0 releases
   * reset, and the high word selects the affected bits.
   */

  putreg32(RV1126B_HIWORD_UPDATE(RV1126B_SRST_PRESETN_UART5 |
                                 RV1126B_SRST_SRESETN_UART5, 0),
           CONSOLE_SOFTRST_REG);
}

/****************************************************************************
 * Name: rv1126b_uart5_config_pins
 *
 * Description:
 *   Configure GPIO4_A6 (TX) and GPIO4_A7 (RX) pin mux for UART5 M0
 *   function.  The IOC (I/O Controller) registers select the pin function.
 *
 *   SDK-verified mapping: UART5 M0 -> GPIO4_PA6 FUNC5, GPIO4_PA7 FUNC5.
 *
 ****************************************************************************/

static void rv1126b_uart5_config_pins(void)
{
  /* GPIO4_A6 = UART5_TXM0 (FUNC5)
   * GPIO4_A7 = UART5_RXM0 (FUNC5)
   *
   * Both pins share GPIO4A_IOMUX_SEL_1 at GPIO4_IOC_BASE + 0x84:
   *   Bits [11:8]  = GPIO4_A6 function select (5 = UART5)
   *   Bits [15:12] = GPIO4_A7 function select (5 = UART5)
   *
   * SDK-verified: rv1126b.h GPIO4A6_SEL_SHIFT=8, GPIO4A7_SEL_SHIFT=12
   * Linux DTS: <4 RK_PA7 5 &pcfg_pull_up>, <4 RK_PA6 5 &pcfg_pull_up>
   * High-word masked write value: 0xff005500.
   */

  putreg32(RV1126B_HIWORD_UPDATE((0xfu << 8) | (0xfu << 12),
                                 (5u << 8) | (5u << 12)),
           GPIO4_IOC_BASE + 0x84);  /* GPIO4A_IOMUX_SEL_1 */

  /* Enable pull-up on UART5 TX (GPIO4_PA6) and RX (GPIO4_PA7) pins
   * to prevent spurious characters when line is undriven.
   * GPIO4A_PULL at GPIO4_IOC_BASE + 0x340:
   *   bits [13:12] = GPIO4_A6 pull (1 = pull-up)
   *   bits [15:14] = GPIO4_A7 pull (1 = pull-up)
   * SDK/HAL-confirmed combined high-word write value: 0xf0005000.
   */

  putreg32(RV1126B_HIWORD_UPDATE((0x3u << 12) | (0x3u << 14),
                                 (1u << 12) | (1u << 14)),
           GPIO4_IOC_BASE + 0x340);  /* GPIO4A_PULL */
}

/****************************************************************************
 * Name: rv1126b_uart5_init_hw
 *
 * Description:
 *   Initialize UART5 hardware: reset, set baud rate, configure 8N1, enable
 *   FIFO.
 *
 ****************************************************************************/

static void rv1126b_uart5_init_hw(void)
{
  /* Software reset UART5 */

  putreg32(RV1126B_UART_SRR_UR | RV1126B_UART_SRR_RFR |
           RV1126B_UART_SRR_XFR, UART_SRR(UART_BASE));

  /* Disable all interrupts */

  putreg32(0, UART_IER(UART_BASE));

  /* Enable FIFO, reset TX and RX FIFOs */

  putreg32(RV1126B_UART_FCR_FIFOE | RV1126B_UART_FCR_RFIFOR |
           RV1126B_UART_FCR_XFIFOR, UART_FCR(UART_BASE));
  putreg32(RV1126B_UART_FCR_FIFOE | RV1126B_UART_FCR_TFT_TWO |
           RV1126B_UART_FCR_RT_HALF, UART_FCR(UART_BASE));

  /* Set baud rate: enable DLAB, write divisor, disable DLAB
   * Divisor is computed from CONSOLE_CLK_RATE and CONSOLE_BAUD_RATE
   * in rv1126b_config.h (Kconfig-derived, default 1500000 baud).
   */

  putreg32(RV1126B_UART_LCR_DLAB, UART_LCR(UART_BASE));
  putreg32(CONSOLE_DIVISOR & 0xff, UART_DLL(UART_BASE));
  putreg32((CONSOLE_DIVISOR >> 8) & 0xff, UART_DLH(UART_BASE));

  /* Set line control: 8 data bits, no parity, 1 stop bit (8N1) */

  putreg32(RV1126B_UART_LCR_DLS_8 | RV1126B_UART_LCR_STOP_1,
           UART_LCR(UART_BASE));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_lowsetup
 *
 * Description:
 *   Early UART5 initialization for low-level debug output.
 *   This is called very early in the boot sequence before the OS is
 *   fully initialized.
 *
 ****************************************************************************/

void rv1126b_lowsetup(void)
{
  /* Enable UART5 clocks */

  rv1126b_uart5_enable_clock();

  /* De-assert UART5 reset */

  rv1126b_uart5_deassert_reset();

  /* Configure pin mux for UART5 TX/RX (GPIO4_PA6/PA7 FUNC5) */

  rv1126b_uart5_config_pins();

  /* Initialize UART5 hardware */

  rv1126b_uart5_init_hw();
}

/****************************************************************************
 * Name: riscv_lowputc
 *
 * Description:
 *   Output one character on UART5 using polled I/O.
 *   This function is used for very early console output before the full
 *   serial driver is initialized.
 *
 ****************************************************************************/

void riscv_lowputc(char ch)
{
  uint32_t timeout = RV1126B_UART_TX_TIMEOUT;

  /* Wait until TX FIFO is not full */

  while (!(getreg32(UART_USR(UART_BASE)) & RV1126B_UART_USR_TFNF))
    {
      if (timeout-- == 0)
        {
          return;
        }
    }

  /* Write the character to the transmit holding register */

  putreg32((uint32_t)ch, UART_THR(UART_BASE));
}

/****************************************************************************
 * Name: riscv_lowgetc
 *
 * Description:
 *   Read one character from UART5 using polled I/O.
 *   Returns -1 if no character is available.
 *
 ****************************************************************************/

int riscv_lowgetc(void)
{
  /* Check if RX FIFO has data */

  if (!(getreg32(UART_USR(UART_BASE)) & RV1126B_UART_USR_RFNE))
    {
      return -1;
    }

  /* Read the character from the receive buffer register */

  return (int)(getreg32(UART_RBR(UART_BASE)) & 0xff);
}
