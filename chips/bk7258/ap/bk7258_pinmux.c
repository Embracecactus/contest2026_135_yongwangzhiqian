/****************************************************************************
 * chips/bk7258/ap/bk7258_pinmux.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Serialized BK7258 SoC pin-function selector updates.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_CORE

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <arch/chip/bk7258_pinmux.h>

#include <common/bk_err.h>
#include <driver/gpio.h>

#include "arm_internal.h"

#define BK7258_PINMUX_SYS_BASE        0x44010000u
#define BK7258_PINMUX_GPIO_BASE       0x44000400u
#define BK7258_PINMUX_SELECTOR_BASE   (BK7258_PINMUX_SYS_BASE + 0xc0u)
#define BK7258_PINMUX_PINS_PER_WORD   8u
#define BK7258_PINMUX_WIDTH           4u
#define BK7258_PINMUX_FUNCTION_MAX    0x0fu
#define BK7258_PINMUX_PIN_COUNT       56u
#define BK7258_PINMUX_PERIPHERAL_BIT  (1u << 6)
#define BK7258_GPIO_OUTPUT_HIGH_WORD  (1u << 1)
#define BK7258_GPIO_INPUT_ENABLE_BIT  (1u << 5)
#define BK7258_GPIO_PULL_ENABLE_BIT   (1u << 4)
#define BK7258_GPIO_PULL_UP_BIT       (1u << 3)
#define BK7258_GPIO_INPUT_MONITOR_BIT (1u << 2)
#define BK7258_GPIO_OUTPUT_LATCH_BIT  (1u << 1)
#define BK7258_GPIO_RELEASE_WORD      (BK7258_GPIO_INPUT_ENABLE_BIT | \
                                       BK7258_GPIO_PULL_ENABLE_BIT | \
                                       BK7258_GPIO_PULL_UP_BIT | \
                                       BK7258_GPIO_INPUT_MONITOR_BIT | \
                                       BK7258_GPIO_OUTPUT_LATCH_BIT)

extern uint32_t sys_amp_res_acquire(void);
extern uint32_t sys_amp_res_release(void);
extern bk_err_t bk_pm_module_vote_ctrl_external_ldo(
  uint32_t module, gpio_id_t gpio_id, gpio_output_state_e value);

struct bk7258_gpio_irq_slot_s
{
  bk7258_gpio_irq_callback_t callback;
  FAR void *arg;
};

static struct bk7258_gpio_irq_slot_s
  g_bk7258_gpio_irq_slots[BK7258_PINMUX_PIN_COUNT];

static void bk7258_gpio_sdk_isr(gpio_id_t gpio)
{
  bk7258_gpio_irq_callback_t callback;
  FAR void *arg;
  irqstate_t flags;

  if ((uint32_t)gpio >= BK7258_PINMUX_PIN_COUNT)
    {
      return;
    }

  flags = up_irq_save();
  callback = g_bk7258_gpio_irq_slots[gpio].callback;
  arg = g_bk7258_gpio_irq_slots[gpio].arg;
  up_irq_restore(flags);

  if (callback != NULL)
    {
      callback((uint8_t)gpio, arg);
    }
}

static uintptr_t bk7258_pinmux_selector_address(uint8_t pin)
{
  return BK7258_PINMUX_SELECTOR_BASE +
         pin / BK7258_PINMUX_PINS_PER_WORD * sizeof(uint32_t);
}

static uintptr_t bk7258_pinmux_pad_address(uint8_t pin)
{
  return BK7258_PINMUX_GPIO_BASE + pin * sizeof(uint32_t);
}

int bk7258_pinmux_apply(const struct bk7258_pinmux_config_s *configs,
                        size_t count)
{
  irqstate_t flags;
  size_t index;
  int ret = OK;

  if (configs == NULL || count == 0u)
    {
      return -EINVAL;
    }

  for (index = 0u; index < count; index++)
    {
      if (configs[index].pin >= BK7258_PINMUX_PIN_COUNT ||
          configs[index].function > BK7258_PINMUX_FUNCTION_MAX)
        {
          return -ERANGE;
        }
    }

  if (sys_amp_res_acquire() != 0u)
    {
      return -EBUSY;
    }

  flags = up_irq_save();
  for (index = 0u; index < count; index++)
    {
      uintptr_t selector =
        bk7258_pinmux_selector_address(configs[index].pin);
      uintptr_t pad = bk7258_pinmux_pad_address(configs[index].pin);
      uint32_t shift =
        configs[index].pin % BK7258_PINMUX_PINS_PER_WORD *
        BK7258_PINMUX_WIDTH;
      uint32_t mask = BK7258_PINMUX_FUNCTION_MAX << shift;
      uint32_t value = getreg32(selector);

      putreg32((value & ~mask) |
               (uint32_t)configs[index].function << shift, selector);
      value = getreg32(pad);
      putreg32(configs[index].peripheral ?
               value | BK7258_PINMUX_PERIPHERAL_BIT :
               value & ~BK7258_PINMUX_PERIPHERAL_BIT, pad);
    }

  __asm volatile ("dmb sy" ::: "memory");
  up_irq_restore(flags);

  if (sys_amp_res_release() != 0u)
    {
      ret = -EIO;
    }

  return ret;
}

static int bk7258_gpio_reclaim(uint8_t pin)
{
  const struct bk7258_pinmux_config_s config =
  {
    .pin = pin,
    .function = 0u,
    .peripheral = false,
  };

  return bk7258_pinmux_apply(&config, 1u);
}

static int bk7258_gpio_driver_ready(void)
{
  return bk_gpio_driver_init() == BK_OK ? OK : -EIO;
}

int bk7258_gpio_configure_output(uint8_t pin, bool high,
                                 enum bk7258_gpio_drive_e drive)
{
  gpio_id_t gpio = (gpio_id_t)pin;
  int ret;

  if (pin >= BK7258_PINMUX_PIN_COUNT || drive > BK7258_GPIO_DRIVE_3)
    {
      return -ERANGE;
    }

  ret = bk7258_gpio_driver_ready();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_gpio_reclaim(pin);
  if (ret < 0)
    {
      return ret;
    }

  if (bk_gpio_disable_input(gpio) != BK_OK ||
      bk_gpio_disable_pull(gpio) != BK_OK ||
      bk_gpio_set_output_value(gpio, high) != BK_OK ||
      bk_gpio_set_capacity(gpio, (uint32_t)drive) != BK_OK ||
      bk_gpio_enable_output(gpio) != BK_OK)
    {
      return -EIO;
    }

  return OK;
}

int bk7258_gpio_configure_input(uint8_t pin,
                                enum bk7258_gpio_pull_e pull)
{
  gpio_id_t gpio = (gpio_id_t)pin;
  bk_err_t error;
  int ret;

  if (pin >= BK7258_PINMUX_PIN_COUNT || pull > BK7258_GPIO_PULL_UP)
    {
      return -ERANGE;
    }

  ret = bk7258_gpio_driver_ready();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_gpio_reclaim(pin);
  if (ret < 0)
    {
      return ret;
    }

  if (bk_gpio_disable_output(gpio) != BK_OK ||
      bk_gpio_enable_input(gpio) != BK_OK)
    {
      return -EIO;
    }

  if (pull == BK7258_GPIO_PULL_UP)
    {
      error = bk_gpio_pull_up(gpio);
    }
  else if (pull == BK7258_GPIO_PULL_DOWN)
    {
      error = bk_gpio_pull_down(gpio);
    }
  else
    {
      error = bk_gpio_disable_pull(gpio);
    }

  return error == BK_OK ? OK : -EIO;
}

int bk7258_gpio_write(uint8_t pin, bool high)
{
  if (pin >= BK7258_PINMUX_PIN_COUNT)
    {
      return -ERANGE;
    }

  return bk_gpio_set_output_value((gpio_id_t)pin, high) == BK_OK ?
         OK : -EIO;
}

int bk7258_gpio_read_input(uint8_t pin, FAR bool *high)
{
  if (pin >= BK7258_PINMUX_PIN_COUNT || high == NULL)
    {
      return -EINVAL;
    }

  *high = bk_gpio_get_input((gpio_id_t)pin);
  return OK;
}

int bk7258_gpio_read_output(uint8_t pin, FAR bool *high)
{
  if (pin >= BK7258_PINMUX_PIN_COUNT || high == NULL)
    {
      return -EINVAL;
    }

  *high = bk_gpio_get_output((gpio_id_t)pin);
  return OK;
}

int bk7258_gpio_configure_open_drain(uint8_t pin,
                                     enum bk7258_gpio_pull_e pull)
{
  gpio_id_t gpio = (gpio_id_t)pin;
  bk_err_t error;
  int ret;

  if (pin >= BK7258_PINMUX_PIN_COUNT || pull > BK7258_GPIO_PULL_UP)
    {
      return -ERANGE;
    }

  ret = bk7258_gpio_driver_ready();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_gpio_reclaim(pin);
  if (ret < 0)
    {
      return ret;
    }

  if (bk_gpio_set_output_value(gpio, false) != BK_OK ||
      bk_gpio_enable_input(gpio) != BK_OK ||
      bk_gpio_disable_output(gpio) != BK_OK)
    {
      return -EIO;
    }

  if (pull == BK7258_GPIO_PULL_UP)
    {
      error = bk_gpio_pull_up(gpio);
    }
  else if (pull == BK7258_GPIO_PULL_DOWN)
    {
      error = bk_gpio_pull_down(gpio);
    }
  else
    {
      error = bk_gpio_disable_pull(gpio);
    }

  return error == BK_OK ? OK : -EIO;
}

int bk7258_gpio_open_drain_write(uint8_t pin, bool high)
{
  if (pin >= BK7258_PINMUX_PIN_COUNT)
    {
      return -ERANGE;
    }

  return (high ? bk_gpio_disable_output((gpio_id_t)pin) :
                 bk_gpio_enable_output((gpio_id_t)pin)) == BK_OK ?
         OK : -EIO;
}

int bk7258_gpio_fast_write(uint8_t pin, bool high)
{
  if (pin >= BK7258_PINMUX_PIN_COUNT)
    {
      return -ERANGE;
    }

  putreg32(high ? BK7258_GPIO_OUTPUT_HIGH_WORD : 0u,
           bk7258_pinmux_pad_address(pin));
  return OK;
}

int bk7258_gpio_fast_release_pullup(uint8_t pin)
{
  if (pin >= BK7258_PINMUX_PIN_COUNT)
    {
      return -ERANGE;
    }

  putreg32(BK7258_GPIO_RELEASE_WORD, bk7258_pinmux_pad_address(pin));
  return OK;
}

int bk7258_gpio_irq_configure(uint8_t pin,
                              enum bk7258_gpio_pull_e pull,
                              enum bk7258_gpio_irq_trigger_e trigger,
                              bk7258_gpio_irq_callback_t callback,
                              FAR void *arg)
{
  irqstate_t flags;
  int ret;

  if (pin >= BK7258_PINMUX_PIN_COUNT || callback == NULL ||
      trigger != BK7258_GPIO_IRQ_FALLING_EDGE)
    {
      return -EINVAL;
    }

  ret = bk7258_gpio_configure_input(pin, pull);
  if (ret < 0)
    {
      return ret;
    }

  (void)bk_gpio_disable_interrupt((gpio_id_t)pin);
  if (bk_gpio_set_interrupt_type((gpio_id_t)pin,
                                 GPIO_INT_TYPE_FALLING_EDGE) != BK_OK)
    {
      return -EIO;
    }

  flags = up_irq_save();
  g_bk7258_gpio_irq_slots[pin].callback = callback;
  g_bk7258_gpio_irq_slots[pin].arg = arg;
  up_irq_restore(flags);

  if (bk_gpio_register_isr((gpio_id_t)pin, bk7258_gpio_sdk_isr) != BK_OK)
    {
      flags = up_irq_save();
      g_bk7258_gpio_irq_slots[pin].callback = NULL;
      g_bk7258_gpio_irq_slots[pin].arg = NULL;
      up_irq_restore(flags);
      return -EIO;
    }

  return OK;
}

int bk7258_gpio_irq_enable(uint8_t pin, bool enable)
{
  bk_err_t error;

  if (pin >= BK7258_PINMUX_PIN_COUNT)
    {
      return -ERANGE;
    }

  if (enable)
    {
      if (bk_gpio_clear_interrupt((gpio_id_t)pin) != BK_OK)
        {
          return -EIO;
        }

      error = bk_gpio_enable_interrupt((gpio_id_t)pin);
    }
  else
    {
      error = bk_gpio_disable_interrupt((gpio_id_t)pin);
      if (error == BK_OK)
        {
          error = bk_gpio_clear_interrupt((gpio_id_t)pin);
        }
    }

  return error == BK_OK ? OK : -EIO;
}

int bk7258_shared_rail_vote(enum bk7258_shared_rail_client_e client,
                            uint8_t control_pin, bool enable)
{
  uint32_t module;

  if (control_pin >= BK7258_PINMUX_PIN_COUNT)
    {
      return -ERANGE;
    }

  switch (client)
    {
      case BK7258_SHARED_RAIL_SDIO:
        module = GPIO_CTRL_LDO_MODULE_SDIO;
        break;

      case BK7258_SHARED_RAIL_LCD:
        module = GPIO_CTRL_LDO_MODULE_LCD;
        break;

      case BK7258_SHARED_RAIL_NFC:
        module = GPIO_CTRL_LDO_MODULE_NFC;
        break;

      default:
        return -EINVAL;
    }

  if (bk7258_gpio_driver_ready() < 0)
    {
      return -EIO;
    }

  return bk_pm_module_vote_ctrl_external_ldo(
           module, (gpio_id_t)control_pin,
           enable ? GPIO_OUTPUT_STATE_HIGH : GPIO_OUTPUT_STATE_LOW) == BK_OK ?
         OK : -EIO;
}

#endif /* CONFIG_BK7258_AP_CORE */
