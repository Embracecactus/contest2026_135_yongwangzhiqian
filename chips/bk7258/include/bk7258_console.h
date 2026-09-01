/****************************************************************************
 * chips/bk7258/include/bk7258_console.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Compile-time BK7258 console and UART ownership contract shared by early
 * MMIO output, the SDK-backed serial lower half and fault diagnostics.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CONSOLE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CONSOLE_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/* UART register layout is identical for all three instances. */

#define BK7258_UART0_BASE             0x44820000u
#define BK7258_UART1_BASE             0x45830000u
#define BK7258_UART2_BASE             0x45840000u
#define BK7258_UART_GLOBAL_CTRL_OFF   0x08u
#define BK7258_UART_CONFIG_OFF        0x10u
#define BK7258_UART_FIFO_CONFIG_OFF   0x14u
#define BK7258_UART_FIFO_STATUS_OFF   0x18u
#define BK7258_UART_FIFO_PORT_OFF     0x1cu
#define BK7258_UART_INT_ENABLE_OFF    0x20u
#define BK7258_UART_INT_STATUS_OFF    0x24u

#define BK7258_UART_GLOBAL_ENABLE     0x00000001u
#define BK7258_UART_RX_THRESHOLD      0x00000100u
#define BK7258_UART_RX_THRESHOLD_MASK 0x0000ff00u
#define BK7258_UART_INT_STATUS_ALL    0x000000ffu
#define BK7258_UART_TX_EMPTY          (1u << 17)
#define BK7258_UART_TX_READY          (1u << 20)
#define BK7258_UART_RX_READY          (1u << 21)
#define BK7258_UART_XTAL_HZ           26000000u
#define BK7258_UART_TX_POLL_LIMIT     100000u
#define BK7258_UART_RX_DRAIN_LIMIT    256u

#if defined(CONFIG_BK7258_CONSOLE_UART0)
#  define BK7258_CONSOLE_UART_ID        0
#  define BK7258_CONSOLE_UART_BASE      BK7258_UART0_BASE
#  define BK7258_CONSOLE_BAUD           CONFIG_BK7258_UART0_BAUD
#  define BK7258_CONSOLE_DATA_BITS      CONFIG_BK7258_UART0_DATA_BITS
#  define BK7258_CONSOLE_PARITY         CONFIG_BK7258_UART0_PARITY
#  define BK7258_CONSOLE_STOP_BITS      CONFIG_BK7258_UART0_STOP_BITS
#  define BK7258_CONSOLE_CLK_ENABLE_BIT (1u << 2)
#  define BK7258_CONSOLE_CLK_SELECT_BIT (1u << 10)
#elif defined(CONFIG_BK7258_CONSOLE_UART1)
#  define BK7258_CONSOLE_UART_ID        1
#  define BK7258_CONSOLE_UART_BASE      BK7258_UART1_BASE
#  define BK7258_CONSOLE_BAUD           CONFIG_BK7258_UART1_BAUD
#  define BK7258_CONSOLE_DATA_BITS      CONFIG_BK7258_UART1_DATA_BITS
#  define BK7258_CONSOLE_PARITY         CONFIG_BK7258_UART1_PARITY
#  define BK7258_CONSOLE_STOP_BITS      CONFIG_BK7258_UART1_STOP_BITS
#  define BK7258_CONSOLE_CLK_ENABLE_BIT (1u << 10)
#  define BK7258_CONSOLE_CLK_SELECT_BIT (1u << 13)
#elif defined(CONFIG_BK7258_CONSOLE_UART2)
#  define BK7258_CONSOLE_UART_ID        2
#  define BK7258_CONSOLE_UART_BASE      BK7258_UART2_BASE
#  define BK7258_CONSOLE_BAUD           CONFIG_BK7258_UART2_BAUD
#  define BK7258_CONSOLE_DATA_BITS      CONFIG_BK7258_UART2_DATA_BITS
#  define BK7258_CONSOLE_PARITY         CONFIG_BK7258_UART2_PARITY
#  define BK7258_CONSOLE_STOP_BITS      CONFIG_BK7258_UART2_STOP_BITS
#  define BK7258_CONSOLE_CLK_ENABLE_BIT (1u << 11)
#  define BK7258_CONSOLE_CLK_SELECT_BIT (1u << 16)
#endif

#if defined(CONFIG_BK7258_CONSOLE_UART0) || \
    defined(CONFIG_BK7258_CONSOLE_UART1) || \
    defined(CONFIG_BK7258_CONSOLE_UART2)
#  define BK7258_HAVE_UART_CONSOLE 1

#  define BK7258_CONSOLE_GLOBAL_CTRL \
    (BK7258_CONSOLE_UART_BASE + BK7258_UART_GLOBAL_CTRL_OFF)
#  define BK7258_CONSOLE_CONFIG \
    (BK7258_CONSOLE_UART_BASE + BK7258_UART_CONFIG_OFF)
#  define BK7258_CONSOLE_FIFO_CONFIG \
    (BK7258_CONSOLE_UART_BASE + BK7258_UART_FIFO_CONFIG_OFF)
#  define BK7258_CONSOLE_FIFO_STATUS \
    (BK7258_CONSOLE_UART_BASE + BK7258_UART_FIFO_STATUS_OFF)
#  define BK7258_CONSOLE_FIFO_PORT \
    (BK7258_CONSOLE_UART_BASE + BK7258_UART_FIFO_PORT_OFF)
#  define BK7258_CONSOLE_INT_ENABLE \
    (BK7258_CONSOLE_UART_BASE + BK7258_UART_INT_ENABLE_OFF)
#  define BK7258_CONSOLE_INT_STATUS \
    (BK7258_CONSOLE_UART_BASE + BK7258_UART_INT_STATUS_OFF)

/* Match v3.1.1.9 uart_hal_set_baud_rate(): integer truncation of the
 * 26 MHz XTAL divider.  Kconfig keeps the result in the 16-bit field.
 */

#  define BK7258_CONSOLE_CLK_DIV \
    ((BK7258_UART_XTAL_HZ / BK7258_CONSOLE_BAUD) - 1u)
#  define BK7258_CONSOLE_PARITY_BITS \
    (BK7258_CONSOLE_PARITY == 0 ? 0u : \
     (1u << 5) | (BK7258_CONSOLE_PARITY == 1 ? (1u << 6) : 0u))
#  define BK7258_CONSOLE_CONFIG_VALUE \
    (0x3u | ((BK7258_CONSOLE_DATA_BITS - 5u) << 3) | \
     BK7258_CONSOLE_PARITY_BITS | \
     (BK7258_CONSOLE_STOP_BITS == 2 ? (1u << 7) : 0u) | \
     (BK7258_CONSOLE_CLK_DIV << 8))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef BK7258_HAVE_UART_CONSOLE
void bk7258_lowputc_handoff(bool enable);
void bk7258_lowputc_restore_console(void);
void bk7258_lowputc_ensure_console(void);
int bk7258_lowputc_set_format(uint32_t baud, unsigned int data_bits,
                             unsigned int parity, unsigned int stop_bits);
void bk7258_uart_recover_console(void);
#endif

#if defined(CONFIG_BK7258_UART0) || defined(CONFIG_BK7258_UART1) || \
    defined(CONFIG_BK7258_UART2)
int bk7258_uart_runtime_reinitialize(unsigned int uart);
int bk7258_uart_pm_prepare(void);
void bk7258_uart_pm_restore(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_CONSOLE_H */
