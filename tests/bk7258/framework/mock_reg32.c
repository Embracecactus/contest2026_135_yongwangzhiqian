/*
 * mock_reg32.c - host-side redirectable MMIO map.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mock_reg32.h"

#include <string.h>

struct mock_reg_window
{
  uintptr_t base;
  uintptr_t limit;
  uint32_t *mem;
};

static uint32_t g_dummy_word;
static uint32_t g_aon[0x1000u / sizeof(uint32_t)];
static uint32_t g_sys[0x400u / sizeof(uint32_t)];
static uint32_t g_flash[0x1000u / sizeof(uint32_t)];
static uint32_t g_wdt[0x1000u / sizeof(uint32_t)];
static uint32_t g_uart0[0x200u / sizeof(uint32_t)];
static uint32_t g_uart1[0x200u / sizeof(uint32_t)];
static uint32_t g_uart2[0x200u / sizeof(uint32_t)];
static uint32_t g_memcheck[0x1000u / sizeof(uint32_t)];
static uint32_t g_otp[0x1000u / sizeof(uint32_t)];
static uint32_t g_dubhe[0x400u / sizeof(uint32_t)];
static uint32_t g_scb[0x1000u / sizeof(uint32_t)];
static uint32_t g_tcm[0x100u / sizeof(uint32_t)];
static uint32_t g_apstate[0x10u / sizeof(uint32_t)];
static uint32_t g_scale1[0x1000u / sizeof(uint32_t)];
static uint32_t g_irda[0x10u / sizeof(uint32_t)];

static const struct mock_reg_window g_windows[] =
{
  { BK7258_MOCK_AON_BASE,      BK7258_MOCK_AON_BASE + 0x1000u,      g_aon },
  { BK7258_MOCK_SYS_BASE,      BK7258_MOCK_SYS_BASE + 0x400u,       g_sys },
  { BK7258_MOCK_FLASH_BASE,    BK7258_MOCK_FLASH_BASE + 0x1000u,    g_flash },
  { BK7258_MOCK_WDT_BASE,      BK7258_MOCK_WDT_BASE + 0x1000u,      g_wdt },
  { BK7258_MOCK_UART0_BASE,    BK7258_MOCK_UART0_BASE + 0x200u,     g_uart0 },
  { BK7258_MOCK_UART1_BASE,    BK7258_MOCK_UART1_BASE + 0x200u,     g_uart1 },
  { BK7258_MOCK_UART2_BASE,    BK7258_MOCK_UART2_BASE + 0x200u,     g_uart2 },
  { BK7258_MOCK_MEMCHECK_BASE, BK7258_MOCK_MEMCHECK_BASE + 0x1000u, g_memcheck },
  { BK7258_MOCK_OTP_BASE,      BK7258_MOCK_OTP_BASE + 0x1000u,      g_otp },
  { BK7258_MOCK_DUBHE_BASE,    BK7258_MOCK_DUBHE_BASE + 0x400u,     g_dubhe },
  { BK7258_MOCK_SCB_BASE,      BK7258_MOCK_SCB_BASE + 0x1000u,     g_scb },
  { BK7258_MOCK_TCM_BASE,      BK7258_MOCK_TCM_BASE + 0x100u,       g_tcm },
  { BK7258_MOCK_APSTATE_ADDR,  BK7258_MOCK_APSTATE_ADDR + 0x10u,    g_apstate },
  { BK7258_MOCK_SCALE1_BASE,   BK7258_MOCK_SCALE1_BASE + 0x1000u,   g_scale1 },
  { BK7258_MOCK_IRDA_BASE,     BK7258_MOCK_IRDA_BASE + 0x10u,       g_irda },
};

#define WINDOW_COUNT (sizeof(g_windows) / sizeof(g_windows[0]))

uint32_t *mock_reg32_ref(uintptr_t addr)
{
  size_t index;

  for (index = 0; index < WINDOW_COUNT; index++)
    {
      if (addr >= g_windows[index].base && addr < g_windows[index].limit)
        {
          return &g_windows[index].mem[(addr - g_windows[index].base) /
                                       sizeof(uint32_t)];
        }
    }

  return &g_dummy_word;
}

uint32_t mock_reg32_read(uintptr_t addr)
{
  return *mock_reg32_ref(addr);
}

void mock_reg32_write(uintptr_t addr, uint32_t value)
{
  *mock_reg32_ref(addr) = value;
}

void mock_reg32_set(uintptr_t addr, uint32_t value)
{
  mock_reg32_write(addr, value);
}

void mock_reg32_reset(void)
{
  size_t window;
  size_t index;

  for (window = 0; window < WINDOW_COUNT; window++)
    {
      size_t words = (g_windows[window].limit - g_windows[window].base) /
                     sizeof(uint32_t);

      for (index = 0; index < words; index++)
        {
          g_windows[window].mem[index] = 0;
        }
    }
}