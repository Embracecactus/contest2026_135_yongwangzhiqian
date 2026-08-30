/****************************************************************************
 * drivers/lcd/ili9488_rgb.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ILI9488 RGB-panel register initialization.  The sequence is source-derived
 * from tuya/TuyaOpen-T5AI commit 13379b63e07e78770fb4d0bffe36db2754658132,
 * tuyaos/tuyaos_adapter/src/test/test_dvp/lcd_ill9488.c (Apache-2.0).
 * It is intentionally transport-independent: boards and SoCs supply reset
 * and 9-bit write ops.
 * The SAMV71 sam_ili9488.c binding is not reused: it owns an SMC board bus
 * and its HAVE_ILI9488_SPI path is explicitly unsupported, unlike this RGB
 * scanout panel with a separate three-wire control bus.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_LCD_ILI9488_RGB

#include <errno.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/lcd/ili9488.h>
#include <nuttx/lcd/ili9488_rgb.h>

/* [data_count][delay_ms][command][data...], terminated by data_count zero. */

static const uint8_t g_ili9488_init_sequence[] =
{
  3,  0,   ILI9488_CMD_POWER_CONTROL_1,   0x0e, 0x0e,
  2,  0,   ILI9488_CMD_POWER_CONTROL_2,   0x46,
  4,  0,   ILI9488_CMD_VCOM_CONTROL_1,    0x00, 0x2d, 0x80,
  2,  0,   ILI9488_CMD_INTERFACE_MODE_CONTROL, 0x00,
  2,  0,   ILI9488_CMD_FRAME_RATE_CONTROL_NORMAL, 0xa0,
  2,  0,   ILI9488_CMD_DISPLAY_INVERSION_CONTROL, 0x02,
  5,  0,   ILI9488_CMD_BLANKING_PORCH_CONTROL, 0x08, 0x0c, 0x50, 0x64,
  3,  0,   ILI9488_CMD_DISPLAY_FUNCTION_CONTROL, 0x32, 0x02,
  2,  0,   ILI9488_CMD_MEMORY_ACCESS_CONTROL, 0x48,
  2,  0,   ILI9488_CMD_COLMOD_PIXEL_FORMAT_SET, 0x70,
  2,  0,   ILI9488_CMD_DISP_INVERSION_ON, 0x00,
  2,  0,   ILI9488_CMD_SET_IMAGE_FUNCTION, 0x01,
  5,  0,   ILI9488_CMD_ADJUST_CONTROL_3, 0xa9, 0x51, 0x2c, 0x82,
  3,  0,   ILI9488_CMD_ADJUST_CONTROL_4, 0x21, 0x05,
  16, 0,   ILI9488_CMD_POSITIVE_GAMMA_CORRECTION,
                              0x00, 0x0c, 0x10, 0x03, 0x0f, 0x05,
                              0x37, 0x66, 0x4d, 0x03, 0x0c, 0x0a,
                              0x2f, 0x35, 0x0f,
  16, 0,   ILI9488_CMD_NEGATIVE_GAMMA_CORRECTION,
                              0x00, 0x0f, 0x16, 0x06, 0x13, 0x07,
                              0x3b, 0x35, 0x51, 0x07, 0x10, 0x0d,
                              0x36, 0x3b, 0x0f,
  1,  120, ILI9488_CMD_SLEEP_OUT,
  1,  20,  ILI9488_CMD_DISPLAY_ON,
  0,
};

static int ili9488_rgb_run_sequence(FAR const struct ili9488_rgb_ops_s *ops,
                                    FAR void *arg)
{
  FAR const uint8_t *sequence = g_ili9488_init_sequence;
  int ret;

  while (sequence[0] != 0)
    {
      uint8_t count = sequence[0];
      uint8_t delay = sequence[1];
      uint8_t i;

      ret = ops->write(arg, false, sequence[2]);
      if (ret < 0)
        {
          return ret;
        }

      for (i = 0; i + 1 < count; i++)
        {
          ret = ops->write(arg, true, sequence[3 + i]);
          if (ret < 0)
            {
              return ret;
            }
        }

      if (delay > 0)
        {
          up_mdelay(delay);
        }

      sequence += count + 2;
    }

  return OK;
}

int ili9488_rgb_initialize(FAR const struct ili9488_rgb_ops_s *ops,
                           FAR void *arg)
{
  int ret;

  if (ops == NULL || ops->reset == NULL || ops->write == NULL)
    {
      return -EINVAL;
    }

  /* Preserve the validated T5AI electrical reset waveform. */

  ret = ops->reset(arg, false);
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(1);
  ret = ops->reset(arg, true);
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(100);
  ret = ops->reset(arg, false);
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(100);
  ret = ops->reset(arg, true);
  if (ret < 0)
    {
      return ret;
    }

  up_mdelay(100);
  return ili9488_rgb_run_sequence(ops, arg);
}

#endif /* CONFIG_LCD_ILI9488_RGB */
