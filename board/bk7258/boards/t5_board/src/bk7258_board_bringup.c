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

#ifdef CONFIG_BK7258_AUD_LIFECYCLE_VALIDATION
#  include <arch/chip/bk7258_aud.h>
#endif

#ifdef CONFIG_BK7258_LCD
#  include <arch/chip/bk7258_lcd.h>
#endif

#ifdef CONFIG_BK7258_T5_BOARD_SARADC_KEY_VALIDATION
#  include <arch/chip/bk7258_saradc.h>

_Static_assert(BK7258_BOARD_ADC_KEY_GPIO == 12,
               "T5-Board ADC key must remain on P12");
_Static_assert(BK7258_BOARD_ADC_KEY_SARADC_CHAN ==
               CONFIG_BK7258_SARADC_CHAN,
               "T5-Board ADC key requires SARADC channel 14");
_Static_assert(BK7258_BOARD_ADC_KEY_ACTIVE_LOW == 1,
               "T5-Board ADC key must remain active-low");

static const struct bk7258_saradc_validation_config_s
g_bk7258_t5_board_adc_key_validation =
{
  .devpath = CONFIG_BK7258_SARADC_DEVNAME,
  .binding_id = BK7258_BOARD_ADC_KEY_BINDING_ID,
  .expected_channel = BK7258_BOARD_ADC_KEY_SARADC_CHAN,
  .active_direction = BK7258_SARADC_ACTIVE_LOW,
  .samples_per_phase = 64,
  .initial_delay_ms = 3000,
  .settle_ms = 50,
  .poll_interval_ms = 20,
  .phase_timeout_ms = 30000,
  .transition_confirm_samples = 3,
  .minimum_delta_raw = 256,
  .minimum_delta_permille = 800,
  .minimum_release_tolerance_raw = 64,
  .release_tolerance_permille = 100,
  .maximum_noise_permille = 100,
};
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

#ifdef CONFIG_BK7258_AUD_LIFECYCLE_VALIDATION
  ret = bk7258_aud_validation_start();
  if (ret < 0)
    {
      _err("ERROR: T5-Board speaker validation worker failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_T5_BOARD_SARADC_KEY_VALIDATION
  ret = bk7258_saradc_validation_start(
          &g_bk7258_t5_board_adc_key_validation);
  if (ret < 0)
    {
      _err("ERROR: T5-Board ADC-key validation worker failed: %d\n", ret);
      return ret;
    }
#endif

  (void)ret;
  return OK;
}
