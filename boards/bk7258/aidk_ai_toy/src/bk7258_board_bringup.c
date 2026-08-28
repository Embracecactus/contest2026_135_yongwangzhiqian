/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK board hook: UART0 console, MIC, SD NAND, PA speaker and the bounded
 * GC2145 Phase 0 identity probe.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_gpio.h>

#ifdef CONFIG_BK7258_AP_CORE
#ifdef CONFIG_BK7258_AIDK_CAMERA_PHASE0
extern int bk7258_aidk_camera_phase0_probe(void);
#endif

static const struct bk7258_mic_config_s g_bk7258_aidk_mic_config =
{
  /* MIC1 is the primary microphone; MIC2 carries the speaker loopback used
   * as the AEC reference.  The input flag names are SoC ADC input names, not
   * a claim that two physical microphones are fitted.
   */

  .channels = BK7258_BOARD_CAPTURE_CHANNELS,
  .flags = BK7258_MIC_INPUT_MIC1 | BK7258_MIC_INPUT_MIC2,
  .variant_name = BK7258_BOARD_VARIANT_NAME,
};

#ifdef CONFIG_BK7258_SDIO
extern int bk7258_board_sdio_initialize(bool widebus);
extern bool bk7258_board_sdio_card_present(void);

static const struct bk7258_sdio_board_s g_bk7258_aidk_sdio =
{
  .card_detect_available = false,
  .media_poll_ms = BK7258_BOARD_SDIO_MEDIA_POLL_MS,
  .initialize = bk7258_board_sdio_initialize,
  .card_present = bk7258_board_sdio_card_present,
};
#endif
#endif /* CONFIG_BK7258_AP_CORE */

const struct bk7258_gpio_config_s g_bk7258_board_gpio_config =
{
  .name                    = BK7258_BOARD_VARIANT_NAME,
  .user_led_gpio           = BK7258_BOARD_USER_LED_GPIO,
  .user_led_active_high    = BK7258_BOARD_USER_LED_ACTIVE_HIGH,
  .user_led_console_shared = BK7258_BOARD_USER_LED_CONSOLE_SHARED,
  .user_button_gpio        = BK7258_BOARD_USER_BUTTON_GPIO,
  .user_button_active_low  = BK7258_BOARD_USER_BUTTON_ACTIVE_LOW,
};

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_board_ap_initialize(void)
{
  FAR const struct bk7258_aud_board_s *audio = NULL;
  FAR const struct bk7258_sdio_board_s *sdio = NULL;
  int ret;

#ifdef CONFIG_BK7258_AUD
  audio = &g_bk7258_board_audio;
#endif

#ifdef CONFIG_BK7258_SDIO
  sdio = &g_bk7258_aidk_sdio;
#endif

  ret = bk7258_board_ap_controllers_initialize(
          &g_bk7258_aidk_mic_config, audio);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_board_ap_buses_initialize(NULL, sdio);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_AIDK_CAMERA_PHASE0
  ret = bk7258_aidk_camera_phase0_probe();
  if (ret < 0)
    {
      return ret;
    }
#endif

  return bk7258_board_ap_finalize_initialize();
}
#endif /* CONFIG_BK7258_AP_CORE */
