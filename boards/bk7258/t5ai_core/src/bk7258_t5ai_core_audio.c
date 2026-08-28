/****************************************************************************
 * boards/bk7258/t5ai_core/src/bk7258_t5ai_core_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5AI-Core V1.0.1 speaker power-amplifier binding.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AUD

#include <errno.h>
#include <stdbool.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_aud.h>

#include <driver/gpio.h>

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);

static bool g_bk7258_speaker_initialized;

static const struct bk7258_aud_config_s g_bk7258_t5ai_audio_config =
{
  .variant_name = BK7258_BOARD_VARIANT_NAME,
  .speaker_control_gpio = BK7258_BOARD_SPEAKER_CONTROL_GPIO,
  .speaker_on_delay_ms = BK7258_BOARD_SPEAKER_ON_DELAY_MS,
  .speaker_off_delay_ms = BK7258_BOARD_SPEAKER_OFF_DELAY_MS,
  .speaker_active_high = BK7258_BOARD_SPEAKER_ACTIVE_HIGH != 0,
};

static int bk7258_t5ai_audio_initialize(
  FAR const struct bk7258_aud_config_s *config)
{
  gpio_id_t pin;
  bk_err_t error;

  if (config == NULL)
    {
      return -EINVAL;
    }

  pin = (gpio_id_t)config->speaker_control_gpio;
  if (g_bk7258_speaker_initialized)
    {
      return OK;
    }

  error = bk_gpio_driver_init();
  if (error != BK_OK)
    {
      return -EIO;
    }

  (void)gpio_dev_unmap(pin);

  error = bk_gpio_enable_output(pin);
  if (error != BK_OK)
    {
      return -EIO;
    }

  error = config->speaker_active_high ? bk_gpio_set_output_low(pin) :
                                         bk_gpio_set_output_high(pin);
  if (error != BK_OK)
    {
      return -EIO;
    }

  g_bk7258_speaker_initialized = true;
  return OK;
}

static int bk7258_t5ai_audio_set(
  FAR const struct bk7258_aud_config_s *config, bool enable)
{
  gpio_id_t pin;
  bool high;
  int ret;

  if (config == NULL)
    {
      return -EINVAL;
    }

  pin = (gpio_id_t)config->speaker_control_gpio;
  high = enable ? config->speaker_active_high : !config->speaker_active_high;
  ret = bk7258_t5ai_audio_initialize(config);
  if (ret < 0)
    {
      return ret;
    }

  return (high ? bk_gpio_set_output_high(pin) :
                 bk_gpio_set_output_low(pin)) == BK_OK ? OK : -EIO;
}

static bool bk7258_t5ai_audio_is_enabled(
  FAR const struct bk7258_aud_config_s *config)
{
  bool high;

  if (config == NULL || !g_bk7258_speaker_initialized)
    {
      return false;
    }

  high = bk_gpio_get_output((gpio_id_t)config->speaker_control_gpio);
  return high == config->speaker_active_high;
}

const struct bk7258_aud_board_s g_bk7258_board_audio =
{
  .config = &g_bk7258_t5ai_audio_config,
  .initialize = bk7258_t5ai_audio_initialize,
  .set = bk7258_t5ai_audio_set,
  .is_enabled = bk7258_t5ai_audio_is_enabled,
};

#endif /* CONFIG_BK7258_AUD */
