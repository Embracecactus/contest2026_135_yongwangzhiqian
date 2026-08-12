/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/
 * bk7258_gpio_lowerhalf.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX GPIO lower-half devices for the T5-AI board LED and USERKEY.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/ioexpander/gpio.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>

#include <arch/board/board.h>

#include <driver/gpio.h>
#include <driver/int.h>
#include <driver/int_types.h>

#include "bk7258_sdk_irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_GPIO_LED_PIN                 \
  ((gpio_id_t)BK7258_BOARD_USER_LED_GPIO)
#define BK7258_GPIO_KEY_PIN                 \
  ((gpio_id_t)BK7258_BOARD_USER_BUTTON_GPIO)
#define BK7258_GPIO_LED_MINOR               0
#define BK7258_GPIO_KEY_MINOR               1
#if (defined(CONFIG_BK7258_UART1) && \
     BK7258_BOARD_USER_LED_CONSOLE_SHARED) || \
    (defined(CONFIG_BK7258_SWD_DEBUG) && \
     defined(CONFIG_BK7258_SWD_PINS_P0_P1) && \
     (BK7258_BOARD_USER_LED_GPIO == 0 || \
      BK7258_BOARD_USER_LED_GPIO == 1))
#  define BK7258_GPIO_LED_AVAILABLE          0
#else
#  define BK7258_GPIO_LED_AVAILABLE          1
#endif
#define BK7258_GPIO_SECURE_SOURCE           INT_SRC_GPIO
#define BK7258_GPIO_ROUTE_REG               0x44010084u
#define BK7258_GPIO_SECURE_ROUTE_BIT        (1u << 23)
#define BK7258_GPIO_ROUTE_MASK              BK7258_GPIO_SECURE_ROUTE_BIT

/****************************************************************************
 * Compile-time Invariants
 ****************************************************************************/

_Static_assert(BK7258_BOARD_USER_LED_GPIO < GPIO_NUM,
               "board LED GPIO is outside the BK7258 GPIO range");
_Static_assert(BK7258_BOARD_USER_BUTTON_GPIO < GPIO_NUM,
               "board button GPIO is outside the BK7258 GPIO range");
_Static_assert(BK7258_BOARD_USER_LED_GPIO !=
               BK7258_BOARD_USER_BUTTON_GPIO,
               "board LED and button GPIOs must differ");
_Static_assert(BK7258_BOARD_USER_LED_ACTIVE_HIGH == 1,
               "GPIO lower-half currently requires an active-high LED");
_Static_assert(BK7258_BOARD_USER_BUTTON_ACTIVE_LOW == 1,
               "GPIO lower-half currently requires an active-low button");
_Static_assert(INT_SRC_GPIO == 55,
               "GPIO lower-half requires GPIO_S source 55");

extern void bk7258_gpio_cp_irq_enable(void);

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_gpio_output_s
{
  struct gpio_dev_s gpio;
  mutex_t lock;
  gpio_id_t pin;
  bool value;
};

struct bk7258_gpio_interrupt_s
{
  struct gpio_dev_s gpio;
  mutex_t lock;
  gpio_id_t pin;
  volatile pin_interrupt_t callback;
  uint32_t saved_route;
  bool enabled;
  bool route_gate_touched;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_gpio_output_read(FAR struct gpio_dev_s *dev,
                                   FAR bool *value);
static int bk7258_gpio_output_write(FAR struct gpio_dev_s *dev,
                                    bool value);
static int bk7258_gpio_output_setpintype(FAR struct gpio_dev_s *dev,
                                         enum gpio_pintype_e pintype);
static int bk7258_gpio_key_read(FAR struct gpio_dev_s *dev,
                                FAR bool *value);
static int bk7258_gpio_key_attach(FAR struct gpio_dev_s *dev,
                                  pin_interrupt_t callback);
static int bk7258_gpio_key_enable(FAR struct gpio_dev_s *dev, bool enable);
static int bk7258_gpio_key_setpintype(FAR struct gpio_dev_s *dev,
                                      enum gpio_pintype_e pintype);
static int bk7258_gpio_setdebounce(FAR struct gpio_dev_s *dev,
                                   unsigned long duration);
static int bk7258_gpio_output_setmask(FAR struct gpio_dev_s *dev,
                                      bool enable);
static int bk7258_gpio_key_setmask(FAR struct gpio_dev_s *dev, bool enable);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct gpio_operations_s g_bk7258_gpio_output_ops =
{
  .go_read        = bk7258_gpio_output_read,
  .go_write       = bk7258_gpio_output_write,
  .go_attach      = NULL,
  .go_enable      = NULL,
  .go_setpintype  = bk7258_gpio_output_setpintype,
  .go_setdebounce = bk7258_gpio_setdebounce,
  .go_setmask     = bk7258_gpio_output_setmask,
};

static const struct gpio_operations_s g_bk7258_gpio_key_ops =
{
  .go_read        = bk7258_gpio_key_read,
  .go_write       = NULL,
  .go_attach      = bk7258_gpio_key_attach,
  .go_enable      = bk7258_gpio_key_enable,
  .go_setpintype  = bk7258_gpio_key_setpintype,
  .go_setdebounce = bk7258_gpio_setdebounce,
  .go_setmask     = bk7258_gpio_key_setmask,
};

static const gpio_config_t g_bk7258_gpio_led_config =
{
  .io_mode = GPIO_OUTPUT_ENABLE,
  .pull_mode = GPIO_PULL_DISABLE,
  .func_mode = GPIO_SECOND_FUNC_DISABLE,
};

static const gpio_config_t g_bk7258_gpio_key_config =
{
  .io_mode = GPIO_INPUT_ENABLE,
  .pull_mode = GPIO_PULL_UP_EN,
  .func_mode = GPIO_SECOND_FUNC_DISABLE,
};

/* These instances represent selected physical-board resources for the full
 * boot lifetime.  The key instance remains file-local so its SDK per-pin ISR
 * can dispatch without allocation or blocking in interrupt context.
 */

static struct bk7258_gpio_output_s g_bk7258_gpio_led =
{
  .gpio =
    {
      .gp_pintype = GPIO_OUTPUT_PIN,
      .gp_ops = &g_bk7258_gpio_output_ops,
    },
  .lock = NXMUTEX_INITIALIZER,
  .pin = BK7258_GPIO_LED_PIN,
};

static struct bk7258_gpio_interrupt_s g_bk7258_gpio_key =
{
  .gpio =
    {
      .gp_pintype = GPIO_INTERRUPT_FALLING_PIN,
      .gp_ops = &g_bk7258_gpio_key_ops,
    },
  .lock = NXMUTEX_INITIALIZER,
  .pin = BK7258_GPIO_KEY_PIN,
};

static mutex_t g_bk7258_gpio_init_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_gpio_initialized;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_gpio_result(bk_err_t error)
{
  if (error == BK_OK)
    {
      return OK;
    }

  if (error == BK_ERR_GPIO_INTERNAL_USED)
    {
      return -EBUSY;
    }

  return -EIO;
}

static uint32_t bk7258_gpio_enable_route(uint32_t *saved)
{
  volatile uint32_t *reg =
    (volatile uint32_t *)(uintptr_t)BK7258_GPIO_ROUTE_REG;
  irqstate_t flags;
  uint32_t value;

  flags = enter_critical_section();
  *saved = *reg;
  *reg = *saved | BK7258_GPIO_ROUTE_MASK;
  value = *reg;
  leave_critical_section(flags);
  return value;
}

static uint32_t bk7258_gpio_restore_route(uint32_t saved)
{
  volatile uint32_t *reg =
    (volatile uint32_t *)(uintptr_t)BK7258_GPIO_ROUTE_REG;
  irqstate_t flags;
  uint32_t value;

  flags = enter_critical_section();
  value = *reg;
  value &= ~BK7258_GPIO_ROUTE_MASK;
  value |= saved & BK7258_GPIO_ROUTE_MASK;
  *reg = value;
  value = *reg;
  leave_critical_section(flags);
  return value;
}

static int bk7258_gpio_close_route(
  FAR struct bk7258_gpio_interrupt_s *key)
{
  uint32_t value;

  if (key->route_gate_touched)
    {
      value = bk7258_gpio_restore_route(key->saved_route);
      if ((value & BK7258_GPIO_ROUTE_MASK) !=
          (key->saved_route & BK7258_GPIO_ROUTE_MASK))
        {
          /* Keep source37 installed while the route might still be live. */

          return -EIO;
        }

      key->route_gate_touched = false;
    }

  return OK;
}

static int bk7258_gpio_open_route(
  FAR struct bk7258_gpio_interrupt_s *key)
{
  int_group_isr_t secure_handler = NULL;
  uint32_t value;
  bk_err_t error;
  int result;

  if (key->route_gate_touched)
    {
      result = bk7258_gpio_close_route(key);
      if (result < 0)
        {
          return result;
        }
    }

  /* Enable GPIO interrupt forwarding to CPU0 at the system level.
   * The CP SDK only calls this from the low-power entry path, never from
   * normal GPIO init.  Without it, GPIO pending bits never reach the NVIC.
   */

  bk7258_gpio_cp_irq_enable();

  error = bk7258_sdk_irq_snapshot_handler(BK7258_GPIO_SECURE_SOURCE,
                                           &secure_handler);
  if (error != BK_OK || secure_handler == NULL)
    {
      syslog(LOG_ERR,
             "bk7258 gpio: snapshot source=%u SDK error=%d handler=%p\n",
             (unsigned int)BK7258_GPIO_SECURE_SOURCE, (int)error,
             (FAR void *)(uintptr_t)secure_handler);
      return -EIO;
    }

  value = bk7258_gpio_enable_route(&key->saved_route);
  key->route_gate_touched = true;
  if ((value & BK7258_GPIO_ROUTE_MASK) != BK7258_GPIO_ROUTE_MASK)
    {
      syslog(LOG_ERR,
             "bk7258 gpio: route readback saved=0x%08lx value=0x%08lx "
             "mask=0x%08lx\n",
             (unsigned long)key->saved_route, (unsigned long)value,
             (unsigned long)BK7258_GPIO_ROUTE_MASK);
      (void)bk7258_gpio_close_route(key);
      return -EIO;
    }

  return OK;
}

static int bk7258_gpio_key_interrupt_type(
  enum gpio_pintype_e pintype, FAR gpio_int_type_t *type)
{
  switch (pintype)
    {
      case GPIO_INTERRUPT_PIN:
      case GPIO_INTERRUPT_FALLING_PIN:
        *type = GPIO_INT_TYPE_FALLING_EDGE;
        return OK;

      case GPIO_INTERRUPT_RISING_PIN:
        *type = GPIO_INT_TYPE_RISING_EDGE;
        return OK;

      default:
        return -ENOTSUP;
    }
}

/* This is the pinned SDK per-pin callback, not the hardware NVIC handler.
 * The SDK top-level ISR has already cleared the selected key pin before
 * invoking it.
 */

static void bk7258_gpio_key_isr(gpio_id_t gpio_id)
{
  pin_interrupt_t callback;

  if (gpio_id != BK7258_GPIO_KEY_PIN)
    {
      return;
    }

  callback = g_bk7258_gpio_key.callback;
  if (callback != NULL)
    {
      callback(&g_bk7258_gpio_key.gpio, (uint8_t)gpio_id);
    }
}

static int bk7258_gpio_output_read(FAR struct gpio_dev_s *dev,
                                   FAR bool *value)
{
  FAR struct bk7258_gpio_output_s *output =
    (FAR struct bk7258_gpio_output_s *)dev;
  int result;

  if (output == NULL || value == NULL)
    {
      return -EINVAL;
    }

  result = nxmutex_lock(&output->lock);
  if (result < 0)
    {
      return result;
    }

  *value = output->value;
  nxmutex_unlock(&output->lock);
  return OK;
}

static int bk7258_gpio_output_write(FAR struct gpio_dev_s *dev,
                                    bool value)
{
  FAR struct bk7258_gpio_output_s *output =
    (FAR struct bk7258_gpio_output_s *)dev;
  bk_err_t error;
  int result;

  if (output == NULL)
    {
      return -EINVAL;
    }

  result = nxmutex_lock(&output->lock);
  if (result < 0)
    {
      return result;
    }

  error = value ? bk_gpio_set_output_high(output->pin) :
                  bk_gpio_set_output_low(output->pin);
  result = bk7258_gpio_result(error);
  if (result == OK)
    {
      output->value = value;
    }

  nxmutex_unlock(&output->lock);
  return result;
}

static int bk7258_gpio_output_setpintype(FAR struct gpio_dev_s *dev,
                                         enum gpio_pintype_e pintype)
{
  FAR struct bk7258_gpio_output_s *output =
    (FAR struct bk7258_gpio_output_s *)dev;
  bk_err_t error;
  int result;

  if (output == NULL || pintype != GPIO_OUTPUT_PIN)
    {
      return -ENOTSUP;
    }

  result = nxmutex_lock(&output->lock);
  if (result < 0)
    {
      return result;
    }

  result = bk7258_gpio_result(
             bk_gpio_set_config(output->pin, &g_bk7258_gpio_led_config));
  if (result == OK)
    {
      error = output->value ? bk_gpio_set_output_high(output->pin) :
                              bk_gpio_set_output_low(output->pin);
      result = bk7258_gpio_result(error);
      if (result == OK)
        {
          output->gpio.gp_pintype = GPIO_OUTPUT_PIN;
        }
    }

  nxmutex_unlock(&output->lock);
  return result;
}

static int bk7258_gpio_key_read(FAR struct gpio_dev_s *dev,
                                FAR bool *value)
{
  FAR struct bk7258_gpio_interrupt_s *key =
    (FAR struct bk7258_gpio_interrupt_s *)dev;
  int result;

  if (key == NULL || value == NULL)
    {
      return -EINVAL;
    }

  result = nxmutex_lock(&key->lock);
  if (result < 0)
    {
      return result;
    }

  *value = bk_gpio_get_input(key->pin);
  nxmutex_unlock(&key->lock);
  return OK;
}

static int bk7258_gpio_key_attach(FAR struct gpio_dev_s *dev,
                                  pin_interrupt_t callback)
{
  FAR struct bk7258_gpio_interrupt_s *key =
    (FAR struct bk7258_gpio_interrupt_s *)dev;
  gpio_isr_t isr;
  bk_err_t error;
  int result;

  if (key == NULL)
    {
      return -EINVAL;
    }

  result = nxmutex_lock(&key->lock);
  if (result < 0)
    {
      return result;
    }

  if (key->enabled)
    {
      nxmutex_unlock(&key->lock);
      return -EBUSY;
    }

  isr = callback != NULL ? bk7258_gpio_key_isr : NULL;
  error = bk_gpio_register_isr(key->pin, isr);
  result = bk7258_gpio_result(error);
  if (result < 0)
    {
      syslog(LOG_ERR,
             "bk7258 gpio: attach P%u callback=%p SDK error=%d\n",
             (unsigned int)key->pin,
             (FAR void *)(uintptr_t)callback, (int)error);
    }

  if (result == OK)
    {
      key->callback = callback;
    }

  nxmutex_unlock(&key->lock);
  return result;
}

static int bk7258_gpio_key_enable(FAR struct gpio_dev_s *dev, bool enable)
{
  FAR struct bk7258_gpio_interrupt_s *key =
    (FAR struct bk7258_gpio_interrupt_s *)dev;
  gpio_int_type_t type;
  bk_err_t error;
  int cleanup_result;
  int result;

  if (key == NULL)
    {
      return -EINVAL;
    }

  result = nxmutex_lock(&key->lock);
  if (result < 0)
    {
      return result;
    }

  if (enable && key->enabled)
    {
      nxmutex_unlock(&key->lock);
      return OK;
    }

  if (!enable)
    {
      result = bk7258_gpio_result(bk_gpio_disable_interrupt(key->pin));
      error = bk_gpio_clear_interrupt(key->pin);
      if (error != BK_OK)
        {
          result = -EIO;
        }

      cleanup_result = bk7258_gpio_close_route(key);
      if (result == OK)
        {
          result = cleanup_result;
        }

      key->enabled = false;
      nxmutex_unlock(&key->lock);
      return result;
    }

  if (key->callback == NULL)
    {
      nxmutex_unlock(&key->lock);
      return -EINVAL;
    }

  /* AP-side SDK drivers initialize their own default GPIO table after the
   * CP lower-half has been registered.  In the v3.1.1.9 BK7258 AP table,
   * P12 is reset to a disabled secondary-function entry.  Reassert the
   * board-owned key configuration at the point the NuttX client enables the
   * interrupt so the standard GPIO ABI does not depend on AP startup order.
   */

  result = bk7258_gpio_result(
             bk_gpio_set_config(key->pin, &g_bk7258_gpio_key_config));
  if (result < 0)
    {
      syslog(LOG_ERR, "bk7258 gpio: configure key P%u failed=%d\n",
             (unsigned int)key->pin, result);
      nxmutex_unlock(&key->lock);
      return result;
    }

  result = bk7258_gpio_key_interrupt_type(
             (enum gpio_pintype_e)key->gpio.gp_pintype, &type);
  if (result < 0)
    {
      nxmutex_unlock(&key->lock);
      return result;
    }

  result = bk7258_gpio_result(
             bk_gpio_set_interrupt_type(key->pin, type));
  if (result < 0)
    {
      syslog(LOG_ERR,
             "bk7258 gpio: set interrupt type P%u type=%u failed=%d\n",
             (unsigned int)key->pin, (unsigned int)type, result);
      nxmutex_unlock(&key->lock);
      return result;
    }

  result = bk7258_gpio_result(bk_gpio_clear_interrupt(key->pin));
  if (result < 0)
    {
      syslog(LOG_ERR, "bk7258 gpio: clear P%u failed=%d\n",
             (unsigned int)key->pin, result);
      nxmutex_unlock(&key->lock);
      return result;
    }

  /* Register the SDK per-pin callback.  This is what the SDK's gpio_isr
   * uses to deliver the interrupt to our handler.
   */

  result = bk7258_gpio_result(
             bk_gpio_register_isr(key->pin, bk7258_gpio_key_isr));
  if (result < 0)
    {
      syslog(LOG_ERR, "bk7258 gpio: register ISR P%u failed=%d\n",
             (unsigned int)key->pin, result);
      nxmutex_unlock(&key->lock);
      return result;
    }

  result = bk7258_gpio_open_route(key);
  if (result < 0)
    {
      syslog(LOG_ERR,
             "bk7258 gpio: open route P%u reg=0x%08lx failed=%d\n",
             (unsigned int)key->pin,
             (unsigned long)BK7258_GPIO_ROUTE_REG, result);
      (void)bk_gpio_register_isr(key->pin, NULL);
      nxmutex_unlock(&key->lock);
      return result;
    }

  result = bk7258_gpio_result(bk_gpio_enable_interrupt(key->pin));
  if (result < 0)
    {
      syslog(LOG_ERR, "bk7258 gpio: enable P%u failed=%d\n",
             (unsigned int)key->pin, result);
      cleanup_result = bk7258_gpio_close_route(key);
      if (cleanup_result < 0)
        {
          result = cleanup_result;
        }

      nxmutex_unlock(&key->lock);
      return result;
    }

  key->enabled = true;
  nxmutex_unlock(&key->lock);
  return OK;
}

static int bk7258_gpio_key_setpintype(FAR struct gpio_dev_s *dev,
                                      enum gpio_pintype_e pintype)
{
  FAR struct bk7258_gpio_interrupt_s *key =
    (FAR struct bk7258_gpio_interrupt_s *)dev;
  gpio_int_type_t type;
  int result;

  if (key == NULL)
    {
      return -EINVAL;
    }

  switch (pintype)
    {
      case GPIO_INPUT_PIN_PULLUP:
      case GPIO_INTERRUPT_PIN:
      case GPIO_INTERRUPT_RISING_PIN:
      case GPIO_INTERRUPT_FALLING_PIN:
        break;

      default:
        return -ENOTSUP;
    }

  result = nxmutex_lock(&key->lock);
  if (result < 0)
    {
      return result;
    }

  if (key->enabled || key->callback != NULL)
    {
      nxmutex_unlock(&key->lock);
      return -EBUSY;
    }

  result = bk7258_gpio_result(
             bk_gpio_set_config(key->pin, &g_bk7258_gpio_key_config));
  if (result < 0)
    {
      nxmutex_unlock(&key->lock);
      return result;
    }

  if (pintype >= GPIO_INTERRUPT_PIN)
    {
      result = bk7258_gpio_key_interrupt_type(pintype, &type);
      if (result == OK)
        {
          result = bk7258_gpio_result(
                     bk_gpio_set_interrupt_type(key->pin, type));
        }
    }

  if (result == OK)
    {
      key->gpio.gp_pintype = pintype;
    }

  nxmutex_unlock(&key->lock);
  return result;
}

static int bk7258_gpio_setdebounce(FAR struct gpio_dev_s *dev,
                                   unsigned long duration)
{
  (void)dev;
  (void)duration;
  return -ENOTSUP;
}

static int bk7258_gpio_output_setmask(FAR struct gpio_dev_s *dev,
                                      bool enable)
{
  (void)dev;
  (void)enable;
  return -ENOTSUP;
}

static int bk7258_gpio_key_setmask(FAR struct gpio_dev_s *dev, bool enable)
{
  return bk7258_gpio_key_enable(dev, !enable);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_gpio_lowerhalf_initialize
 *
 * Description:
 *   Configure and register the selected physical board's LED and user-key
 *   devices as /dev/gpio0 and /dev/gpio1.  A board whose LED is claimed by
 *   UART1 or the active P0/P1 SWD route omits /dev/gpio0 without touching
 *   that pin, while keeping the independent user-key device available as
 *   /dev/gpio1.
 *
 * Returned Value:
 *   Zero on success or a negated errno value on failure.
 *
 ****************************************************************************/

int bk7258_gpio_lowerhalf_initialize(void)
{
  uint32_t saved_led = 0;
  uint32_t saved_key;
  bool led_configured = false;
  bool key_configured = false;
  bool led_registered = false;
  bk_err_t error;
  int result;

  result = nxmutex_lock(&g_bk7258_gpio_init_lock);
  if (result < 0)
    {
      return result;
    }

  if (g_bk7258_gpio_initialized)
    {
      nxmutex_unlock(&g_bk7258_gpio_init_lock);
      return OK;
    }

  result = bk7258_gpio_result(bk_gpio_driver_init());
  if (result < 0)
    {
      goto out;
    }

  saved_key = bk_gpio_get_value(BK7258_GPIO_KEY_PIN);

  if (BK7258_GPIO_LED_AVAILABLE)
    {
      saved_led = bk_gpio_get_value(BK7258_GPIO_LED_PIN);
      result = bk7258_gpio_result(
                 bk_gpio_set_config(BK7258_GPIO_LED_PIN,
                                    &g_bk7258_gpio_led_config));
      if (result < 0)
        {
          goto out;
        }

      led_configured = true;
      result = bk7258_gpio_result(
                 bk_gpio_set_output_low(BK7258_GPIO_LED_PIN));
      if (result < 0)
        {
          goto restore;
        }

      g_bk7258_gpio_led.value = false;
    }

  result = bk7258_gpio_result(
             bk_gpio_set_config(BK7258_GPIO_KEY_PIN,
                                &g_bk7258_gpio_key_config));
  if (result < 0)
    {
      goto restore;
    }

  key_configured = true;
  result = bk7258_gpio_result(
             bk_gpio_disable_interrupt(BK7258_GPIO_KEY_PIN));
  if (result < 0)
    {
      goto restore;
    }

  result = bk7258_gpio_result(
             bk_gpio_clear_interrupt(BK7258_GPIO_KEY_PIN));
  if (result < 0)
    {
      goto restore;
    }

  if (BK7258_GPIO_LED_AVAILABLE)
    {
      result = gpio_pin_register(&g_bk7258_gpio_led.gpio,
                                 BK7258_GPIO_LED_MINOR);
      if (result < 0)
        {
          goto restore;
        }

      led_registered = true;
    }

  result = gpio_pin_register(&g_bk7258_gpio_key.gpio,
                             BK7258_GPIO_KEY_MINOR);
  if (result < 0)
    {
      goto unregister;
    }

  g_bk7258_gpio_initialized = true;
  nxmutex_unlock(&g_bk7258_gpio_init_lock);
  return OK;

unregister:
  if (led_registered)
    {
      (void)gpio_pin_unregister(&g_bk7258_gpio_led.gpio,
                                BK7258_GPIO_LED_MINOR);
    }

restore:
  if (key_configured)
    {
      error = bk_gpio_set_value(BK7258_GPIO_KEY_PIN, saved_key);
      if (error != BK_OK)
        {
          result = -EIO;
        }
    }

  if (led_configured)
    {
      error = bk_gpio_set_value(BK7258_GPIO_LED_PIN, saved_led);
      if (error != BK_OK)
        {
          result = -EIO;
        }
    }

out:
  nxmutex_unlock(&g_bk7258_gpio_init_lock);
  return result;
}
