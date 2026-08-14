/****************************************************************************
 * board/bk7258/boards/t5_board/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board-specific peripheral registration hooks.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <debug.h>

#include <arch/board/board.h>

#ifdef CONFIG_BK7258_LCD
#  include <arch/chip/bk7258_lcd.h>
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_board_early_initialize(void)
{
#ifdef CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION
  return bk7258_t5_board_rgb_lcd_backlight_validation_initialize();
#else
  return OK;
#endif
}

int bk7258_board_devices_initialize(void)
{
  int ret = OK;

#ifdef CONFIG_BK7258_LCD
  ret = bk7258_lcd_initialize();
  if (ret < 0)
    {
      lcderr("ERROR: LCD framebuffer registration failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_GT1151
  ret = bk7258_board_gt1151_initialize();
  if (ret < 0)
    {
      ierr("ERROR: GT1151 registration failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA
  ret = bk7258_t5_board_camera_initialize();
  if (ret < 0)
    {
      verr("ERROR: T5-Board camera registration failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_T5_BOARD_TF_VALIDATION
  ret = bk7258_t5_board_tf_validation_initialize();
  if (ret < 0)
    {
      _err("ERROR: T5-Board TF validation worker failed: %d\n", ret);
      return ret;
    }
#endif

  (void)ret;
  return OK;
}
