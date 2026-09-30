/* SPDX-License-Identifier: Apache-2.0 */
/* Bare-metal emulator fixture, not a NuttX or production firmware image. */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define UART 0x44820000u
#define SHARED 0x28000000u
#define DTCM 0x20000000u

static volatile uint32_t ticks;
static volatile uint32_t received;
static volatile uint32_t nmi_seen;
static void cp_start(void);
static void ap1_start(void);
static void ap2_start(void);
static void fault(void);
static void tick(void);
static void nmi(void);
static void uart_irq(void);

__attribute__((section(".vectors.cp"), used))
const uintptr_t cp_vectors[80] =
{
  [0] = 0x20004000,
  [1] = (uintptr_t)cp_start,
  [2] = (uintptr_t)nmi,
  [3] = (uintptr_t)fault,
  [4] = (uintptr_t)fault,
  [5] = (uintptr_t)fault,
  [6] = (uintptr_t)fault,
  [15] = (uintptr_t)tick,
  [20] = (uintptr_t)uart_irq,
};

__attribute__((section(".vectors.ap1"), used))
static const uintptr_t ap1_vectors[16] =
{
  [0] = 0x20004000,
  [1] = (uintptr_t)ap1_start,
  [3] = (uintptr_t)fault,
};

__attribute__((section(".vectors.ap2"), used))
static const uintptr_t ap2_vectors[16] =
{
  [0] = 0x20004000,
  [1] = (uintptr_t)ap2_start,
  [3] = (uintptr_t)fault,
};

static void text(const char *p)
{
  while (*p)
    {
      while (!(REG(UART + 0x18) & (1u << 20)))
        {
        }
      REG(UART + 0x1c) = (uint8_t)*p++;
    }
}

__attribute__((noreturn)) static void finish(uint32_t status)
{
  uint32_t args[2] = {0x20026, status};
  register uint32_t r0 __asm__("r0") = 0x20;
  register uint32_t *r1 __asm__("r1") = args;
  __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
  for (;;) {}
}

static void fault(void)
{
  text("BK7258 FAULT\n");
  finish(1);
}

static void nmi(void)
{
  uint32_t exception;
  __asm__ volatile("mrs %0, ipsr" : "=r"(exception));
  if (exception != 2) { fault(); }
  nmi_seen++;
}

static void tick(void)
{
  ticks++;
}

static void uart_irq(void)
{
  if (REG(UART + 0x18) & (1u << 21))
    {
      received = (REG(UART + 0x1c) >> 8) & 0xff;
    }
  REG(UART + 0x24) = 0xff;
}

static void ap1_start(void)
{
  REG(DTCM) = 1;
  REG(SHARED + 4) = 0xa1000001;
  for (;;) { __asm__ volatile("wfi"); }
}

static void ap2_start(void)
{
  REG(DTCM) = 2;
  REG(SHARED + 12)++;
  REG(SHARED + 8) = 0xa2000002;
  for (;;) { __asm__ volatile("wfi"); }
}

static void cp_start(void)
{
  ticks = received = nmi_seen = 0;
  REG(UART + 0x08) = 1;
  REG(UART + 0x10) = 0xe11b; /* 26 MHz, 115200 baud, 8N1, TX/RX */
  REG(UART + 0x14) = 0x100;
  text("BK7258 CP UART OK\n");
  if (REG(0x44010014) != 2 || REG(0x44010018) != 10 ||
      REG(0x44010080) != 0) { fault(); }

  REG(SHARED) = 0x72580000;
  if (REG(0x08000000) != REG(SHARED) ||
      REG(0x18000000) != REG(SHARED) ||
      REG(0x38000000) != REG(SHARED))
    {
      fault();
    }
  text("BK7258 SRAM ALIASES OK\n");

  /* APB watchdog must execute CP NMI even with maskable interrupts off. */
  REG(0x44010028) = 0xc;
  REG(0x44010030) = 1u << 31;
  REG(0x44800008) = 1;
  __asm__ volatile("cpsid i" ::: "memory");
  REG(0x44800010) = 0x5a0002;
  REG(0x44800010) = 0xa50002;
  for (uint32_t n = 0; !nmi_seen && n < 10000000; n++)
    { __asm__ volatile("nop"); }
  __asm__ volatile("cpsie i" ::: "memory");
  if (nmi_seen != 1) { fault(); }
  REG(0x44800010) = 0x5a0000;
  REG(0x44800010) = 0xa50000;
  text("BK7258 WATCHDOG NMI OK\n");

  /* Use the real CPU's secure SysTick and exception entry. */
  REG(0xe000e014) = 25999;
  REG(0xe000e018) = 0;
  REG(0xe000e010) = 7;
  while (ticks < 2) { __asm__ volatile("wfi"); }
  text("BK7258 SYSTICK IRQ OK\n");
  REG(0xe000e010) = 0;
  REG(0xe000e014) = 31;
  REG(0xe000e018) = 0;
  ticks = 0;
  REG(0xe000e010) = 3;
  while (ticks < 2) { __asm__ volatile("wfi"); }
  text("BK7258 EXTERNAL 32K SYSTICK OK\n");

  REG(DTCM) = 0;
  REG(SHARED + 4) = REG(SHARED + 8) = REG(SHARED + 12) = 0;
  REG(0x44010014) = (uintptr_t)ap1_vectors | 1;
  REG(0x44010018) = (uintptr_t)ap2_vectors | 1;
  while (REG(SHARED + 4) != 0xa1000001 ||
         REG(SHARED + 8) != 0xa2000002)
    {
      __asm__ volatile("wfi");
    }
  if (REG(DTCM) != 0 || REG(0x30000000) != 0) { fault(); }
  text("BK7258 CPU1 CPU2 RELEASE AND PRIVATE TCM OK\n");

  /* Upper vector bits are software tokens after release, not reset strobes. */
  REG(0x44010018) ^= 0x100;
  REG(0x44010018) ^= 0x100;
  REG(0x44010018) |= 8;
  REG(0x44010018) &= ~8u;
  ticks = 0;
  while (ticks < 2) { __asm__ volatile("wfi"); }
  if (REG(SHARED + 12) != 1) { fault(); }
  REG(0x44010018) &= ~1u;
  REG(0x44010018) |= 1;
  while (REG(SHARED + 12) != 2) { __asm__ volatile("wfi"); }
  text("BK7258 HALT RESUME AND RESET OK\n");
  if (REG(SHARED + 16) == 0x7258abcd)
    {
      REG(SHARED + 16) = 0;
      text("BK7258 SYSTEM RESET OK\nBK7258 DIAGNOSTIC PASS\n");
      finish(0);
    }
  REG(UART + 0x24) = 0xff;
  REG(UART + 0x20) = 2;
  REG(0x44010080) = 1u << 4;
  REG(0xe000e100) = 1u << 4;
  text("BK7258 WAIT RX\n");
  while (!received) { __asm__ volatile("wfi"); }
  if (received != 'Z') { fault(); }
  text("BK7258 UART RX IRQ OK\n");
  REG(SHARED + 16) = 0x7258abcd;
  text("BK7258 SYSTEM RESET REQUEST\n");
  REG(0xe000ed0c) = 0x05fa0004;
  for (;;) {}
}
