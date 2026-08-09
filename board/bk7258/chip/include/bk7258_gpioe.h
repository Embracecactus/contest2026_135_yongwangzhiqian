/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_gpioe.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) general-purpose multi-pin GPIO — NuttX ioexpander
 * lower-half.
 *
 * Exposes BK7258 GPIO0..GPIO47 through the standard NuttX ioexpander
 * interface (ioexpander_dev_s / ioexpander_ops_s).  Consumer drivers use
 * the IOEXP_* / IOEP_* helpers, and gpio_lower_half() registers individual
 * pins as /dev/gpioN.
 *
 * AP role (verified against the armino SDK source):
 *   - Both AP and CP libdriver.a export the bk_gpio_* symbols, but the AP
 *     bk_gpio_driver_init() is the SDK-canonical path: it registers the
 *     GPIO ISR (INT_SRC_GPIO / GPIO_NS) AND enables CPU interrupt
 *     forwarding via sys_drv_int_group2_enable().  The CP variant only
 *     registers the ISR and relies on the low-power path plus the manual
 *     bk7258_gpio_cp_irq_enable() wrapper.  This driver therefore lives on
 *     the AP core, consistent with the I2C/SPI/SDIO/SARADC wrappers.
 *   - Per-pin interrupt dispatch is exact: bk_gpio_register_isr(id, isr)
 *     stores one callback per GPIO id in the SDK's s_gpio_isr[] table, and
 *     the SDK's gpio_isr() calls it directly for the triggering pin.
 *
 * SDK pin identity: NuttX ioexpander pin N maps one-to-one to gpio_id_t
 * GPIO_N.  CONFIG_BK7258_GPIOE_NPINS limits how many pins are exposed.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_GPIOE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_GPIOE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_GPIOE
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_gpioe_initialize
 *
 * Description:
 *   Construct the BK7258 general-purpose ioexpander lower half covering
 *   GPIO0..GPIO(CONFIG_BK7258_GPIOE_NPINS-1).  No pin is configured until
 *   the ioexpander interface is used (IOEXP_SETDIRECTION / IOEXP_SETOPTION
 *   / IOEXP_WRITEPIN), so this is safe to call early in board bring-up.
 *
 *   On the AP core bk_gpio_driver_init() runs lazily on the first pin
 *   operation (idempotent in the SDK), enabling GPIO interrupts along the
 *   way.
 *
 * Returned Value:
 *   A pointer to the ioexpander interface, or NULL on failure.  Callers
 *   typically feed this to gpio_lower_half() to publish /dev/gpioN.
 *
 ****************************************************************************/

FAR struct ioexpander_dev_s *bk7258_gpioe_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_GPIOE */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_GPIOE_H */
