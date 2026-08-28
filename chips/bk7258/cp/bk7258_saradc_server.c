/****************************************************************************
 * chips/bk7258/cp/bk7258_saradc_server.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Start the official v3.1.1.9 CP SARADC driver and its mailbox server for
 * the AP bk_adc_* client.  The SDK function is idempotent and owns the local
 * hardware, ISR, IPC channel and server task; this file only supplies the
 * board-level NuttX startup point.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SARADC_SERVER

#include <errno.h>

#include <arch/chip/bk7258_saradc_server.h>

#include <common/bk_err.h>
#include <driver/adc.h>
#include <driver/gpio.h>

int bk7258_saradc_server_initialize(void)
{
  /* Channel-map RPCs use the SDK GPIO HAL, but bk_adc_driver_init() does not
   * initialize it.  Establish the process-lifetime chip GPIO foundation
   * before starting the SARADC mailbox task; no selected-board GPIO device
   * is registered here and the SDK's incomplete deinit is never called.
   */

  if (bk_gpio_driver_init() != BK_OK)
    {
      return -EIO;
    }

  return bk_adc_driver_init() == BK_OK ? 0 : -EIO;
}

#endif /* CONFIG_BK7258_SARADC_SERVER */
