/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_clock.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) deterministic 320 MHz CPU0 clock bring-up.
 *
 * bk7258_clock_bringup_320m() mirrors the Armino SDK early-init clock path
 * (sys_hal_early_init -> sys_hal_dpll_cpu_flash_time_early_init ->
 * sys_hal_cali_dpll + sys_hal_core_bus_clock_ctrl) so that NuttX itself
 * drives the core clock to 320 MHz (DPLL /1.5, the highest board-verified
 * operating point on this chip; 480 MHz direct is SDK-rejected).
 *
 * It raises VDDDIG to 0x0c (0.9 V) and VDDD to 0x06 (1.0 V) and completes a
 * DPLL SPI recalibration before switching the core mux, matching the SDK.  It
 * does not perform any runtime DVFS: it sets a single fixed 320 MHz target
 * once at boot.
 *
 * Called early in __start(), before nx_start(), so bk7258_clockdiag_current
 * _cpu_hz() observes M1 low bits = 0x20 (csrc=2, cdiv=0) and up_timer_
 * initialize() arms the correct SysTick reload.  The UART1 console runs off
 * an independent clocking path and survives the core mux switch.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCK_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCK_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_clock_bringup_320m
 *
 * Description:
 *   Bring the CPU0 core clock up to 320 MHz deterministically.  Ensures the
 *   DPLL is enabled (running the SDK SPI recalibration), raises VDDDIG/VDDD
 *   to the voltage the SDK guard requires for 320M, then switches the core
 *   source/divider.  No effect on SysTick here; the caller's runtime
 *   detection arms the reload for the new frequency.
 *
 * Returned Value:
 *   None.  A failed or stalled recalibration is recoverable by re-flash; this
 *   routine does not return in a way callers can react to (it executes before
 *   the scheduler exists).
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_CLOCK_320M
void bk7258_clock_bringup_320m(void);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCK_H */