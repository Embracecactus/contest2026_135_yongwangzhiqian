/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/
 * bk7258_gpio_irq_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Manual P29 edge-interrupt test through the Beken SDK CPU0 IRQ bridge.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <nuttx/spinlock.h>

#include <driver/gpio.h>
#include <driver/int.h>
#include <driver/int_types.h>

#include "arm_internal.h"
#include "nvic.h"
#include "bk7258_sdk_irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_GPIOIRQ_KEY                   GPIO_29
#define BK7258_GPIOIRQ_SECURE_SOURCE         INT_SRC_GPIO
#define BK7258_GPIOIRQ_NONSECURE_SOURCE      ((icu_int_src_t)37)
#define BK7258_GPIOIRQ_SECURE_IRQ            \
  (BK7258_SDK_IRQ_FIRST + BK7258_GPIOIRQ_SECURE_SOURCE)
#define BK7258_GPIOIRQ_NONSECURE_IRQ         \
  (BK7258_SDK_IRQ_FIRST + BK7258_GPIOIRQ_NONSECURE_SOURCE)
#define BK7258_GPIOIRQ_WAIT_STEP_US          10000u
#define BK7258_GPIOIRQ_WAIT_STEPS            1000u
#define BK7258_GPIOIRQ_SETTLE_STEPS          50u
#define BK7258_GPIOIRQ_ENABLE_BIT            (1u << 12)
#define BK7258_GPIOIRQ_ROUTE_REG              0x44010080u
#define BK7258_GPIOIRQ_SECURE_ROUTE_BIT       (1u << 23)
#define BK7258_GPIOIRQ_NONSECURE_ROUTE_BIT    (1u << 5)
#define BK7258_GPIOIRQ_ROUTE_MASK             \
  (BK7258_GPIOIRQ_SECURE_ROUTE_BIT |         \
   BK7258_GPIOIRQ_NONSECURE_ROUTE_BIT)

#define BK7258_GPIOIRQ_RESERVED(pin) \
  ((pin) == GPIO_0 || (pin) == GPIO_1 || \
   (pin) == GPIO_10 || (pin) == GPIO_11)

/****************************************************************************
 * Compile-time Invariants
 ****************************************************************************/

_Static_assert(GPIO_29 == 29,
               "GPIO IRQ test requires USERKEY to remain on P29");
_Static_assert(GPIO_NUM == 56,
               "GPIO IRQ test requires the pinned 56-channel SDK layout");
_Static_assert(!BK7258_GPIOIRQ_RESERVED(BK7258_GPIOIRQ_KEY),
               "GPIO IRQ test must not use a console/boot UART pin");
_Static_assert(INT_SRC_GPIO == 55,
               "GPIO IRQ test requires SDK GPIO_S source 55");
_Static_assert(BK7258_GPIOIRQ_SECURE_IRQ == 71,
               "GPIO IRQ test requires GPIO_S NuttX IRQ 71");
_Static_assert(BK7258_GPIOIRQ_NONSECURE_SOURCE == 37,
               "GPIO IRQ test requires GPIO_NS source 37");
_Static_assert(BK7258_GPIOIRQ_NONSECURE_IRQ == 53,
               "GPIO IRQ test requires GPIO_NS NuttX IRQ 53");

/****************************************************************************
 * Private Data
 ****************************************************************************/

struct bk7258_gpioirq_nvic_diag_s
{
  bool enabled;
  bool pending;
  bool active;
  uint32_t dispatch_count;
};

struct bk7258_gpioirq_timeout_diag_s
{
  uint32_t control;
  uint32_t route;
  uint32_t pending_low;
  uint32_t pending_high;
  struct bk7258_gpioirq_nvic_diag_s secure;
  struct bk7258_gpioirq_nvic_diag_s nonsecure;
  uint32_t callback_count;
  gpio_id_t last_id;
  bool raw;
};

static bool g_bk7258_gpioirq_running;
static volatile uint32_t g_bk7258_gpioirq_count;
static volatile gpio_id_t g_bk7258_gpioirq_last_id;

static const gpio_config_t g_bk7258_gpioirq_key_config =
{
  .io_mode = GPIO_INPUT_ENABLE,
  .pull_mode = GPIO_PULL_UP_EN,
  .func_mode = GPIO_SECOND_FUNC_DISABLE,
};

/* The pinned SDK exports this helper from gpio_driver_base.c but does not
 * declare it in the public GPIO header. It snapshots the raw interrupt
 * status registers as (high[55:32], low[31:0]).
 */

extern void gpio_get_interrupt_status(uint32_t *high, uint32_t *low);
extern int sys_drv_int_group2_enable(uint32_t param);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_gpioirq_claim(void)
{
  irqstate_t flags;
  bool claimed;

  flags = enter_critical_section();
  claimed = !g_bk7258_gpioirq_running;
  if (claimed)
    {
      g_bk7258_gpioirq_running = true;
    }

  leave_critical_section(flags);
  return claimed;
}

static void bk7258_gpioirq_release(void)
{
  irqstate_t flags;

  flags = enter_critical_section();
  g_bk7258_gpioirq_running = false;
  leave_critical_section(flags);
}

static uint32_t bk7258_gpioirq_enable_route(uint32_t *saved)
{
  volatile uint32_t *reg =
    (volatile uint32_t *)(uintptr_t)BK7258_GPIOIRQ_ROUTE_REG;
  irqstate_t flags;
  uint32_t value;

  flags = enter_critical_section();
  *saved = *reg;
  *reg = *saved | BK7258_GPIOIRQ_ROUTE_MASK;
  value = *reg;
  leave_critical_section(flags);
  return value;
}

static uint32_t bk7258_gpioirq_restore_route(uint32_t saved)
{
  volatile uint32_t *reg =
    (volatile uint32_t *)(uintptr_t)BK7258_GPIOIRQ_ROUTE_REG;
  irqstate_t flags;
  uint32_t value;

  flags = enter_critical_section();
  value = *reg;
  value &= ~BK7258_GPIOIRQ_ROUTE_MASK;
  value |= saved & BK7258_GPIOIRQ_ROUTE_MASK;
  *reg = value;
  value = *reg;
  leave_critical_section(flags);
  return value;
}

static int bk7258_gpioirq_result(const char *operation, bk_err_t error)
{
  if (error == BK_OK)
    {
      return 0;
    }

  if (error == BK_ERR_GPIO_INTERNAL_USED)
    {
      printf("bkgpioirq: FAIL %s P29 is internally owned (%d)\n",
             operation, (int)error);
      return -EBUSY;
    }

  printf("bkgpioirq: FAIL %s P29 error=%d\n", operation, (int)error);
  return -EIO;
}

static void bk7258_gpioirq_snapshot_nvic_diag(
  int irq, icu_int_src_t source, FAR struct bk7258_gpioirq_nvic_diag_s *diag)
{
  unsigned int external = (unsigned int)(irq - BK7258_SDK_IRQ_FIRST);
  uint32_t bit = 1u << (external & 0x1f);
  uint32_t count = 0;
  irqstate_t flags;

  flags = enter_critical_section();
  diag->enabled = (getreg32(NVIC_IRQ_ENABLE(external)) & bit) != 0;
  diag->pending = (getreg32(NVIC_IRQ_PEND(external)) & bit) != 0;
  diag->active = (getreg32(NVIC_IRQ_ACTIVE(external)) & bit) != 0;
  if (bk7258_sdk_irq_test_snapshot_dispatch_count(source, &count) == BK_OK)
    {
      diag->dispatch_count = count;
    }
  else
    {
      diag->dispatch_count = 0;
    }

  leave_critical_section(flags);
}

static void bk7258_gpioirq_snapshot_timeout_diag(
  FAR struct bk7258_gpioirq_timeout_diag_s *diag)
{
  irqstate_t flags;

  diag->control = bk_gpio_get_value(BK7258_GPIOIRQ_KEY);
  diag->route = getreg32(BK7258_GPIOIRQ_ROUTE_REG);
  diag->raw = bk_gpio_get_input(BK7258_GPIOIRQ_KEY);
  gpio_get_interrupt_status(&diag->pending_high, &diag->pending_low);
  bk7258_gpioirq_snapshot_nvic_diag(BK7258_GPIOIRQ_SECURE_IRQ,
                                    BK7258_GPIOIRQ_SECURE_SOURCE,
                                    &diag->secure);
  bk7258_gpioirq_snapshot_nvic_diag(BK7258_GPIOIRQ_NONSECURE_IRQ,
                                    BK7258_GPIOIRQ_NONSECURE_SOURCE,
                                    &diag->nonsecure);
  flags = enter_critical_section();
  diag->callback_count = g_bk7258_gpioirq_count;
  diag->last_id = g_bk7258_gpioirq_last_id;
  leave_critical_section(flags);
}

static void bk7258_gpioirq_report_timeout(const char *edge)
{
  struct bk7258_gpioirq_timeout_diag_s diag;

  bk7258_gpioirq_snapshot_timeout_diag(&diag);
  printf("bkgpioirq: timeout %s p29 raw=%u ctrl=0x%08lx ien=%u "
         "route=0x%08lx pendlo=0x%08lx pendhi=0x%08lx p29pend=%u\n",
         edge, diag.raw ? 1u : 0u, (unsigned long)diag.control,
         (diag.control & BK7258_GPIOIRQ_ENABLE_BIT) != 0 ? 1u : 0u,
         (unsigned long)diag.route, (unsigned long)diag.pending_low,
         (unsigned long)diag.pending_high,
         (diag.pending_low >> BK7258_GPIOIRQ_KEY) & 1u);
  printf("bkgpioirq: timeout %s irq71 en=%u pend=%u act=%u disp=%lu "
         "irq53 en=%u pend=%u act=%u disp=%lu cb=%lu id=%u\n",
         edge,
         diag.secure.enabled ? 1u : 0u,
         diag.secure.pending ? 1u : 0u,
         diag.secure.active ? 1u : 0u,
         (unsigned long)diag.secure.dispatch_count,
         diag.nonsecure.enabled ? 1u : 0u,
         diag.nonsecure.pending ? 1u : 0u,
         diag.nonsecure.active ? 1u : 0u,
         (unsigned long)diag.nonsecure.dispatch_count,
         (unsigned long)diag.callback_count,
         (unsigned int)diag.last_id);
}

static bool bk7258_gpioirq_wait_level(bool expected, unsigned int steps)
{
  unsigned int step;

  for (step = 0; step < steps; step++)
    {
      if (bk_gpio_get_input(BK7258_GPIOIRQ_KEY) == expected)
        {
          return true;
        }

      usleep(BK7258_GPIOIRQ_WAIT_STEP_US);
    }

  return false;
}

static bool bk7258_gpioirq_wait_callback(void)
{
  unsigned int step;

  for (step = 0; step < BK7258_GPIOIRQ_WAIT_STEPS; step++)
    {
      if (g_bk7258_gpioirq_count != 0)
        {
          return true;
        }

      usleep(BK7258_GPIOIRQ_WAIT_STEP_US);
    }

  return false;
}

static void bk7258_gpioirq_callback(gpio_id_t gpio_id)
{
  if (gpio_id == BK7258_GPIOIRQ_KEY)
    {
      g_bk7258_gpioirq_last_id = gpio_id;
      g_bk7258_gpioirq_count++;
    }
}

static int bk7258_gpioirq_run_edge(gpio_int_type_t type,
                                   bool expected_level,
                                   const char *edge,
                                   const char *action,
                                   bool *interrupt_enabled)
{
  uint32_t count;
  gpio_id_t last_id;
  bk_err_t error;
  int result;

  error = bk_gpio_set_interrupt_type(BK7258_GPIOIRQ_KEY, type);
  result = bk7258_gpioirq_result("set edge", error);
  if (result < 0)
    {
      return result;
    }

  g_bk7258_gpioirq_count = 0;
  g_bk7258_gpioirq_last_id = GPIO_NUM;
  bk7258_sdk_irq_test_reset_dispatch_counts();

  error = bk_gpio_clear_interrupt(BK7258_GPIOIRQ_KEY);
  result = bk7258_gpioirq_result("clear pending", error);
  if (result < 0)
    {
      return result;
    }

  error = bk_gpio_enable_interrupt(BK7258_GPIOIRQ_KEY);
  result = bk7258_gpioirq_result("enable interrupt", error);
  if (result < 0)
    {
      return result;
    }

  *interrupt_enabled = true;
  printf("bkgpioirq: %s USERKEY and hold until detected\n", action);

  if (!bk7258_gpioirq_wait_callback())
    {
      printf("bkgpioirq: FAIL %s callback timeout\n", edge);
      bk7258_gpioirq_report_timeout(edge);
      return -ETIMEDOUT;
    }

  error = bk_gpio_disable_interrupt(BK7258_GPIOIRQ_KEY);
  result = bk7258_gpioirq_result("disable interrupt", error);
  if (result < 0)
    {
      return result;
    }

  *interrupt_enabled = false;

  if (!bk7258_gpioirq_wait_level(expected_level,
                                  BK7258_GPIOIRQ_SETTLE_STEPS))
    {
      printf("bkgpioirq: FAIL %s unstable raw=%u\n", edge,
             bk_gpio_get_input(BK7258_GPIOIRQ_KEY) ? 1u : 0u);
      return -EIO;
    }

  count = g_bk7258_gpioirq_count;
  last_id = g_bk7258_gpioirq_last_id;
  if (last_id != BK7258_GPIOIRQ_KEY)
    {
      printf("bkgpioirq: FAIL %s callback id=%u\n", edge,
             (unsigned int)last_id);
      return -EIO;
    }

  printf("bkgpioirq: %s count=%u id=%u raw=%u\n",
         edge, (unsigned int)count, (unsigned int)last_id,
         expected_level ? 1u : 0u);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_gpio_irq_test(void)
{
  int_group_isr_t gpio_handler = NULL;
  int_group_isr_t saved_nonsecure_handler = NULL;
  uint32_t saved_key = 0;
  uint32_t saved_route = 0;
  uint32_t route_value;
  bool pin_configured = false;
  bool handler_registered = false;
  bool interrupt_enabled = false;
  bool nonsecure_route_touched = false;
  bool route_gate_touched = false;
  bk_err_t error;
  int cleanup_result;
  int result = -EIO;

  if (!bk7258_gpioirq_claim())
    {
      printf("bkgpioirq: FAIL test already running\n");
      return -EBUSY;
    }

  printf("bkgpioirq: BEGIN key=P29 gpio_s=%u/irq%u gpio_ns=%u/irq%u\n",
         (unsigned int)BK7258_GPIOIRQ_SECURE_SOURCE,
         (unsigned int)BK7258_GPIOIRQ_SECURE_IRQ,
         (unsigned int)BK7258_GPIOIRQ_NONSECURE_SOURCE,
         (unsigned int)BK7258_GPIOIRQ_NONSECURE_IRQ);

  error = bk_gpio_driver_init();
  result = bk7258_gpioirq_result("driver init", error);
  if (result < 0)
    {
      goto out;
    }

  /* Enable GPIO interrupt forwarding to CPU0 at the system level.
   * The SDK only calls this from the low-power entry path, never from normal
   * GPIO init.  Without it, the GPIO pending bit stays set at the peripheral
   * but never reaches the NVIC.
   */

  sys_drv_int_group2_enable((1u << 23) | (1u << 5));

  error = bk7258_sdk_irq_test_snapshot_handler(
            BK7258_GPIOIRQ_SECURE_SOURCE, &gpio_handler);
  if (error != BK_OK || gpio_handler == NULL)
    {
      printf("bkgpioirq: FAIL snapshot GPIO_S ret=%d handler=0x%08lx\n",
             (int)error, (unsigned long)(uintptr_t)gpio_handler);
      result = -EIO;
      goto out;
    }

  error = bk7258_sdk_irq_test_snapshot_handler(
            BK7258_GPIOIRQ_NONSECURE_SOURCE,
            &saved_nonsecure_handler);
  if (error != BK_OK)
    {
      printf("bkgpioirq: FAIL snapshot GPIO_NS ret=%d\n", (int)error);
      result = -EIO;
      goto out;
    }

  error = bk_int_isr_register(BK7258_GPIOIRQ_NONSECURE_SOURCE,
                              gpio_handler, NULL);
  if (error != BK_OK)
    {
      printf("bkgpioirq: FAIL mirror GPIO_NS ret=%d\n", (int)error);
      result = -EIO;
      goto out;
    }

  nonsecure_route_touched = true;
  printf("bkgpioirq: mirror GPIO_S handler to GPIO_NS source=%u irq=%u\n",
         (unsigned int)BK7258_GPIOIRQ_NONSECURE_SOURCE,
         (unsigned int)BK7258_GPIOIRQ_NONSECURE_IRQ);

  route_value = bk7258_gpioirq_enable_route(&saved_route);
  route_gate_touched = true;
  printf("bkgpioirq: route 0x%08lx -> 0x%08lx mask=0x%08lx\n",
         (unsigned long)saved_route, (unsigned long)route_value,
         (unsigned long)BK7258_GPIOIRQ_ROUTE_MASK);
  if ((route_value & BK7258_GPIOIRQ_ROUTE_MASK) !=
      BK7258_GPIOIRQ_ROUTE_MASK)
    {
      printf("bkgpioirq: FAIL route gate readback\n");
      result = -EIO;
      goto out;
    }

  saved_key = bk_gpio_get_value(BK7258_GPIOIRQ_KEY);
  if ((saved_key & BK7258_GPIOIRQ_ENABLE_BIT) != 0)
    {
      printf("bkgpioirq: FAIL P29 already has interrupt enabled\n");
      result = -EBUSY;
      goto out;
    }

  error = bk_gpio_set_config(BK7258_GPIOIRQ_KEY,
                             &g_bk7258_gpioirq_key_config);
  result = bk7258_gpioirq_result("configure input pull-up", error);
  if (result < 0)
    {
      goto out;
    }

  pin_configured = true;

  error = bk_gpio_disable_interrupt(BK7258_GPIOIRQ_KEY);
  result = bk7258_gpioirq_result("disable interrupt", error);
  if (result < 0)
    {
      goto out;
    }

  error = bk_gpio_clear_interrupt(BK7258_GPIOIRQ_KEY);
  result = bk7258_gpioirq_result("clear pending", error);
  if (result < 0)
    {
      goto out;
    }

  error = bk_gpio_register_isr(BK7258_GPIOIRQ_KEY,
                               bk7258_gpioirq_callback);
  result = bk7258_gpioirq_result("register callback", error);
  if (result < 0)
    {
      goto out;
    }

  handler_registered = true;

  printf("bkgpioirq: release USERKEY before falling-edge test\n");
  if (!bk7258_gpioirq_wait_level(true, BK7258_GPIOIRQ_WAIT_STEPS))
    {
      printf("bkgpioirq: FAIL release timeout raw=0\n");
      result = -ETIMEDOUT;
      goto out;
    }

  result = bk7258_gpioirq_run_edge(GPIO_INT_TYPE_FALLING_EDGE, false,
                                   "FALL", "PRESS", &interrupt_enabled);
  if (result < 0)
    {
      goto out;
    }

  result = bk7258_gpioirq_run_edge(GPIO_INT_TYPE_RISING_EDGE, true,
                                   "RISE", "RELEASE", &interrupt_enabled);
  if (result < 0)
    {
      goto out;
    }

  result = 0;

out:
  if (interrupt_enabled || pin_configured)
    {
      error = bk_gpio_disable_interrupt(BK7258_GPIOIRQ_KEY);
      cleanup_result = bk7258_gpioirq_result("cleanup disable", error);
      if (result == 0 && cleanup_result < 0)
        {
          result = cleanup_result;
        }
    }

  if (pin_configured)
    {
      error = bk_gpio_clear_interrupt(BK7258_GPIOIRQ_KEY);
      cleanup_result = bk7258_gpioirq_result("cleanup clear", error);
      if (result == 0 && cleanup_result < 0)
        {
          result = cleanup_result;
        }
    }

  if (handler_registered)
    {
      /* The pinned archive declares no bk_gpio_unregister_isr symbol.
       * Its bk_gpio_register_isr implementation stores the callback pointer
       * directly, so registering NULL is the exact per-pin unregister path.
       */

      error = bk_gpio_register_isr(BK7258_GPIOIRQ_KEY, NULL);
      cleanup_result = bk7258_gpioirq_result("clear callback", error);
      if (result == 0 && cleanup_result < 0)
        {
          result = cleanup_result;
        }
    }

  if (route_gate_touched)
    {
      route_value = bk7258_gpioirq_restore_route(saved_route);
      printf("bkgpioirq: route restore=0x%08lx\n",
             (unsigned long)route_value);
      if ((route_value & BK7258_GPIOIRQ_ROUTE_MASK) !=
          (saved_route & BK7258_GPIOIRQ_ROUTE_MASK))
        {
          printf("bkgpioirq: FAIL route restore readback\n");
          if (result == 0)
            {
              result = -EIO;
            }
        }
    }

  if (nonsecure_route_touched)
    {
      if (saved_nonsecure_handler != NULL)
        {
          error = bk_int_isr_register(BK7258_GPIOIRQ_NONSECURE_SOURCE,
                                      saved_nonsecure_handler, NULL);
        }
      else
        {
          error = bk_int_isr_unregister(BK7258_GPIOIRQ_NONSECURE_SOURCE);
        }

      if (error != BK_OK)
        {
          printf("bkgpioirq: FAIL restore GPIO_NS route ret=%d\n",
                 (int)error);
          if (result == 0)
            {
              result = -EIO;
            }
        }
    }

  if (pin_configured)
    {
      error = bk_gpio_set_value(BK7258_GPIOIRQ_KEY, saved_key);
      cleanup_result = bk7258_gpioirq_result("restore", error);
      if (result == 0 && cleanup_result < 0)
        {
          result = cleanup_result;
        }
    }

  if (result == 0)
    {
      printf("bkgpioirq: restore OK\n");
      printf("bkgpioirq: PASS\n");
    }
  else
    {
      printf("bkgpioirq: FAIL result=%d\n", result);
    }

  bk7258_gpioirq_release();
  return result;
}
