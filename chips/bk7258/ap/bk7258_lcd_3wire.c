/****************************************************************************
 * chips/bk7258/ap/bk7258_lcd_3wire.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 GPIO transport for 3-wire, 9-bit LCD control buses.  Panel command
 * sequences deliberately live in transport-independent NuttX LCD drivers.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LCD_RGB

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/irq.h>

#include <arch/chip/bk7258_lcd_3wire.h>

#include <driver/gpio.h>

static int bk7258_lcd_3wire_gpio_write(uint8_t pin, bool high)
{
  bk_err_t ret = high ? bk_gpio_set_output_high((gpio_id_t)pin) :
                        bk_gpio_set_output_low((gpio_id_t)pin);
  return ret == BK_OK ? OK : -EIO;
}

static int bk7258_lcd_3wire_send_byte(
  FAR const struct bk7258_lcd_3wire_s *bus, uint8_t data)
{
  irqstate_t flags;
  uint8_t bit;
  int ret = OK;

  flags = up_irq_save();
  for (bit = 0; bit < 8; bit++)
    {
      ret = bk7258_lcd_3wire_gpio_write(bus->data_gpio,
                                        (data & 0x80u) != 0);
      if (ret < 0)
        {
          break;
        }

      data <<= 1;
      ret = bk7258_lcd_3wire_gpio_write(bus->clock_gpio, false);
      if (ret < 0)
        {
          break;
        }

      ret = bk7258_lcd_3wire_gpio_write(bus->clock_gpio, true);
      if (ret < 0)
        {
          break;
        }
    }

  up_irq_restore(flags);
  return ret;
}

int bk7258_lcd_3wire_initialize(void *arg)
{
  FAR const struct bk7258_lcd_3wire_s *bus = arg;
  int ret;

  if (bus == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_lcd_3wire_gpio_write(bus->clock_gpio, true);
  if (ret >= 0)
    {
      ret = bk7258_lcd_3wire_gpio_write(bus->chip_select_gpio, true);
    }

  if (ret >= 0)
    {
      ret = bk7258_lcd_3wire_gpio_write(bus->data_gpio, false);
    }

  if (ret >= 0)
    {
      ret = bk7258_lcd_3wire_gpio_write(bus->reset_gpio, false);
    }

  return ret;
}

int bk7258_lcd_3wire_reset(void *arg, bool high)
{
  FAR const struct bk7258_lcd_3wire_s *bus = arg;

  if (bus == NULL)
    {
      return -EINVAL;
    }

  return bk7258_lcd_3wire_gpio_write(bus->reset_gpio, high);
}

int bk7258_lcd_3wire_write(void *arg, bool data_phase, uint8_t value)
{
  FAR const struct bk7258_lcd_3wire_s *bus = arg;
  int ret;

  if (bus == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_lcd_3wire_gpio_write(bus->chip_select_gpio, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_lcd_3wire_gpio_write(bus->data_gpio, data_phase);
  if (ret >= 0)
    {
      ret = bk7258_lcd_3wire_gpio_write(bus->clock_gpio, false);
    }

  if (ret >= 0)
    {
      ret = bk7258_lcd_3wire_gpio_write(bus->clock_gpio, true);
    }

  if (ret >= 0)
    {
      ret = bk7258_lcd_3wire_send_byte(bus, value);
    }

  (void)bk7258_lcd_3wire_gpio_write(bus->chip_select_gpio, true);
  return ret;
}

#endif /* CONFIG_BK7258_LCD_RGB */
