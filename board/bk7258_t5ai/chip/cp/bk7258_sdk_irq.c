/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/bk7258_sdk_irq.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPU0 Beken SDK-to-NuttX IRQ bridge for the BK7258.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stddef.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include <arch/barriers.h>
#include <arch/irq.h>

#include <driver/int.h>

#include "bk7258_sdk_irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SDK_IRQ_PRIORITY_MAX \
  ((1u << BK7258_SDK_IRQ_PRIORITY_BITS) - 1u)

#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
#  define BK7258_SDK_IRQ_GPIO_SECURE_SOURCE \
  ((unsigned int)INT_SRC_GPIO)
#  define BK7258_SDK_IRQ_GPIO_NONSECURE_SOURCE  37u
#  define BK7258_SDK_IRQ_GPIO_SOURCE_COUNT      2u
#  define BK7258_SDK_IRQ_GPIO_SECURE_INDEX      0u
#  define BK7258_SDK_IRQ_GPIO_NONSECURE_INDEX   1u
#endif

/****************************************************************************
 * Compile-time Invariants
 ****************************************************************************/

_Static_assert(BK7258_SDK_IRQ_COUNT == 64,
               "Stage B gate: SDK IRQ source count must be 64");
_Static_assert(INT_SRC_NONE == BK7258_EXTERNAL_IRQS,
               "Stage B gate: SDK INT_SRC_NONE must follow source 63");
_Static_assert(BK7258_SDK_IRQ_FIRST + BK7258_SDK_IRQ_COUNT == NR_IRQS,
               "Stage B gate: SDK source 0..63 must map to IRQ 16..79");
_Static_assert(BK7258_SDK_IRQ_PRIORITY_BITS == 3,
               "Stage B gate: STAR NVIC implements three priority bits");
_Static_assert(INT_SRC_LCD == 27,
               "Stage B gate: LCD priority exception must remain source 27");
#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
_Static_assert(INT_SRC_GPIO == 55,
               "GPIO IRQ test requires GPIO_S source 55");
_Static_assert(BK7258_SDK_IRQ_GPIO_NONSECURE_SOURCE == 37,
               "GPIO IRQ test requires GPIO_NS source 37");
#endif
_Static_assert(BK7258_SDK_IRQ_DEFAULT_PRIORITY <=
               BK7258_SDK_IRQ_PRIORITY_MAX,
               "Stage B gate: SDK default priority must be encodable");
_Static_assert(BK7258_SDK_IRQ_LCD_PRIORITY <=
               BK7258_SDK_IRQ_PRIORITY_MAX,
               "Stage B gate: SDK LCD priority must be encodable");

/****************************************************************************
 * Private Data
 ****************************************************************************/

static spinlock_t g_bk7258_sdk_irq_lock = SP_UNLOCKED;
static int_group_isr_t g_bk7258_sdk_irq_handlers[BK7258_SDK_IRQ_COUNT];
#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
static volatile uint32_t g_bk7258_sdk_irq_gpio_dispatch_counts[
  BK7258_SDK_IRQ_GPIO_SOURCE_COUNT];
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_sdk_source_to_irq(icu_int_src_t source)
{
  unsigned int index = (unsigned int)source;

  if (index >= BK7258_SDK_IRQ_COUNT)
    {
      return -1;
    }

  return BK7258_SDK_IRQ_FIRST + (int)index;
}

static int bk7258_sdk_irq_encode_priority(uint32_t priority)
{
  if (priority > BK7258_SDK_IRQ_PRIORITY_MAX)
    {
      return -1;
    }

#ifndef CONFIG_ARCH_HIPRI_INTERRUPT
  /* The common ARMv8-M dispatcher is not re-entrant in this configuration.
   * Keep SDK device IRQs at the same logical priority as SysTick so neither
   * can preempt the other while arm_doirq owns a TCB exception context.
   */

  priority = BK7258_SDK_IRQ_DEFAULT_PRIORITY;
#endif

  return (int)(priority << BK7258_SDK_IRQ_PRIORITY_SHIFT);
}

static uint32_t bk7258_sdk_irq_default_priority(icu_int_src_t source)
{
  if (source == INT_SRC_LCD)
    {
      return BK7258_SDK_IRQ_LCD_PRIORITY;
    }

  return BK7258_SDK_IRQ_DEFAULT_PRIORITY;
}

static int bk7258_sdk_irq_dispatch(int irq, void *context, void *arg)
{
  unsigned int source;
  int_group_isr_t handler;

  (void)context;
  (void)arg;

  source = (unsigned int)(irq - BK7258_SDK_IRQ_FIRST);
  if (source >= BK7258_SDK_IRQ_COUNT)
    {
      return OK;
    }

  handler = g_bk7258_sdk_irq_handlers[source];

#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
  if (source == BK7258_SDK_IRQ_GPIO_SECURE_SOURCE)
    {
      g_bk7258_sdk_irq_gpio_dispatch_counts[
        BK7258_SDK_IRQ_GPIO_SECURE_INDEX]++;
    }
  else if (source == BK7258_SDK_IRQ_GPIO_NONSECURE_SOURCE)
    {
      g_bk7258_sdk_irq_gpio_dispatch_counts[
        BK7258_SDK_IRQ_GPIO_NONSECURE_INDEX]++;
    }
#endif

  if (handler != NULL)
    {
      handler();
    }

  return OK;
}

static bk_err_t
bk7258_sdk_irq_unregister_locked(unsigned int index, int irq)
{
  int ret;

  up_disable_irq(irq);
  bk7258_clear_pending_irq(irq);
  g_bk7258_sdk_irq_handlers[index] = NULL;
  UP_DSB();
  UP_ISB();

  ret = irq_detach(irq);
  return ret < 0 ? BK_FAIL : BK_OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bk_err_t bk_int_isr_register(icu_int_src_t source,
                             int_group_isr_t handler, void *arg)
{
  unsigned int index = (unsigned int)source;
  irqstate_t flags;
  bk_err_t result;
  int priority;
  int irq;
  int ret;

  (void)arg; /* The CP CM33 SDK implementation also ignores this argument. */

  irq = bk7258_sdk_source_to_irq(source);
  if (irq < 0)
    {
      return BK_ERR_INT_DEVICE_NONE;
    }

  priority = bk7258_sdk_irq_encode_priority(
      bk7258_sdk_irq_default_priority(source));
  flags = spin_lock_irqsave(&g_bk7258_sdk_irq_lock);

  /* Registration replaces the previous owner of this source.  Keep the
   * line disabled and non-pending until both NuttX dispatch state and
   * priority are complete.
   */

  result = bk7258_sdk_irq_unregister_locked(index, irq);
  if (result != BK_OK)
    {
      goto out;
    }

  ret = up_prioritize_irq(irq, priority);
  if (ret < 0)
    {
      result = BK_FAIL;
      goto out;
    }

  /* A NULL SDK callback has the same net effect as the vendor CM33 path:
   * the previous callback is removed and the interrupt stays disabled.
   */

  if (handler == NULL)
    {
      result = BK_OK;
      goto out;
    }

  ret = irq_attach(irq, bk7258_sdk_irq_dispatch, NULL);
  if (ret < 0)
    {
      result = BK_FAIL;
      goto out;
    }

  g_bk7258_sdk_irq_handlers[index] = handler;
  UP_DSB();
  UP_ISB();
  up_enable_irq(irq);
  result = BK_OK;

out:
  spin_unlock_irqrestore(&g_bk7258_sdk_irq_lock, flags);
  return result;
}

bk_err_t bk_int_isr_unregister(icu_int_src_t source)
{
  unsigned int index = (unsigned int)source;
  irqstate_t flags;
  bk_err_t result;
  int irq;

  irq = bk7258_sdk_source_to_irq(source);
  if (irq < 0)
    {
      return BK_ERR_INT_DEVICE_NONE;
    }

  flags = spin_lock_irqsave(&g_bk7258_sdk_irq_lock);
  result = bk7258_sdk_irq_unregister_locked(index, irq);
  spin_unlock_irqrestore(&g_bk7258_sdk_irq_lock, flags);
  return result;
}

bk_err_t bk_int_set_priority(icu_int_src_t source, uint32_t priority)
{
  irqstate_t flags;
  bk_err_t result;
  int encoded;
  int irq;

  irq = bk7258_sdk_source_to_irq(source);
  if (irq < 0)
    {
      return BK_ERR_INT_DEVICE_NONE;
    }

  encoded = bk7258_sdk_irq_encode_priority(priority);
  if (encoded < 0)
    {
      return BK_ERR_NOT_SUPPORT;
    }

  flags = spin_lock_irqsave(&g_bk7258_sdk_irq_lock);
  result = up_prioritize_irq(irq, encoded) < 0 ? BK_FAIL : BK_OK;
  spin_unlock_irqrestore(&g_bk7258_sdk_irq_lock, flags);
  return result;
}

bk_err_t bk7258_sdk_irq_snapshot_handler(icu_int_src_t source,
                                         int_group_isr_t *handler)
{
  unsigned int index = (unsigned int)source;
  irqstate_t flags;

  if (handler == NULL)
    {
      return BK_FAIL;
    }

  if (bk7258_sdk_source_to_irq(source) < 0)
    {
      return BK_ERR_INT_DEVICE_NONE;
    }

  flags = spin_lock_irqsave(&g_bk7258_sdk_irq_lock);
  *handler = g_bk7258_sdk_irq_handlers[index];
  spin_unlock_irqrestore(&g_bk7258_sdk_irq_lock, flags);
  return BK_OK;
}

#if defined(CONFIG_BK7258_SDK_IRQ_TIMER_TEST) || \
    defined(CONFIG_BK7258_GPIO_IRQ_TEST)
bk_err_t bk7258_sdk_irq_test_snapshot_handler(icu_int_src_t source,
                                               int_group_isr_t *handler)
{
  return bk7258_sdk_irq_snapshot_handler(source, handler);
}
#endif

#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
void bk7258_sdk_irq_test_reset_dispatch_counts(void)
{
  irqstate_t flags;
  unsigned int index;

  flags = enter_critical_section();
  for (index = 0; index < BK7258_SDK_IRQ_GPIO_SOURCE_COUNT; index++)
    {
      g_bk7258_sdk_irq_gpio_dispatch_counts[index] = 0;
    }

  leave_critical_section(flags);
}

bk_err_t bk7258_sdk_irq_test_snapshot_dispatch_count(icu_int_src_t source,
                                                     uint32_t *count)
{
  unsigned int index = (unsigned int)source;
  irqstate_t flags;

  if (count == NULL)
    {
      return BK_FAIL;
    }

  if (index == BK7258_SDK_IRQ_GPIO_SECURE_SOURCE)
    {
      index = BK7258_SDK_IRQ_GPIO_SECURE_INDEX;
    }
  else if (index == BK7258_SDK_IRQ_GPIO_NONSECURE_SOURCE)
    {
      index = BK7258_SDK_IRQ_GPIO_NONSECURE_INDEX;
    }
  else
    {
      return BK_ERR_INT_DEVICE_NONE;
    }

  flags = enter_critical_section();
  *count = g_bk7258_sdk_irq_gpio_dispatch_counts[index];
  leave_critical_section(flags);
  return BK_OK;
}
#endif

void interrupt_init(void)
{
  /* NuttX up_irqinitialize() already owns VTOR, g_irqvector, and the NVIC. */
}

void interrupt_deinit(void)
{
  irqstate_t flags;
  unsigned int source;

  flags = spin_lock_irqsave(&g_bk7258_sdk_irq_lock);

  for (source = 0; source < BK7258_SDK_IRQ_COUNT; source++)
    {
      if (g_bk7258_sdk_irq_handlers[source] != NULL)
        {
          bk7258_sdk_irq_unregister_locked(
              source, BK7258_SDK_IRQ_FIRST + (int)source);
        }
    }

  spin_unlock_irqrestore(&g_bk7258_sdk_irq_lock, flags);
}
