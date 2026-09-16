/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_motor.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CN10/P9 motor binding for the Shaniu product haptic adapter.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_pinmux.h>

static mutex_t g_aidk_motor_lock = NXMUTEX_INITIALIZER;
static bool g_aidk_motor_ready;
static bool g_aidk_motor_active;
static bool g_aidk_motor_quiet;
static bool g_aidk_motor_stopped_once;
static clock_t g_aidk_motor_stopped_at;

static int aidk_motor_set_locked(bool enable)
{
  int ret;

  if (!g_aidk_motor_ready)
    {
      return -ENODEV;
    }

  if (enable)
    {
      if (g_aidk_motor_quiet)
        {
          return -EBUSY;
        }

      if (g_aidk_motor_active)
        {
          return OK;
        }

      if (g_aidk_motor_stopped_once &&
          (clock_t)(clock_systime_ticks() - g_aidk_motor_stopped_at) <
          MSEC2TICK(BK7258_BOARD_MOTOR_MIN_OFF_MS))
        {
          return -EAGAIN;
        }

      ret = bk7258_shared_rail_vote(BK7258_SHARED_RAIL_MOTOR,
                                    BK7258_BOARD_PIN_LDO33_EN, true);
      if (ret < 0)
        {
          return ret;
        }

      ret = bk7258_gpio_write(BK7258_BOARD_PIN_MOTOR,
                              BK7258_BOARD_MOTOR_ACTIVE_HIGH != 0);
      if (ret < 0)
        {
          (void)bk7258_shared_rail_vote(BK7258_SHARED_RAIL_MOTOR,
                                        BK7258_BOARD_PIN_LDO33_EN, false);
          return ret;
        }

      g_aidk_motor_active = true;
      return OK;
    }

  ret = bk7258_gpio_write(BK7258_BOARD_PIN_MOTOR,
                          BK7258_BOARD_MOTOR_ACTIVE_HIGH == 0);
  if (g_aidk_motor_active)
    {
      int power_ret = bk7258_shared_rail_vote(BK7258_SHARED_RAIL_MOTOR,
                                              BK7258_BOARD_PIN_LDO33_EN,
                                              false);

      if (ret >= 0)
        {
          g_aidk_motor_active = false;
          g_aidk_motor_stopped_once = true;
          g_aidk_motor_stopped_at = clock_systime_ticks();
          ret = power_ret;
        }
    }

  return ret;
}

int bk7258_aidk_motor_initialize(void)
{
  int ret = nxmutex_lock(&g_aidk_motor_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (g_aidk_motor_ready)
    {
      nxmutex_unlock(&g_aidk_motor_lock);
      return OK;
    }

  ret = bk7258_gpio_configure_output(BK7258_BOARD_PIN_MOTOR,
             BK7258_BOARD_MOTOR_ACTIVE_HIGH == 0, BK7258_GPIO_DRIVE_0);
  if (ret >= 0)
    {
      g_aidk_motor_ready = true;
    }

  nxmutex_unlock(&g_aidk_motor_lock);
  return ret;
}

bool bk7258_aidk_motor_ready(void)
{
  bool ready = false;

  if (nxmutex_lock(&g_aidk_motor_lock) >= 0)
    {
      ready = g_aidk_motor_ready;
      nxmutex_unlock(&g_aidk_motor_lock);
    }

  return ready;
}

int bk7258_aidk_motor_set(bool enable)
{
  int ret = nxmutex_lock(&g_aidk_motor_lock);

  if (ret >= 0)
    {
      ret = aidk_motor_set_locked(enable);
      nxmutex_unlock(&g_aidk_motor_lock);
    }

  return ret;
}

int bk7258_aidk_motor_capture_quiet(bool quiet)
{
  int ret = nxmutex_lock(&g_aidk_motor_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (!g_aidk_motor_ready)
    {
      ret = -ENODEV;
    }
  else
    {
      g_aidk_motor_quiet = quiet;
      ret = quiet ? aidk_motor_set_locked(false) : OK;
    }

  nxmutex_unlock(&g_aidk_motor_lock);
  return ret;
}
