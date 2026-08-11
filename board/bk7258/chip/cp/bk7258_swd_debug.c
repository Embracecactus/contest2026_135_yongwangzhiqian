/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_swd_debug.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 group-1 SWD pinmux bridge for the optional RTT debug profile.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <driver/gpio.h>

#include <arch/chip/bk7258_debug.h>

/* v3.1.1.9 exports the GPIO device mapper from libdriver.a, while its public
 * wrapper header exposes the GPIO types but omits these two declarations.
 */

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev);
extern int32_t sys_drv_set_jtag_mode(uint32_t core_id);

int bk7258_swd_group1_initialize(void)
{
  bk_err_t ret;

  /* Match v3.1.1.9 bk_set_jtag_mode(): select physical CPU1/AP in the
   * system debug router before exposing JTAG group 1 on P0/P1.  This lets
   * J-Link halt the core that executes the H264 pipeline instead of CP.
   */

  ret = (bk_err_t)sys_drv_set_jtag_mode(1u);
  if (ret == BK_OK)
    {
      ret = gpio_dev_unmap(GPIO_0);
    }

  if (ret == BK_OK)
    {
      ret = gpio_dev_unmap(GPIO_1);
    }

  if (ret == BK_OK)
    {
      ret = gpio_dev_map(GPIO_0, GPIO_DEV_JTAG_TCK);
    }

  if (ret == BK_OK)
    {
      ret = gpio_dev_map(GPIO_1, GPIO_DEV_JTAG_TMS);
    }

  return ret == BK_OK ? 0 : -EIO;
}
