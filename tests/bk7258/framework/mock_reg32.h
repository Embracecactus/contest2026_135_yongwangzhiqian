/*
 * mock_reg32.h - host-side redirectable MMIO for BK7258 firmware modules.
 *
 * Freestanding modules read/write 32-bit "registers" through macros such as
 * REG32().  patch.py rewrites those macros in throwaway copies to
 * (*mock_reg32_ref(addr)), which resolves addresses into fixed RAM windows
 * covering the BK7258 APB/SCB/OTP regions used by bootloader and driver
 * code.  Tests preset and inspect state through mock_reg32_set/read.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BK7258_TESTS_MOCK_REG32_H
#define BK7258_TESTS_MOCK_REG32_H

#include <stddef.h>
#include <stdint.h>

/* Cover the board MMIO windows referenced by bootloader + driver modules. */
#define BK7258_MOCK_AON_BASE      0x44000000u
#define BK7258_MOCK_SYS_BASE      0x44010000u
#define BK7258_MOCK_FLASH_BASE    0x44030000u
#define BK7258_MOCK_WDT_BASE      0x44800000u
#define BK7258_MOCK_UART0_BASE    0x44820000u
#define BK7258_MOCK_UART1_BASE    0x45830000u
#define BK7258_MOCK_UART2_BASE    0x45840000u
#define BK7258_MOCK_MEMCHECK_BASE 0x44890000u
#define BK7258_MOCK_OTP_BASE      0x4b100000u
#define BK7258_MOCK_DUBHE_BASE    0x4b111000u
#define BK7258_MOCK_SCB_BASE      0xe000e000u
#define BK7258_MOCK_TCM_BASE      0xe001e000u
#define BK7258_MOCK_APSTATE_ADDR  0x2809f000u
#define BK7258_MOCK_SCALE1_BASE   0x480e0000u
#define BK7258_MOCK_IRDA_BASE     0x458b0000u

/* Resolve an address to its backing 32-bit slot.  Unmapped addresses resolve
 * to a shared dummy word; the returned pointer may be written. */
uint32_t *mock_reg32_ref(uintptr_t addr);

uint32_t mock_reg32_read(uintptr_t addr);
void mock_reg32_write(uintptr_t addr, uint32_t value);
void mock_reg32_set(uintptr_t addr, uint32_t value);

/* Zero every mapped window.  Call in setUp() for a deterministic map. */
void mock_reg32_reset(void);

#endif /* BK7258_TESTS_MOCK_REG32_H */