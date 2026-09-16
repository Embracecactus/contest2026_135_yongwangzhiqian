/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_motor.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CN10/P9 binding for the standard NuttX GPIO force-feedback lower half.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include <nuttx/input/gpio_ff.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_pinmux.h>

static FAR struct ff_lowerhalf_s *g_aidk_motor;

static int aidk_motor_output(FAR void *arg, bool enable)
{
  (void)arg;

  /* The timer and capture interlock must be able to stop the transistor
   * directly in interrupt context.  P9 is exclusively configured here.
   */

  return bk7258_gpio_fast_write(BK7258_BOARD_PIN_MOTOR,
             enable == (BK7258_BOARD_MOTOR_ACTIVE_HIGH != 0));
}

static int aidk_motor_power(FAR void *arg, bool enable)
{
  (void)arg;

  /* The SDK aggregates independent consumers of P52.  Never drive the
   * common rail directly when releasing this motor's vote.
   */

  return bk7258_shared_rail_vote(BK7258_SHARED_RAIL_MOTOR,
                                 BK7258_BOARD_PIN_LDO33_EN, enable);
}

static const struct gpio_ff_config_s g_aidk_motor_config =
{
  .set_output = aidk_motor_output,
  .set_power = aidk_motor_power,
  .max_on_ms = BK7258_BOARD_MOTOR_MAX_ON_MS,
  .min_off_ms = BK7258_BOARD_MOTOR_MIN_OFF_MS,
};

int bk7258_aidk_motor_initialize(void)
{
  int ret;

  if (g_aidk_motor != NULL)
    {
      return OK;
    }

  ret = bk7258_gpio_configure_output(BK7258_BOARD_PIN_MOTOR,
             BK7258_BOARD_MOTOR_ACTIVE_HIGH == 0, BK7258_GPIO_DRIVE_0);
  if (ret < 0)
    {
      return ret;
    }

  ret = gpio_ff_register("/dev/input_ff0", &g_aidk_motor_config,
                          &g_aidk_motor);
  if (ret < 0)
    {
      (void)aidk_motor_output(NULL, false);
    }

  return ret;
}

int bk7258_aidk_motor_capture_quiet(bool quiet)
{
  /* MIC starts only after board initialization has registered this owner.
   * A missing owner fails capture closed instead of bypassing the interlock.
   */

  if (g_aidk_motor == NULL)
    {
      return -ENODEV;
    }

  return gpio_ff_inhibit(g_aidk_motor, quiet);
}
