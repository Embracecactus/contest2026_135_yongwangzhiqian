/****************************************************************************
 * contest2026_135_yongwangzhiqian/boards/bk7258/common/panels/ili9488_rgb.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ILI9488 RGB-panel register initialization over its three-wire serial bus.
 * The command sequence is source-derived from TuyaOpen's verified T5AI
 * display implementation; all physical pin ownership stays in the selected
 * board profile.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LCD

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <arch/chip/bk7258_lcd.h>

#include <driver/gpio.h>

#include "ili9488_rgb.h"

#define ILI9488_SLPOUT     0x11
#define ILI9488_PWCTR1     0xc0
#define ILI9488_PWCTR2     0xc1
#define ILI9488_VMCTR1     0xc5
#define ILI9488_IFMODE     0xb0
#define ILI9488_FRMCTR1    0xb1
#define ILI9488_INVCTR     0xb4
#define ILI9488_PRCTR      0xb5
#define ILI9488_DFUNCTR    0xb6
#define ILI9488_MADCTL     0x36
#define ILI9488_PIXFMT     0x3a
#define ILI9488_INVON      0x21
#define ILI9488_SETIMAGE   0xe9
#define ILI9488_ACTRL3     0xf7
#define ILI9488_ACTRL4     0xf8
#define ILI9488_GMCTRP1    0xe0
#define ILI9488_GMCTRN1    0xe1
#define ILI9488_DISPON     0x29

/* Sequence format: [data_count][delay_ms][command][data...], terminated by
 * a zero data_count.  data_count includes the command byte.
 */

static const uint8_t g_ili9488_init_sequence[] =
{
  3,  0,   ILI9488_PWCTR1,   0x0e, 0x0e,
  2,  0,   ILI9488_PWCTR2,   0x46,
  4,  0,   ILI9488_VMCTR1,   0x00, 0x2d, 0x80,
  2,  0,   ILI9488_IFMODE,   0x00,
  2,  0,   ILI9488_FRMCTR1,  0xa0,
  2,  0,   ILI9488_INVCTR,   0x02,
  5,  0,   ILI9488_PRCTR,    0x08, 0x0c, 0x50, 0x64,
  3,  0,   ILI9488_DFUNCTR,  0x32, 0x02,
  2,  0,   ILI9488_MADCTL,   0x48,
  2,  0,   ILI9488_PIXFMT,   0x70,
  2,  0,   ILI9488_INVON,    0x00,
  2,  0,   ILI9488_SETIMAGE, 0x01,
  5,  0,   ILI9488_ACTRL3,   0xa9, 0x51, 0x2c, 0x82,
  3,  0,   ILI9488_ACTRL4,   0x21, 0x05,
  16, 0,   ILI9488_GMCTRP1,  0x00, 0x0c, 0x10, 0x03, 0x0f, 0x05,
                              0x37, 0x66, 0x4d, 0x03, 0x0c, 0x0a,
                              0x2f, 0x35, 0x0f,
  16, 0,   ILI9488_GMCTRN1,  0x00, 0x0f, 0x16, 0x06, 0x13, 0x07,
                              0x3b, 0x35, 0x51, 0x07, 0x10, 0x0d,
                              0x36, 0x3b, 0x0f,
  1,  120, ILI9488_SLPOUT,
  1,  20,  ILI9488_DISPON,
  0,
};

static inline bk_err_t ili9488_gpio_write(uint8_t pin, bool high)
{
  gpio_id_t gpio = (gpio_id_t)pin;

  /* Match the working Tuya GPIO adapter: the pins are already outputs, so
   * toggling must not rewrite their output-enable state on every edge.
   */

  return high ? bk_gpio_set_output_high(gpio) :
                bk_gpio_set_output_low(gpio);
}

static inline irqstate_t ili9488_local_irq_save(void)
{
  irqstate_t flags;

  __asm volatile
    (
      "mrs %0, primask\n"
      "cpsid i\n"
      : "=r" (flags)
      :
      : "memory"
    );

  return flags;
}

static inline void ili9488_local_irq_restore(irqstate_t flags)
{
  __asm volatile
    (
      "msr primask, %0\n"
      :
      : "r" (flags)
      : "memory"
    );
}

static void ili9488_send_byte(const struct bk7258_lcd_board_s *board,
                              uint8_t data)
{
  irqstate_t flags;
  uint8_t bit;

  flags = ili9488_local_irq_save();

  for (bit = 0; bit < 8; bit++)
    {
      (void)ili9488_gpio_write(board->control.data_gpio,
                               (data & 0x80u) != 0);
      data <<= 1;
      (void)ili9488_gpio_write(board->control.clock_gpio, false);
      (void)ili9488_gpio_write(board->control.clock_gpio, true);
    }

  ili9488_local_irq_restore(flags);
}

static void ili9488_write(const struct bk7258_lcd_board_s *board,
                          bool data_phase, uint8_t value)
{
  (void)ili9488_gpio_write(board->control.chip_select_gpio, false);
  (void)ili9488_gpio_write(board->control.data_gpio, data_phase);
  (void)ili9488_gpio_write(board->control.clock_gpio, false);
  (void)ili9488_gpio_write(board->control.clock_gpio, true);
  ili9488_send_byte(board, value);
  (void)ili9488_gpio_write(board->control.chip_select_gpio, true);
}

static void ili9488_run_sequence(const struct bk7258_lcd_board_s *board,
                                 const uint8_t *sequence)
{
  while (sequence[0] != 0)
    {
      uint8_t count = sequence[0];
      uint8_t delay = sequence[1];
      uint8_t i;

      ili9488_write(board, false, sequence[2]);
      for (i = 0; i + 1 < count; i++)
        {
          ili9488_write(board, true, sequence[3 + i]);
        }

      if (delay > 0)
        {
          up_mdelay(delay);
        }

      sequence += count + 2;
    }
}

static int ili9488_initialize(const struct bk7258_lcd_board_s *board)
{
  int ret;

  if (board == NULL || board->control_pins_initialize == NULL ||
      board->set_backlight == NULL)
    {
      return -EINVAL;
    }

  ret = board->control_pins_initialize(board);
  if (ret < 0)
    {
      return ret;
    }

  ret = board->set_backlight(board, false);
  if (ret < 0)
    {
      return ret;
    }

  if (ili9488_gpio_write(board->control.clock_gpio, true) != BK_OK ||
      ili9488_gpio_write(board->control.chip_select_gpio, true) != BK_OK ||
      ili9488_gpio_write(board->control.data_gpio, false) != BK_OK ||
      ili9488_gpio_write(board->control.reset_gpio, false) != BK_OK)
    {
      return -EIO;
    }

  up_mdelay(1);
  (void)ili9488_gpio_write(board->control.reset_gpio, true);
  up_mdelay(100);
  (void)ili9488_gpio_write(board->control.reset_gpio, false);
  up_mdelay(100);
  (void)ili9488_gpio_write(board->control.reset_gpio, true);
  up_mdelay(100);

  ili9488_run_sequence(board, g_ili9488_init_sequence);
  return OK;
}

const struct bk7258_lcd_panel_s g_bk7258_ili9488_rgb_panel =
{
  .name       = "ili9488",
  .width      = 320,
  .height     = 480,
  .format     = BK7258_LCD_PIXEL_FORMAT_RGB565,
  .initialize = ili9488_initialize,
};

#endif /* CONFIG_BK7258_LCD */
