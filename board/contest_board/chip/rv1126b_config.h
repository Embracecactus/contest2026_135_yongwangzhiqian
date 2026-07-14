/****************************************************************************
 * NuttX - RV1126B BSP Configuration
 *
 * Console UART selection, clock rate, and baud rate definitions shared
 * between the early lowputc driver and the full serial driver.
 *
 * This BSP uses a custom rv1126b_serial.c driver rather than the generic
 * 16550 driver, so CONFIG_16550_UART is NOT required.
 ****************************************************************************/

#ifndef __BOARD_CHIP_RV1126B_CONFIG_H
#define __BOARD_CHIP_RV1126B_CONFIG_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* UART configuration
 *
 * This BSP uses a custom rv1126b_serial.c driver rather than the generic
 * 16550 driver.  The console is selected via CONFIG_RV1126B_UART5_CONSOLE
 * defined in the chip Kconfig.
 */

#if defined(CONFIG_RV1126B_UART5_CONSOLE)
#  define HAVE_UART           1
#  define HAVE_SERIAL_CONSOLE 1
#  define CONSOLE_UART        5
#  define CONSOLE_BASE        RV1126B_UART5_BASE
#else
#  define HAVE_UART           0
#  define HAVE_SERIAL_CONSOLE 0
#endif

/* GPIO configuration */

#if defined(CONFIG_DEV_GPIO)
#  define HAVE_GPIO           1
#else
#  define HAVE_GPIO           0
#endif

/* Timer configuration */

#define HAVE_TIMER            1
#define TIMER_FREQUENCY       396000000  /* 396 MHz timer input clock */

/* UART5 source clock rate (24 MHz oscillator, SDK-verified) */

#define CONSOLE_CLK_RATE      24000000

/* Console baud rate and divisor latch value.
 * SDK default for all UARTs on this SoC is 1500000 baud.
 */

#define CONSOLE_BAUD_RATE     1500000

#define CONSOLE_DIVISOR       (CONSOLE_CLK_RATE / 16 / CONSOLE_BAUD_RATE)

#endif /* __BOARD_CHIP_RV1126B_CONFIG_H */
