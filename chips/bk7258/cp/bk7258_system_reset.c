/****************************************************************************
 * chips/bk7258/cp/bk7258_system_reset.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 whole-device reset mechanism.  The CP writes the SDK reset-reason
 * latch before routing the AON watchdog to all cores.  A failed AON arm
 * falls back to the architectural reset instead of hanging forever.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <arch/chip/bk7258_system_reset.h>

#include <driver/aon_wdt.h>

/* These APIs are present in the manifest-pinned v3.1.1.9 driver archive but
 * are not exported by its reduced public header bundle.
 */

extern void bk_misc_set_reset_reason(uint32_t type);
extern void aon_pmu_drv_wdt_change_not_rosc_clk(void);
extern void aon_pmu_drv_wdt_rst_dev_enable(void);

static enum bk7258_reset_source_e
bk7258_system_reset_source(enum bk7258_reset_source_e source)
{
  switch (source)
    {
      case BK7258_RESET_SOURCE_REBOOT:
      case BK7258_RESET_SOURCE_WATCHDOG:
      case BK7258_RESET_SOURCE_NMI_WDT:
        return source;

      default:
        return BK7258_RESET_SOURCE_REBOOT;
    }
}

void bk7258_system_reset(enum bk7258_reset_source_e source)
{
  irqstate_t flags;

  source = bk7258_system_reset_source(source);
  flags = up_irq_save();
  (void)flags;

  /* reset_reason_init() copies this SDK latch to the retained R7A snapshot
   * on the next boot.  Set it before enabling the whole-device reset route
   * so planned OTA reboots cannot be mistaken for watchdog failures.
   */

  bk_misc_set_reset_reason((uint32_t)source);
  aon_pmu_drv_wdt_change_not_rosc_clk();
  aon_pmu_drv_wdt_rst_dev_enable();

  if (bk_aon_wdt_set_period(10u) != BK_OK)
    {
      up_systemreset();
    }

  for (;;)
    {
    }
}
