/****************************************************************************
 * board/bk7258_t5ai/chip/cp/bk7258_saradc_server.c
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

int bk7258_saradc_server_initialize(void)
{
  return bk_adc_driver_init() == BK_OK ? 0 : -EIO;
}

#endif /* CONFIG_BK7258_SARADC_SERVER */
