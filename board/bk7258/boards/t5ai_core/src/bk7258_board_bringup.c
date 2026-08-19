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
#include <arch/chip/bk7258_board_binding.h>
#include <arch/chip/bk7258_gpio.h>
#ifdef CONFIG_BK7258_ETH
#  include <arch/chip/bk7258_eth.h>
#endif

static int bk7258_t5ai_core_mic_initialize(void)
{
  /* T5AI-Core's microphone route is fixed by the board schematic and needs
   * no runtime pin-mux operation.  Keep the callback explicit so a future
   * revision can add a power or mux transition without changing chip code.
   */

  return OK;
}

static const struct bk7258_mic_config_s g_bk7258_t5ai_core_mic_config =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_mic_config_s),
  .channels = 1,
  .flags = BK7258_MIC_BINDING_MIC1,
  .variant_name = "T5AI-Core",
};

static const struct bk7258_mic_binding_s g_bk7258_t5ai_core_mic_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_mic_binding_s),
  .config = &g_bk7258_t5ai_core_mic_config,
  .initialize = bk7258_t5ai_core_mic_initialize,
};

static const struct bk7258_board_binding_s g_bk7258_t5ai_core_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_board_binding_s),
  .mic = &g_bk7258_t5ai_core_mic_binding,
  .sdio = NULL,
#ifdef CONFIG_BK7258_AUD
  .audio = &g_bk7258_board_audio_binding,
#else
  .audio = NULL,
#endif
  .early_initialize = bk7258_board_early_initialize,
  .devices_initialize = bk7258_board_devices_initialize,
};

const struct bk7258_board_binding_s *bk7258_board_get_binding(void)
{
  return &g_bk7258_t5ai_core_binding;
}
/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bk7258_gpio_config_s g_bk7258_t5ai_core_gpio_config =
{
  .version                 = BK7258_GPIO_BINDING_VERSION,
  .size                    = sizeof(struct bk7258_gpio_config_s),
  .name                    = BK7258_BOARD_VARIANT_NAME,
  .user_led_gpio           = BK7258_BOARD_USER_LED_GPIO,
  .user_led_active_high    = BK7258_BOARD_USER_LED_ACTIVE_HIGH,
  .user_led_console_shared = BK7258_BOARD_USER_LED_CONSOLE_SHARED,
  .user_button_gpio        = BK7258_BOARD_USER_BUTTON_GPIO,
  .user_button_active_low  = BK7258_BOARD_USER_BUTTON_ACTIVE_LOW,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR const struct bk7258_gpio_config_s *bk7258_board_gpio_config(void)
{
  return &g_bk7258_t5ai_core_gpio_config;
}

int bk7258_board_early_initialize(void)
{
  return OK;
}

int bk7258_board_devices_initialize(void)
{
  return OK;
}

#ifdef CONFIG_BK7258_ETH
static const struct bk7258_eth_board_s g_bk7258_t5ai_core_eth_config =
{
  .name      = "T5AI-Core",
  .pin_group = BK7258_ETH_PIN_GROUP0,
  .mac_addr  = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 },
};

FAR const struct bk7258_eth_board_s *bk7258_board_eth_config(void)
{
  return &g_bk7258_t5ai_core_eth_config;
}
#endif /* CONFIG_BK7258_ETH */
