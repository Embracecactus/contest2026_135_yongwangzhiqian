/****************************************************************************
 * board/bk7258/boards/t5ai_core/src/bk7258_t5ai_core_audio.c
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

#include <driver/gpio.h>

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);

static bool g_bk7258_speaker_initialized;

int bk7258_board_speaker_initialize(void)
{
  gpio_id_t pin = (gpio_id_t)BK7258_BOARD_SPEAKER_CONTROL_GPIO;
  bk_err_t error;

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

#if BK7258_BOARD_SPEAKER_ACTIVE_HIGH
  error = bk_gpio_set_output_low(pin);
#else
  error = bk_gpio_set_output_high(pin);
#endif
  if (error != BK_OK)
    {
      return -EIO;
    }

  g_bk7258_speaker_initialized = true;
  return OK;
}

int bk7258_board_speaker_set(bool enable)
{
  gpio_id_t pin = (gpio_id_t)BK7258_BOARD_SPEAKER_CONTROL_GPIO;
  bool high = enable ? BK7258_BOARD_SPEAKER_ACTIVE_HIGH :
                       !BK7258_BOARD_SPEAKER_ACTIVE_HIGH;
  int ret;

  ret = bk7258_board_speaker_initialize();
  if (ret < 0)
    {
      return ret;
    }

  return (high ? bk_gpio_set_output_high(pin) :
                 bk_gpio_set_output_low(pin)) == BK_OK ? OK : -EIO;
}

bool bk7258_board_speaker_is_enabled(void)
{
  bool high;

  if (!g_bk7258_speaker_initialized)
    {
      return false;
    }

  high = bk_gpio_get_output(
    (gpio_id_t)BK7258_BOARD_SPEAKER_CONTROL_GPIO);
  return high == (BK7258_BOARD_SPEAKER_ACTIVE_HIGH != 0);
}

#endif /* CONFIG_BK7258_AUD */
