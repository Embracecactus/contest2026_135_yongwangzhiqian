/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_buttons.c
 *
 * AIDK AI Toy physical, polled three-button binding.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_BUTTONS

#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/compiler.h>
#include <nuttx/input/buttons.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_pinmux.h>

static bool g_aidk_buttons_registered;

static btn_buttonset_t aidk_buttons_supported(
  FAR const struct btn_lowerhalf_s *lower)
{
  (void)lower;
  return BUTTON_K1 | BUTTON_K2 | BUTTON_K3;
}

static btn_buttonset_t aidk_buttons_read(
  FAR const struct btn_lowerhalf_s *lower)
{
  static const uint8_t pins[BOARD_NBUTTONS] =
    {
      BK7258_BOARD_PIN_KEY1,
      BK7258_BOARD_PIN_KEY2,
      BK7258_BOARD_PIN_KEY3,
    };
  btn_buttonset_t buttons = 0;
  bool high;
  int ret;
  uint8_t index;

  (void)lower;
  for (index = 0; index < BOARD_NBUTTONS; index++)
    {
      ret = bk7258_gpio_read_input(pins[index], &high);
      if (ret == 0 && !high)
        {
          buttons |= 1u << index;
        }
    }

  return buttons;
}

static void aidk_buttons_enable(FAR const struct btn_lowerhalf_s *lower,
                                btn_buttonset_t press,
                                btn_buttonset_t release,
                                btn_handler_t handler, FAR void *arg)
{
  /* This board is deliberately read-polled.  Keep a non-NULL lower-half
   * callback for the standard upper half, but never synthesize IRQ events.
   * BTNIOC_REGISTER and poll wakeups therefore have no notification source.
   */
  (void)lower;
  (void)press;
  (void)release;
  (void)handler;
  (void)arg;
}

static const struct btn_lowerhalf_s g_aidk_buttons_lower =
{
  .bl_supported = aidk_buttons_supported,
  .bl_buttons = aidk_buttons_read,
  .bl_enable = aidk_buttons_enable,
  .bl_write = NULL,
};

static int aidk_buttons_configure(void)
{
  static const uint8_t pins[BOARD_NBUTTONS] =
    {
      BK7258_BOARD_PIN_KEY1,
      BK7258_BOARD_PIN_KEY2,
      BK7258_BOARD_PIN_KEY3,
    };
  int ret;
  uint8_t index;

  for (index = 0; index < BOARD_NBUTTONS; index++)
    {
      ret = bk7258_gpio_configure_input(pins[index], BK7258_GPIO_PULL_UP);
      if (ret < 0)
        {
          syslog(LOG_ERR, "AIDK buttons: configure P%u failed: %d\n",
                 pins[index], ret);
          return ret;
        }
    }

  return 0;
}

int bk7258_board_buttons_initialize(void)
{
  int ret;

  if (g_aidk_buttons_registered)
    {
      return 0;
    }

  ret = aidk_buttons_configure();
  if (ret < 0)
    {
      return ret;
    }

  ret = btn_register("/dev/buttons", &g_aidk_buttons_lower);
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK buttons: register failed: %d\n", ret);
      return ret;
    }

  g_aidk_buttons_registered = true;
  return 0;
}

uint32_t board_button_initialize(void)
{
  return bk7258_board_buttons_initialize() < 0 ? 0 : BOARD_NBUTTONS;
}

uint32_t board_buttons(void)
{
  return aidk_buttons_read(&g_aidk_buttons_lower);
}

#endif /* CONFIG_BK7258_AIDK_BUTTONS */
