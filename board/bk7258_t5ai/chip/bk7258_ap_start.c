/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_ap_start.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reset entry for physical CPU1 running AP-local logical core 0.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/init.h>

#include <stdint.h>

#include <arch/chip/bk7258_amp.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SCB_VTOR          (*(volatile uint32_t *)0xe000ed08u)
#define BK7258_SCB_CPACR         (*(volatile uint32_t *)0xe000ed88u)
#define BK7258_FPU_FPCCR         (*(volatile uint32_t *)0xe000ef34u)

#define HEAP_BASE  ((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE)

/****************************************************************************
 * Public Data
 ****************************************************************************/

const uintptr_t g_idle_topstack = HEAP_BASE;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void __start(void)
{
#ifndef CONFIG_BUILD_PIC
  const uint32_t *src;
  uint32_t *dest;
#endif
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  uint32_t msp;

  __asm volatile ("cpsid i");
  __asm volatile ("mrs %0, msp" : "=r"(msp));

  /* Match the official AP namespace: local core 0 maps to physical CPU1. */

  *(volatile uint32_t *)BK7258_LOCAL_CORE_ID_ADDR = 0;

  state->local_core_id  = 0;
  state->physical_core_id = BK7258_AP_PHYSICAL_ID_OFFSET;
  state->initial_msp    = msp;
  state->initial_vtor   = BK7258_AP_FLASH_ADDR;

  BK7258_SCB_VTOR = BK7258_AP_FLASH_ADDR;
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  /* Disable automatic/lazy FP context stacking before enabling CP10/CP11. */

  BK7258_SCB_CPACR &= ~((3u << 20) | (3u << 22));
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  BK7258_FPU_FPCCR &= ~((1u << 31) | (1u << 30) | (1u << 29));
  BK7258_SCB_CPACR |= ((3u << 20) | (3u << 22));
  __asm volatile ("dsb sy; isb sy" ::: "memory");

#ifndef CONFIG_BUILD_PIC
  src = (const uint32_t *)_eronly;
  dest = (uint32_t *)_sdata;
  while (dest < (uint32_t *)_edata)
    {
      *dest++ = *src++;
    }

#ifndef CONFIG_ARCH_SKIP_ZERO_BSS
  dest = (uint32_t *)_sbss;
  while (dest < (uint32_t *)_ebss)
    {
      *dest++ = 0;
    }
#endif
#endif

  __asm volatile ("dmb sy" ::: "memory");
  nx_start();

  for (; ; )
    {
      __asm volatile ("wfe");
    }
}
