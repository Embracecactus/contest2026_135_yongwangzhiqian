/****************************************************************************
 * board/bk7258/boards/t5ai_core/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5AI-Core-specific peripheral registration hooks.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_gpio.h>
#ifdef CONFIG_BK7258_ETH
#  include <arch/chip/bk7258_eth.h>
#endif

#ifdef CONFIG_BK7258_AP_CORE
static const struct bk7258_mic_config_s g_bk7258_t5ai_core_mic_config =
{
  .channels = 1,
  .flags = BK7258_MIC_INPUT_MIC1,
  .variant_name = "T5AI-Core",
};
#endif
/****************************************************************************
 * Private Data
 ****************************************************************************/

const struct bk7258_gpio_config_s g_bk7258_board_gpio_config =
{
  .name                    = BK7258_BOARD_VARIANT_NAME,
  .user_led_gpio           = BK7258_BOARD_USER_LED_GPIO,
  .user_led_active_high    = BK7258_BOARD_USER_LED_ACTIVE_HIGH,
  .user_led_console_shared = BK7258_BOARD_USER_LED_CONSOLE_SHARED,
  .user_button_gpio        = BK7258_BOARD_USER_BUTTON_GPIO,
  .user_button_active_low  = BK7258_BOARD_USER_BUTTON_ACTIVE_LOW,
};

#ifdef CONFIG_BK7258_ETH
static const struct bk7258_eth_board_s g_bk7258_t5ai_core_eth_config =
{
  .name      = "T5AI-Core",
  .pin_group = BK7258_ETH_PIN_GROUP0,
  .mac_addr  = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 },
};
#endif /* CONFIG_BK7258_ETH */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_board_ap_initialize(void)
{
  FAR const struct bk7258_aud_board_s *audio = NULL;
  int ret;

#ifdef CONFIG_BK7258_AUD
  audio = &g_bk7258_board_audio;
#endif

  ret = bk7258_board_ap_controllers_initialize(
          &g_bk7258_t5ai_core_mic_config, audio);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_board_ap_buses_initialize(NULL, NULL);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_ETH
  ret = bk7258_eth_initialize(&g_bk7258_t5ai_core_eth_config);
  if (ret < 0)
    {
      return ret;
    }
#endif

  return bk7258_board_ap_finalize_initialize();
}
#endif /* CONFIG_BK7258_AP_CORE */
