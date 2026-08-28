/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/
 * bk7258_gpio_cp_irq.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-specific GPIO interrupt source enable wrapper for BK7258.
 *
 * The CP SDK's gpio_driver_base.c only enables GPIO interrupt forwarding
 * to CPU0 in the low-power entry path (gpio_enter_low_power), never in
 * normal GPIO init.  This is by design: the CP core targets low-power
 * operation, and GPIO interrupt forwarding is a power-sensitive feature.
 *
 * However, NuttX on CP needs GPIO interrupts for normal operation (e.g.,
 * the GPIO lower-half edge interrupt support).  The pinned v3.1.1.9 CP
 * configuration has CONFIG_TZ=1 and CONFIG_SPE=1, so its GPIO driver
 * registers only GPIO_S (source55).  GPIO_NS aliases MAC_HSU at source37
 * and must not be claimed by this CP wrapper.
 *
 * The CP SDK provides sys_drv_int_group2_enable/disable in libdriver.a
 * but does not expose the header in the pinned includes.  The extern
 * declarations are isolated here to keep business code clean.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CPU0_INT_32_63_EN register at 0x44010084.
 * GPIO_S (source55) = bit23.
 */

#define BK7258_GPIO_CP_SOURCE_MASK  (1u << 23)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* CP SDK provides these symbols in libdriver.a but no header in pinned
 * includes.  Isolated here to avoid extern pollution in business code.
 */

extern int sys_drv_int_group2_enable(uint32_t param);
extern int sys_drv_int_group2_disable(uint32_t param);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_gpio_cp_irq_enable
 *
 * Description:
 *   Enable GPIO interrupt forwarding to CPU0 at the system level.
 *   This sets the CPU0 source enable bit for GPIO_S (source55) in the
 *   CPU0_INT_32_63_EN register.
 *
 *   The CP SDK only calls this from gpio_enter_low_power(), never from
 *   normal GPIO init.  NuttX needs it for normal GPIO interrupt operation.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bk7258_gpio_cp_irq_enable(void)
{
  sys_drv_int_group2_enable(BK7258_GPIO_CP_SOURCE_MASK);
}

/****************************************************************************
 * Name: bk7258_gpio_cp_irq_disable
 *
 * Description:
 *   Disable GPIO interrupt forwarding to CPU0 at the system level.
 *   This clears the CPU0 source enable bit for GPIO_S (source55) in the
 *   CPU0_INT_32_63_EN register.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bk7258_gpio_cp_irq_disable(void)
{
  sys_drv_int_group2_disable(BK7258_GPIO_CP_SOURCE_MASK);
}
