/****************************************************************************
 * chips/bk7258/cp/bk7258_pm_soft_off.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Explicit whole-chip reboot-to-sleep. Never stop a live AP alone: the CP
 * Wi-Fi controller retains AP-owned buffers until a whole-chip reset.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bk7258_gpio.h>
#include <arch/chip/bk7258_pinmux.h>
#include <arch/chip/bk7258_pm.h>
#include <arch/chip/bk7258_ota.h>
#include <arch/chip/bk7258_reset_cause.h>
#include <arch/chip/bk7258_storage_guard.h>
#include <arch/chip/bk7258_system_reset.h>

#include <driver/gpio.h>

#include "arm_internal.h"
#include "nvic.h"

/* pm.h in the reduced archive bundle depends on an omitted sys_types.h.
 * Keep its pinned C enum ABI private, as the existing PM server does.
 */

#define BK7258_SDK_PM_SUPER_DEEP_SLEEP 3
extern int bk_pm_sleep_mode_set(int mode);
extern void bk_pm_enter_sleep(void);
extern void __real_arch_deep_sleep(void);

/* Exported by the pinned cp-aidk archives; reduced headers omit these. */

extern void pm_hardware_init(void);
extern void bk_misc_set_reset_reason(uint32_t type);

static struct work_s g_soft_off_work;
static bool g_soft_off_ready;
static bool g_soft_off_pending;

/* This is sampled only before the final WFI.  Its true path does not inspect
 * it again after wake, so retention of ordinary SRAM data is not a condition
 * for reaching the reset request.
 */

static volatile bool g_soft_off_wfi_armed;

#define BK7258_PM_SRAM_CODE \
  __attribute__((section(".itcm_sec_code"), noinline, used))

/*
 * The pinned SDK's arch_deep_sleep() is a WFI followed by "bx lr".  Its
 * super-deep caller is in XIP and leaves BASEPRI raised, which is unsuitable
 * for the configured AON wake source and can fetch from XIP after wake.
 *
 * GNU ld redirects the SDK's unresolved arch_deep_sleep reference here only
 * when CONFIG_BK7258_PM_SOFT_OFF selects --wrap.  Normal low-voltage callers
 * keep the SDK behavior through __real_arch_deep_sleep().  The armed path is
 * copied with its literal pool to SRAM and never returns to that SDK caller.
 * This only removes the known WFI mask/XIP-return hazard.  SYSRESETREQ scope,
 * clock restoration, and actual K2 power-off behavior still require board
 * evidence; do not treat this as proof of whole-chip power removal.
 */

BK7258_PM_SRAM_CODE __attribute__((noreturn))
void bk7258_pm_soft_off_wfi_reset(void)
{
  volatile uint32_t *aircr = (volatile uint32_t *)(uintptr_t)NVIC_AIRCR;
  uint32_t regval;

  /* Match NuttX up_systemreset(), but keep the complete post-WFI sequence in
   * SRAM.  No existing reset helper is SRAM-resident in this image.
   */

  regval = *aircr & NVIC_AIRCR_PRIGROUP_MASK;
  *aircr = regval | NVIC_AIRCR_VECTKEY | NVIC_AIRCR_SYSRESETREQ;
  __asm volatile ("dsb sy" ::: "memory");

  /* A rejected reset request must not fall through into XIP either. */

  for (;;)
    {
    }
}

BK7258_PM_SRAM_CODE void __wrap_arch_deep_sleep(void)
{
  if (!__atomic_load_n(&g_soft_off_wfi_armed, __ATOMIC_ACQUIRE))
    {
      __real_arch_deep_sleep();
      return;
    }

  /* The SDK's sys_drv_enter_deep_sleep() uses BASEPRI critical sections.
   * Translate only at its final WFI boundary: PRIMASK preserves the critical
   * section while BASEPRI=0 lets the configured AON wake interrupt release
   * WFI, matching the existing coordinated low-voltage wrapper.
   */

  setprimask(1);
  setbasepri(0);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  __asm volatile ("wfi" ::: "memory");
  __asm volatile ("isb sy" ::: "memory");
  bk7258_pm_soft_off_wfi_reset();
}

static void bk7258_pm_soft_off_worker(void *arg)
{
  int ret;

  (void)arg;
  sync();

  /* Serialize reset with every CP Flash transaction, including OTA and radio
   * persistence. The AP caller has already drained and synced its owners.
   * Hold this read-only guard until reset, so no new Flash mutation can race.
   */

  ret = bk7258_storage_guard_lock(BK7258_STORAGE_GUARD_DATA, false, 1000);
  if (ret < 0)
    {
      syslog(LOG_ERR, "soft-off: storage busy %d; reset canceled\n", ret);
      __atomic_store_n(&g_soft_off_pending, false, __ATOMIC_RELEASE);
      return;
    }

  bk7258_system_reset(BK7258_RESET_SOURCE_FORCE_DEEPSLEEP);
}

int bk7258_pm_soft_off_status(void)
{
  return __atomic_load_n(&g_soft_off_pending, __ATOMIC_ACQUIRE) ? 1 : 0;
}

int bk7258_pm_soft_off_request(void)
{
  struct bk7258_ota_pair_snapshot_s pair;
  bool expected = false;
  int ret;

  if (!g_soft_off_ready)
    {
      return -ENOTSUP;
    }

  /* Do not turn a trial-image power request into an unintended rollback. */

  ret = bk7258_ota_get_active_pair(&pair);
  if (ret < 0 || pair.state != BK7258_OTA_PAIR_CONFIRMED)
    {
      return -EBUSY;
    }

  if (!__atomic_compare_exchange_n(&g_soft_off_pending, &expected, true,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return OK;
    }

  /* Reply/replay has time to finish before reset; never block the RPMsg RX
   * worker with filesystem work. Duplicate requests do not schedule resets.
   */

  ret = work_queue(LPWORK, &g_soft_off_work, bk7258_pm_soft_off_worker,
                   NULL, MSEC2TICK(4000));
  if (ret < 0)
    {
      __atomic_store_n(&g_soft_off_pending, false, __ATOMIC_RELEASE);
    }

  return ret;
}

void bk7258_pm_soft_off_boot(const struct bk7258_gpio_config_s *config)
{
  struct bk7258_reset_cause_raw_s reason;
  bool high;
  int ret;

  g_soft_off_ready = config != NULL && config->power_button_enabled &&
                     config->power_button_gpio <= 15;
  if (bk7258_reset_cause_read(&reason) < 0 ||
      reason.source != BK7258_RESET_SOURCE_FORCE_DEEPSLEEP)
    {
      return;
    }

  /* Consume the intent before any fallible setup. A failed entry must boot
   * normally, and a later analog-key wake must not repeat the off request.
   */

  bk_misc_set_reset_reason(BK7258_RESET_SOURCE_REBOOT);
  if (!g_soft_off_ready)
    {
      return;
    }

  ret = bk7258_gpio_configure_input(config->power_button_gpio,
    config->power_button_active_low ? BK7258_GPIO_PULL_UP :
                                      BK7258_GPIO_PULL_DOWN);
  if (ret < 0 ||
      bk7258_gpio_read_input(config->power_button_gpio, &high) < 0 ||
      high != config->power_button_active_low)
    {
      syslog(LOG_WARNING, "soft-off: wake key active/setup failed; booting\n");
      return;
    }

  /* This hook runs before radio/AP/SD startup. Use the SDK's complete sleep
   * setup only in this one-shot boot path; ordinary NuttX idle stays owned by
   * the existing PM policy. GPIO16 (SD D0) is changed by the analog callback,
   * so this must never be called from a running product-key callback.
   */

  pm_hardware_init();
  ret = bk_gpio_ana_register_wakeup_source(config->power_button_gpio,
    config->power_button_active_low ? GPIO_INT_TYPE_LOW_LEVEL :
                                      GPIO_INT_TYPE_HIGH_LEVEL);
  if (ret == BK_OK)
    {
      ret = bk_pm_sleep_mode_set(BK7258_SDK_PM_SUPER_DEEP_SLEEP);
    }

  if (ret == BK_OK)
    {
      syslog(LOG_INFO, "soft-off: entering super-deep; wake gpio=%u\n",
             config->power_button_gpio);
      up_mdelay(20);
      __atomic_store_n(&g_soft_off_wfi_armed, true, __ATOMIC_RELEASE);
      bk_pm_enter_sleep();
    }

  /* SDK entry may abort on a pending interrupt or a power-domain vote.
   * GPIO/clock setup may already have changed: cold boot, never resume the
   * partially quiesced system and never leave the sleep-intent latch set.
   */

  bk7258_system_reset(BK7258_RESET_SOURCE_REBOOT);
}
