/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/
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

#include <arch/chip/bk7258_gpio.h>

#include <driver/gpio.h>
#include <driver/int.h>
#include <driver/int_types.h>

#include "arm_internal.h"
#include "nvic.h"
#include "bk7258_sdk_irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_GPIOIRQ_SECURE_SOURCE         INT_SRC_GPIO
#define BK7258_GPIOIRQ_SECURE_IRQ            \
  (BK7258_SDK_IRQ_FIRST + BK7258_GPIOIRQ_SECURE_SOURCE)
#define BK7258_GPIOIRQ_WAIT_STEP_US          10000u
#define BK7258_GPIOIRQ_WAIT_STEPS            1000u
#define BK7258_GPIOIRQ_SETTLE_STEPS          50u
#define BK7258_GPIOIRQ_ENABLE_BIT            (1u << 12)
#define BK7258_GPIOIRQ_ROUTE_REG              0x44010084u
#define BK7258_GPIOIRQ_SECURE_ROUTE_BIT       (1u << 23)
#define BK7258_GPIOIRQ_ROUTE_MASK             BK7258_GPIOIRQ_SECURE_ROUTE_BIT
#define BK7258_GPIOIRQ_RESERVED(pin) \
  ((pin) == GPIO_0 || (pin) == GPIO_1 || \
   (pin) == GPIO_10 || (pin) == GPIO_11)

/****************************************************************************
 * Compile-time Invariants
 ****************************************************************************/

_Static_assert(GPIO_NUM == 56,
               "GPIO IRQ test requires the pinned 56-channel SDK layout");
_Static_assert(INT_SRC_GPIO == 55,
               "GPIO IRQ test requires SDK GPIO_S source 55");
_Static_assert(BK7258_GPIOIRQ_SECURE_IRQ == 71,
               "GPIO IRQ test requires GPIO_S NuttX IRQ 71");

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
  uint32_t callback_count;
  gpio_id_t last_id;
  bool raw;
};

static bool g_bk7258_gpioirq_running;
static FAR const struct bk7258_gpio_config_s *g_bk7258_gpioirq_config;
static gpio_id_t g_bk7258_gpioirq_key;
static gpio_int_type_t g_bk7258_gpioirq_press_edge;
static gpio_int_type_t g_bk7258_gpioirq_release_edge;
static const char *g_bk7258_gpioirq_press_edge_name;
static const char *g_bk7258_gpioirq_release_edge_name;
static bool g_bk7258_gpioirq_pressed_level;
static bool g_bk7258_gpioirq_released_level;
static volatile uint32_t g_bk7258_gpioirq_count;
static volatile gpio_id_t g_bk7258_gpioirq_last_id;

static gpio_config_t g_bk7258_gpioirq_key_config =
{
  .io_mode = GPIO_INPUT_ENABLE,
  .pull_mode = GPIO_PULL_DISABLE,
  .func_mode = GPIO_SECOND_FUNC_DISABLE,
};

#define BK7258_GPIOIRQ_KEY                (g_bk7258_gpioirq_key)
#define BK7258_GPIOIRQ_PRESS_EDGE         (g_bk7258_gpioirq_press_edge)
#define BK7258_GPIOIRQ_RELEASE_EDGE       (g_bk7258_gpioirq_release_edge)
#define BK7258_GPIOIRQ_PRESS_EDGE_NAME    (g_bk7258_gpioirq_press_edge_name)
#define BK7258_GPIOIRQ_RELEASE_EDGE_NAME  (g_bk7258_gpioirq_release_edge_name)
#define BK7258_GPIOIRQ_PRESSED_LEVEL      (g_bk7258_gpioirq_pressed_level)
#define BK7258_GPIOIRQ_RELEASED_LEVEL     (g_bk7258_gpioirq_released_level)

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
      printf("bkgpioirq: FAIL %s P%u is internally owned (%d)\n",
             operation, (unsigned int)BK7258_GPIOIRQ_KEY, (int)error);
      return -EBUSY;
    }

  printf("bkgpioirq: FAIL %s P%u error=%d\n", operation,
         (unsigned int)BK7258_GPIOIRQ_KEY, (int)error);
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
  flags = enter_critical_section();
  diag->callback_count = g_bk7258_gpioirq_count;
  diag->last_id = g_bk7258_gpioirq_last_id;
  leave_critical_section(flags);
}

static void bk7258_gpioirq_report_timeout(const char *edge)
{
  struct bk7258_gpioirq_timeout_diag_s diag;

  bk7258_gpioirq_snapshot_timeout_diag(&diag);
  printf("bkgpioirq: timeout %s p%u raw=%u ctrl=0x%08lx ien=%u "
         "route=0x%08lx pendlo=0x%08lx pendhi=0x%08lx pinpend=%u\n",
         edge, (unsigned int)BK7258_GPIOIRQ_KEY, diag.raw ? 1u : 0u,
         (unsigned long)diag.control,
         (diag.control & BK7258_GPIOIRQ_ENABLE_BIT) != 0 ? 1u : 0u,
         (unsigned long)diag.route, (unsigned long)diag.pending_low,
         (unsigned long)diag.pending_high,
         (diag.pending_low >> BK7258_GPIOIRQ_KEY) & 1u);
  printf("bkgpioirq: timeout %s irq71 en=%u pend=%u act=%u disp=%lu "
         "cb=%lu id=%u\n",
         edge,
         diag.secure.enabled ? 1u : 0u,
         diag.secure.pending ? 1u : 0u,
         diag.secure.active ? 1u : 0u,
         (unsigned long)diag.secure.dispatch_count,
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

int bk7258_gpio_irq_test(
  FAR const struct bk7258_gpio_config_s *config)
{
  int_group_isr_t gpio_handler = NULL;
  uint32_t saved_key = 0;
  uint32_t saved_route = 0;
  uint32_t route_value;
  bool pin_configured = false;
  bool handler_registered = false;
  bool interrupt_enabled = false;
  bool route_gate_touched = false;
  bk_err_t error;
  int cleanup_result;
  int result = -EIO;

  if (!bk7258_gpioirq_claim())
    {
      printf("bkgpioirq: FAIL test already running\n");
      return -EBUSY;
    }

  g_bk7258_gpioirq_config = config;
  if (g_bk7258_gpioirq_config == NULL ||
      g_bk7258_gpioirq_config->name == NULL ||
      g_bk7258_gpioirq_config->user_button_gpio >= GPIO_NUM ||
      BK7258_GPIOIRQ_RESERVED(
        g_bk7258_gpioirq_config->user_button_gpio))
    {
      printf("bkgpioirq: FAIL no valid GPIO board configuration\n");
      bk7258_gpioirq_release();
      return -ENODEV;
    }

  BK7258_GPIOIRQ_KEY =
    (gpio_id_t)g_bk7258_gpioirq_config->user_button_gpio;
  if (g_bk7258_gpioirq_config->user_button_active_low)
    {
      BK7258_GPIOIRQ_PRESS_EDGE = GPIO_INT_TYPE_FALLING_EDGE;
      BK7258_GPIOIRQ_RELEASE_EDGE = GPIO_INT_TYPE_RISING_EDGE;
      BK7258_GPIOIRQ_PRESS_EDGE_NAME = "FALL";
      BK7258_GPIOIRQ_RELEASE_EDGE_NAME = "RISE";
      BK7258_GPIOIRQ_PRESSED_LEVEL = false;
      BK7258_GPIOIRQ_RELEASED_LEVEL = true;
      g_bk7258_gpioirq_key_config.pull_mode = GPIO_PULL_UP_EN;
    }
  else
    {
      BK7258_GPIOIRQ_PRESS_EDGE = GPIO_INT_TYPE_RISING_EDGE;
      BK7258_GPIOIRQ_RELEASE_EDGE = GPIO_INT_TYPE_FALLING_EDGE;
      BK7258_GPIOIRQ_PRESS_EDGE_NAME = "RISE";
      BK7258_GPIOIRQ_RELEASE_EDGE_NAME = "FALL";
      BK7258_GPIOIRQ_PRESSED_LEVEL = true;
      BK7258_GPIOIRQ_RELEASED_LEVEL = false;
      g_bk7258_gpioirq_key_config.pull_mode = GPIO_PULL_DOWN_EN;
    }

  printf("bkgpioirq: BEGIN board=%s key=P%u gpio_s=%u/irq%u\n",
         g_bk7258_gpioirq_config->name,
         (unsigned int)BK7258_GPIOIRQ_KEY,
         (unsigned int)BK7258_GPIOIRQ_SECURE_SOURCE,
         (unsigned int)BK7258_GPIOIRQ_SECURE_IRQ);

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

  sys_drv_int_group2_enable(BK7258_GPIOIRQ_ROUTE_MASK);

  error = bk7258_sdk_irq_test_snapshot_handler(
            BK7258_GPIOIRQ_SECURE_SOURCE, &gpio_handler);
  if (error != BK_OK || gpio_handler == NULL)
    {
      printf("bkgpioirq: FAIL snapshot GPIO_S ret=%d handler=0x%08lx\n",
             (int)error, (unsigned long)(uintptr_t)gpio_handler);
      result = -EIO;
      goto out;
    }

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
      printf("bkgpioirq: FAIL P%u already has interrupt enabled\n",
             (unsigned int)BK7258_GPIOIRQ_KEY);
      result = -EBUSY;
      goto out;
    }

  error = bk_gpio_set_config(BK7258_GPIOIRQ_KEY,
                             &g_bk7258_gpioirq_key_config);
  result = bk7258_gpioirq_result("configure input pull", error);
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

  printf("bkgpioirq: release USERKEY before press-edge test\n");
  if (!bk7258_gpioirq_wait_level(BK7258_GPIOIRQ_RELEASED_LEVEL,
                                 BK7258_GPIOIRQ_WAIT_STEPS))
    {
      printf("bkgpioirq: FAIL release timeout raw=%u\n",
             bk_gpio_get_input(BK7258_GPIOIRQ_KEY) ? 1u : 0u);
      result = -ETIMEDOUT;
      goto out;
    }

  result = bk7258_gpioirq_run_edge(BK7258_GPIOIRQ_PRESS_EDGE,
                                   BK7258_GPIOIRQ_PRESSED_LEVEL,
                                   BK7258_GPIOIRQ_PRESS_EDGE_NAME, "PRESS",
                                   &interrupt_enabled);
  if (result < 0)
    {
      goto out;
    }

  result = bk7258_gpioirq_run_edge(BK7258_GPIOIRQ_RELEASE_EDGE,
                                   BK7258_GPIOIRQ_RELEASED_LEVEL,
                                   BK7258_GPIOIRQ_RELEASE_EDGE_NAME,
                                   "RELEASE", &interrupt_enabled);
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
