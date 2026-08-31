/****************************************************************************
 * chips/bk7258/
 * bk7258_gpio_foundation_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Manual non-IRQ GPIO foundation test for the selected board's LED and key.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_gpio.h>

#include <driver/gpio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_GPIOC0_LED_OFF_US            250000u
#define BK7258_GPIOC0_LED_ON_US             500000u
#define BK7258_GPIOC0_LED_CYCLES            2u
#define BK7258_GPIOC0_KEY_WAIT_STEP_US      20000u
#define BK7258_GPIOC0_KEY_WAIT_STEPS        500u

#define BK7258_GPIOC0_RESERVED(pin) \
  ((pin) == GPIO_0 || (pin) == GPIO_1 || \
   (pin) == GPIO_10 || (pin) == GPIO_11)

/****************************************************************************
 * Compile-time Invariants
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_bk7258_gpioc0_running;
static FAR const struct bk7258_gpio_config_s *g_bk7258_gpioc0_config;
static gpio_id_t g_bk7258_gpioc0_led;
static gpio_id_t g_bk7258_gpioc0_key;

static const gpio_config_t g_bk7258_gpioc0_led_config =
{
  .io_mode = GPIO_OUTPUT_ENABLE,
  .pull_mode = GPIO_PULL_DISABLE,
  .func_mode = GPIO_SECOND_FUNC_DISABLE,
};

static gpio_config_t g_bk7258_gpioc0_key_config =
{
  .io_mode = GPIO_INPUT_ENABLE,
  .pull_mode = GPIO_PULL_DISABLE,
  .func_mode = GPIO_SECOND_FUNC_DISABLE,
};

#define BK7258_GPIOC0_LED  (g_bk7258_gpioc0_led)
#define BK7258_GPIOC0_KEY  (g_bk7258_gpioc0_key)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_gpioc0_claim(void)
{
  irqstate_t flags;
  bool claimed;

  flags = enter_critical_section();
  claimed = !g_bk7258_gpioc0_running;
  if (claimed)
    {
      g_bk7258_gpioc0_running = true;
    }

  leave_critical_section(flags);
  return claimed;
}

static void bk7258_gpioc0_release(void)
{
  irqstate_t flags;

  flags = enter_critical_section();
  g_bk7258_gpioc0_running = false;
  leave_critical_section(flags);
}

static int bk7258_gpioc0_result(const char *operation, gpio_id_t pin,
                                bk_err_t error)
{
  if (error == BK_OK)
    {
      return 0;
    }

  if (error == BK_ERR_GPIO_INTERNAL_USED)
    {
      printf("bkgpioc0: FAIL %s P%u is internally owned (%d)\n",
             operation, (unsigned int)pin, (int)error);
      return -EBUSY;
    }

  printf("bkgpioc0: FAIL %s P%u error=%d\n",
         operation, (unsigned int)pin, (int)error);
  return -EIO;
}

static bk_err_t bk7258_gpioc0_set_led(bool on)
{
  bool level = on ? g_bk7258_gpioc0_config->user_led_active_high :
                    !g_bk7258_gpioc0_config->user_led_active_high;

  return level ? bk_gpio_set_output_high(BK7258_GPIOC0_LED) :
                 bk_gpio_set_output_low(BK7258_GPIOC0_LED);
}

static void bk7258_gpioc0_record_key(bool level, bool *seen_high,
                                     bool *seen_low)
{
  bool pressed = level !=
                 (g_bk7258_gpioc0_config->user_button_active_low != 0);

  if (level)
    {
      *seen_high = true;
    }
  else
    {
      *seen_low = true;
    }

  printf("bkgpioc0: KEY P%u raw=%u %s\n",
         (unsigned int)BK7258_GPIOC0_KEY, level ? 1u : 0u,
         pressed ? "PRESSED" : "RELEASED");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_gpio_foundation_test(
  FAR const struct bk7258_gpio_config_s *config)
{
  uint32_t saved_led;
  uint32_t saved_key;
  bool led_configured = false;
  bool key_configured = false;
  bool seen_high = false;
  bool seen_low = false;
  bool level;
  bool last_level;
  unsigned int cycle;
  unsigned int step;
  bk_err_t error;
  int cleanup_result;
  int result = -EIO;

  if (!bk7258_gpioc0_claim())
    {
      printf("bkgpioc0: FAIL test already running\n");
      return -EBUSY;
    }

  g_bk7258_gpioc0_config = config;
  if (g_bk7258_gpioc0_config == NULL ||
      g_bk7258_gpioc0_config->name == NULL ||
      g_bk7258_gpioc0_config->user_led_gpio >= GPIO_NUM ||
      g_bk7258_gpioc0_config->user_button_gpio >= GPIO_NUM ||
      g_bk7258_gpioc0_config->user_led_gpio ==
        g_bk7258_gpioc0_config->user_button_gpio ||
      BK7258_GPIOC0_RESERVED(g_bk7258_gpioc0_config->user_led_gpio) ||
      BK7258_GPIOC0_RESERVED(g_bk7258_gpioc0_config->user_button_gpio))
    {
      printf("bkgpioc0: FAIL no valid GPIO board configuration\n");
      bk7258_gpioc0_release();
      return -ENODEV;
    }

  BK7258_GPIOC0_LED =
    (gpio_id_t)g_bk7258_gpioc0_config->user_led_gpio;
  BK7258_GPIOC0_KEY =
    (gpio_id_t)g_bk7258_gpioc0_config->user_button_gpio;
  g_bk7258_gpioc0_key_config.pull_mode =
    g_bk7258_gpioc0_config->user_button_active_low ? GPIO_PULL_UP_EN :
                                                     GPIO_PULL_DOWN_EN;

  printf("bkgpioc0: BEGIN board=%s LED=P%u active-%s "
         "KEY=P%u active-%s\n",
         g_bk7258_gpioc0_config->name,
         (unsigned int)BK7258_GPIOC0_LED,
         g_bk7258_gpioc0_config->user_led_active_high ? "high" : "low",
         (unsigned int)BK7258_GPIOC0_KEY,
         g_bk7258_gpioc0_config->user_button_active_low ? "low" : "high");

  error = bk_gpio_driver_init();
  result = bk7258_gpioc0_result("driver init", BK7258_GPIOC0_LED,
                                error);
  if (result < 0)
    {
      goto out;
    }

  saved_led = bk_gpio_get_value(BK7258_GPIOC0_LED);
  saved_key = bk_gpio_get_value(BK7258_GPIOC0_KEY);

  error = bk_gpio_set_config(BK7258_GPIOC0_LED,
                             &g_bk7258_gpioc0_led_config);
  result = bk7258_gpioc0_result("configure output", BK7258_GPIOC0_LED,
                                error);
  if (result < 0)
    {
      goto out;
    }

  led_configured = true;

  error = bk_gpio_set_config(BK7258_GPIOC0_KEY,
                             &g_bk7258_gpioc0_key_config);
  result = bk7258_gpioc0_result("configure input pull",
                                BK7258_GPIOC0_KEY, error);
  if (result < 0)
    {
      goto out;
    }

  key_configured = true;

  printf("bkgpioc0: visually confirm two LED pulses\n");
  for (cycle = 0; cycle < BK7258_GPIOC0_LED_CYCLES; cycle++)
    {
      error = bk7258_gpioc0_set_led(false);
      result = bk7258_gpioc0_result("turn off", BK7258_GPIOC0_LED, error);
      if (result < 0)
        {
          goto out;
        }

      printf("bkgpioc0: LED P%u OFF\n",
             (unsigned int)BK7258_GPIOC0_LED);
      usleep(BK7258_GPIOC0_LED_OFF_US);

      error = bk7258_gpioc0_set_led(true);
      result = bk7258_gpioc0_result("turn on", BK7258_GPIOC0_LED, error);
      if (result < 0)
        {
          goto out;
        }

      printf("bkgpioc0: LED P%u ON\n",
             (unsigned int)BK7258_GPIOC0_LED);
      usleep(BK7258_GPIOC0_LED_ON_US);
    }

  error = bk7258_gpioc0_set_led(false);
  result = bk7258_gpioc0_result("turn off", BK7258_GPIOC0_LED, error);
  if (result < 0)
    {
      goto out;
    }

  printf("bkgpioc0: LED P%u OFF\n",
         (unsigned int)BK7258_GPIOC0_LED);
  printf("bkgpioc0: toggle USERKEY within 10 seconds\n");

  last_level = bk_gpio_get_input(BK7258_GPIOC0_KEY);
  bk7258_gpioc0_record_key(last_level, &seen_high, &seen_low);

  for (step = 0;
       step < BK7258_GPIOC0_KEY_WAIT_STEPS && !(seen_high && seen_low);
       step++)
    {
      usleep(BK7258_GPIOC0_KEY_WAIT_STEP_US);
      level = bk_gpio_get_input(BK7258_GPIOC0_KEY);
      if (level != last_level)
        {
          last_level = level;
          bk7258_gpioc0_record_key(level, &seen_high, &seen_low);
        }
    }

  if (!(seen_high && seen_low))
    {
      printf("bkgpioc0: FAIL key states high=%u low=%u\n",
             seen_high ? 1u : 0u, seen_low ? 1u : 0u);
      result = -ETIMEDOUT;
      goto out;
    }

  result = 0;

out:
  if (led_configured)
    {
      error = bk7258_gpioc0_set_led(false);
      cleanup_result = bk7258_gpioc0_result("leave off",
                                            BK7258_GPIOC0_LED, error);
      if (result == 0 && cleanup_result < 0)
        {
          result = cleanup_result;
        }
    }

  if (key_configured)
    {
      error = bk_gpio_set_value(BK7258_GPIOC0_KEY, saved_key);
      cleanup_result = bk7258_gpioc0_result("restore", BK7258_GPIOC0_KEY,
                                            error);
      if (result == 0 && cleanup_result < 0)
        {
          result = cleanup_result;
        }
    }

  if (led_configured)
    {
      error = bk_gpio_set_value(BK7258_GPIOC0_LED, saved_led);
      cleanup_result = bk7258_gpioc0_result("restore", BK7258_GPIOC0_LED,
                                            error);
      if (result == 0 && cleanup_result < 0)
        {
          result = cleanup_result;
        }
    }

  if (result == 0)
    {
      printf("bkgpioc0: restore OK\n");
      printf("bkgpioc0: PASS\n");
    }
  else
    {
      printf("bkgpioc0: FAIL result=%d\n", result);
    }

  bk7258_gpioc0_release();
  return result;
}
