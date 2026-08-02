/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_ap_start.c
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
#define BK7258_SCB_CCR           (*(volatile uint32_t *)0xe000ed14u)
#define BK7258_SCB_CPACR         (*(volatile uint32_t *)0xe000ed88u)
#define BK7258_FPU_FPCCR         (*(volatile uint32_t *)0xe000ef34u)
#define BK7258_SCB_CCR_DCACHE    (1u << 16)
#define BK7258_SCB_CCSIDR        0xe000ed80u
#define BK7258_SCB_CSSELR        0xe000ed84u
#define BK7258_SCB_ICIALLU       0xe000ef50u
#define BK7258_SCB_DCISW         0xe000ef60u
#define BK7258_MPU_CTRL          0xe000ed94u
#define BK7258_MPU_RNR           0xe000ed98u
#define BK7258_MPU_RBAR          0xe000ed9cu
#define BK7258_MPU_RLAR          0xe000eda0u
#define BK7258_MPU_MAIR0         0xe000edc0u
#define BK7258_MPU_SRAM_REGION   15u
#define BK7258_MPU_SRAM_RBAR     0x2800001au
#define BK7258_MPU_SRAM_RLAR     0x3fffffe3u
#define BK7258_MPU_ATTR1_MASK    0x0000ff00u
#define BK7258_MPU_ATTR1_NOCACHE 0x00004400u
#define BK7258_MPU_CTRL_ENABLE   0x7u

#define HEAP_BASE  ((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE)

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef CONFIG_BUILD_PIC
extern uint32_t _lspinlock_data[];
extern uint32_t _sspinlock_data[];
extern uint32_t _espinlock_data[];
extern uint32_t _sspinlock_bss[];
extern uint32_t _espinlock_bss[];
#endif

const uintptr_t g_idle_topstack = HEAP_BASE;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void __attribute__((naked, noinline, used))
bk7258_ap_smp_memory_initialize(void)
{
  /* Match Beken's bk_avdk_smp v3.1.1.9 MPU contract before either AP core
   * touches NuttX SMP state.  Region 15 gives the full shared SRAM alias
   * range the official Inner Shareable, Normal Non-cacheable attributes:
   *
   *   RBAR = ARM_MPU_RBAR(0x28000000, ARM_MPU_SH_INNER, 0, 1, 0)
   *   RLAR = ARM_MPU_RLAR(0x3fffffe0, 1)
   *   MAIR attribute 1 = ARM_MPU_ATTR(0x4, 0x4)
   *
   * Keep D-cache allocation disabled for this NuttX port.  A CPU-only reset
   * can retain private lines even though CCR.DC is cleared, so invalidate
   * every L1 D-cache set/way before installing the non-cacheable region.
   * This is the reset-safe counterpart of the official v3.1.1.9 AP startup
   * sequence, which maintains the complete D-cache on both AP cores.
   *
   * Invalidate I-cache as well so a same-session AP reflash cannot execute
   * retained XIP lines.  The routine uses no stack because the reset stack
   * itself lives in the SRAM whose attributes are being replaced.
   */

  __asm volatile
    (
      "cpsid i\n"
      "dsb sy\n"
      "ldr r0, =%c[ccr]\n"
      "ldr r1, [r0]\n"
      "bic r1, r1, %c[dc]\n"
      "str r1, [r0]\n"
      "dsb sy\n"
      "isb sy\n"
      "ldr r0, =%c[csselr]\n"
      "movs r1, #0\n"
      "str r1, [r0]\n"
      "dsb sy\n"
      "ldr r0, =%c[ccsidr]\n"
      "ldr r1, [r0]\n"
      "ubfx r2, r1, #13, #15\n"
      "lsls r2, r2, #5\n"
      "ldr r0, =%c[dcisw]\n"
      "1:\n"
      "ubfx r3, r1, #3, #10\n"
      "2:\n"
      "lsl r12, r3, #30\n"
      "orr r12, r12, r2\n"
      "str r12, [r0]\n"
      "subs r3, r3, #1\n"
      "bpl 2b\n"
      "subs r2, r2, #32\n"
      "bpl 1b\n"
      "dsb sy\n"
      "isb sy\n"
      "ldr r0, =%c[iciallu]\n"
      "movs r1, #0\n"
      "str r1, [r0]\n"
      "dsb sy\n"
      "isb sy\n"
      "ldr r0, =%c[mpu_ctrl]\n"
      "movs r1, #0\n"
      "str r1, [r0]\n"
      "dsb sy\n"
      "isb sy\n"
      "ldr r0, =%c[mair0]\n"
      "ldr r1, [r0]\n"
      "ldr r2, =%c[attr_mask]\n"
      "bic r1, r1, r2\n"
      "ldr r2, =%c[attr_nocache]\n"
      "orr r1, r1, r2\n"
      "str r1, [r0]\n"
      "ldr r0, =%c[rnr]\n"
      "movs r1, %c[region]\n"
      "str r1, [r0]\n"
      "ldr r0, =%c[rbar]\n"
      "ldr r1, =%c[sram_rbar]\n"
      "str r1, [r0]\n"
      "ldr r0, =%c[rlar]\n"
      "ldr r1, =%c[sram_rlar]\n"
      "str r1, [r0]\n"
      "ldr r0, =%c[mpu_ctrl]\n"
      "movs r1, %c[mpu_enable]\n"
      "str r1, [r0]\n"
      "dsb sy\n"
      "isb sy\n"
      "bx lr\n"
      :
      : [ccr] "i" (0xe000ed14u),
        [dc] "i" (BK7258_SCB_CCR_DCACHE),
        [ccsidr] "i" (BK7258_SCB_CCSIDR),
        [csselr] "i" (BK7258_SCB_CSSELR),
        [iciallu] "i" (BK7258_SCB_ICIALLU),
        [dcisw] "i" (BK7258_SCB_DCISW),
        [mpu_ctrl] "i" (BK7258_MPU_CTRL),
        [rnr] "i" (BK7258_MPU_RNR),
        [rbar] "i" (BK7258_MPU_RBAR),
        [rlar] "i" (BK7258_MPU_RLAR),
        [mair0] "i" (BK7258_MPU_MAIR0),
        [region] "i" (BK7258_MPU_SRAM_REGION),
        [sram_rbar] "i" (BK7258_MPU_SRAM_RBAR),
        [sram_rlar] "i" (BK7258_MPU_SRAM_RLAR),
        [attr_mask] "i" (BK7258_MPU_ATTR1_MASK),
        [attr_nocache] "i" (BK7258_MPU_ATTR1_NOCACHE),
        [mpu_enable] "i" (BK7258_MPU_CTRL_ENABLE)
    );
}

void __start(void)
{
#ifndef CONFIG_BUILD_PIC
  const uint32_t *src;
  uint32_t *dest;
#endif
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  uint32_t msp;

  __asm volatile ("cpsid i");

  bk7258_ap_smp_memory_initialize();

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

  /* Initialize the official AP SMP exclusive-state region before CPU2 is
   * released.  Vendor and board recursive locks carry nonzero free-owner
   * initializers; NuttX static locks and the scheduler IPI pending words are
   * zero-initialized independently of the ordinary AP .bss range.
   */

  src = (const uint32_t *)_lspinlock_data;
  dest = (uint32_t *)_sspinlock_data;
  while (dest < (uint32_t *)_espinlock_data)
    {
      *dest++ = *src++;
    }

  dest = (uint32_t *)_sspinlock_bss;
  while (dest < (uint32_t *)_espinlock_bss)
    {
      *dest++ = 0;
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
