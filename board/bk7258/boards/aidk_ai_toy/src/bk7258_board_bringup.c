/****************************************************************************
 * board/bk7258/boards/aidk_ai_toy/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK board hook: UART0 console, MIC, SD NAND and PA speaker bindings.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_board_binding.h>
#include <arch/chip/bk7258_gpio.h>

static int bk7258_aidk_mic_initialize(void)
{
  /* MIC1/MIC2 are fixed analog routes; no runtime pin mux required. */

  return OK;
}

static const struct bk7258_mic_config_s g_bk7258_aidk_mic_config =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_mic_config_s),
  .channels = 2,
  .flags = BK7258_MIC_BINDING_MIC1 | BK7258_MIC_BINDING_MIC2,
  .variant_name = BK7258_BOARD_VARIANT_NAME,
};

static const struct bk7258_mic_binding_s g_bk7258_aidk_mic_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_mic_binding_s),
  .config = &g_bk7258_aidk_mic_config,
  .initialize = bk7258_aidk_mic_initialize,
};

#ifdef CONFIG_BK7258_SDIO
extern int bk7258_board_sdio_initialize(bool widebus);
extern bool bk7258_board_sdio_card_present(void);

static const struct bk7258_sdio_config_s g_bk7258_aidk_sdio_config =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_sdio_config_s),
  .card_detect_available = false,
  .media_poll_ms = BK7258_BOARD_SDIO_MEDIA_POLL_MS,
};

static const struct bk7258_sdio_binding_s g_bk7258_aidk_sdio_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_sdio_binding_s),
  .config = &g_bk7258_aidk_sdio_config,
  .initialize = bk7258_board_sdio_initialize,
  .card_present = bk7258_board_sdio_card_present,
};
#endif

static const struct bk7258_gpio_config_s g_bk7258_aidk_gpio_config =
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

static const struct bk7258_board_binding_s g_bk7258_aidk_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_board_binding_s),
  .mic = &g_bk7258_aidk_mic_binding,
#ifdef CONFIG_BK7258_SDIO
  .sdio = &g_bk7258_aidk_sdio_binding,
#else
  .sdio = NULL,
#endif
#ifdef CONFIG_BK7258_AUD
  .audio = &g_bk7258_board_audio_binding,
#else
  .audio = NULL,
#endif
  .early_initialize = bk7258_board_early_initialize,
  .devices_initialize = bk7258_board_devices_initialize,
};

FAR const struct bk7258_gpio_config_s *bk7258_board_gpio_config(void)
{
  return &g_bk7258_aidk_gpio_config;
}

const struct bk7258_board_binding_s *bk7258_board_get_binding(void)
{
  return &g_bk7258_aidk_binding;
}

int bk7258_board_early_initialize(void)
{
  return OK;
}

int bk7258_board_devices_initialize(void)
{
  return OK;
}
