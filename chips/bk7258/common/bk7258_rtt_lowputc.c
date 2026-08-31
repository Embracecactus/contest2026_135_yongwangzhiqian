/****************************************************************************
 * chips/bk7258/common/
 * bk7258_rtt_lowputc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 low-level output for the mutually exclusive SWD/RTT profile.
 ****************************************************************************/

#include <nuttx/config.h>

#include <SEGGER_RTT.h>

#ifndef CONFIG_SERIAL_RTT_CONSOLE_CHANNEL
#  define CONFIG_SERIAL_RTT_CONSOLE_CHANNEL 0
#endif

void arm_lowputc(char ch)
{
  (void)SEGGER_RTT_PutChar(CONFIG_SERIAL_RTT_CONSOLE_CHANNEL, ch);
}

void arm_lowputs(const char *str)
{
  while (*str != '\0')
    {
      arm_lowputc(*str++);
    }
}

void up_putc(int ch)
{
  arm_lowputc((char)ch);
}
