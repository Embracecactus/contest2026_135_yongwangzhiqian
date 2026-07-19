/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_diag_uart.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding UART1 diagnostic output helpers for BK7258 bring-up code.
 * These helpers touch only UART1 FIFO MMIO and do not depend on .data/.bss,
 * printf, or the NuttX console device.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DIAG_UART_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DIAG_UART_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef BK7258_CDIAG_UART1_BASE
#  define BK7258_CDIAG_UART1_BASE       0x45830000u
#endif

#ifndef BK7258_CDIAG_UART1_FSTAT
#  define BK7258_CDIAG_UART1_FSTAT \
    (*(volatile uint32_t *)(BK7258_CDIAG_UART1_BASE + 0x18u))
#endif

#ifndef BK7258_CDIAG_UART1_FPORT
#  define BK7258_CDIAG_UART1_FPORT \
    (*(volatile uint32_t *)(BK7258_CDIAG_UART1_BASE + 0x1cu))
#endif

#ifndef BK7258_CDIAG_UART1_READY
#  define BK7258_CDIAG_UART1_READY      (1u << 20)
#endif

/****************************************************************************
 * Public Functions (static inline)
 ****************************************************************************/

static inline void bk7258_clockdiag_putc(unsigned char c)
{
  while ((BK7258_CDIAG_UART1_FSTAT & BK7258_CDIAG_UART1_READY) == 0)
    {
    }

  BK7258_CDIAG_UART1_FPORT = (uint32_t)(c & 0xffu);
}

static inline void bk7258_clockdiag_puts(const char *s)
{
  while (*s)
    {
      bk7258_clockdiag_putc((unsigned char)*s);
      s++;
    }
}

static inline void bk7258_clockdiag_putnibble(unsigned int n)
{
  bk7258_clockdiag_putc((unsigned char)(n < 10u ? ('0' + (char)n)
                                                : ('a' + (char)(n - 10u))));
}

static inline void bk7258_clockdiag_puthex(uint32_t v, int width)
{
  int i;

  for (i = width - 1; i >= 0; i--)
    {
      bk7258_clockdiag_putnibble((unsigned int)((v >> (i * 4)) & 0xfu));
    }
}

static inline void bk7258_clockdiag_putreg(const char *tag, uint32_t v)
{
  bk7258_clockdiag_puts(tag);
  bk7258_clockdiag_putc('=');
  bk7258_clockdiag_puthex(v, 8);
}

static inline void bk7258_clockdiag_putfield(const char *label,
                                             uint32_t v, int width)
{
  bk7258_clockdiag_puts(label);
  bk7258_clockdiag_putc('=');
  bk7258_clockdiag_puthex(v, width);
}

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DIAG_UART_H */
