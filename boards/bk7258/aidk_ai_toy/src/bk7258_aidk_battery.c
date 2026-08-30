/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_battery.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ETA4322 board lower half for the standard NuttX battery upper half.
 *
 * The charger has no software control bus.  The official AIDK SDK reports
 * its state from P51 (external 5 V present) and active-low P26 (full), and
 * samples the internal ADC0 VBAT path.  This lower half exposes those facts
 * through BATIOC_STATE and BATIOC_GET_VOLTAGE at /dev/bat0.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_BATTERY

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/fs/fs.h>
#include <nuttx/power/battery_charger.h>
#include <nuttx/power/battery_ioctl.h>

#include <arch/board/board.h>

#include <driver/gpio.h>

#define AIDK_BATTERY_DEVPATH  "/dev/bat0"

#if BK7258_BOARD_HAS_BATTERY != 1 || \
    BK7258_BOARD_PIN_5V_DET != 51 || \
    BK7258_BOARD_PIN_FULL_DET != 26 || \
    BK7258_BOARD_5V_DET_ACTIVE_HIGH != 1 || \
    BK7258_BOARD_FULL_DET_ACTIVE_LOW != 1
#  error "AIDK ETA4322 binding no longer matches the board"
#endif

struct aidk_battery_dev_s
{
  struct battery_charger_dev_s dev;
};

static int aidk_battery_state(FAR struct battery_charger_dev_s *dev,
                              FAR int *status);
static int aidk_battery_health(FAR struct battery_charger_dev_s *dev,
                               FAR int *health);
static int aidk_battery_online(FAR struct battery_charger_dev_s *dev,
                               FAR bool *online);
static int aidk_battery_voltage(FAR struct battery_charger_dev_s *dev,
                                int value);
static int aidk_battery_current(FAR struct battery_charger_dev_s *dev,
                                int value);
static int aidk_battery_input_current(FAR struct battery_charger_dev_s *dev,
                                      int value);
static int aidk_battery_operate(FAR struct battery_charger_dev_s *dev,
                                uintptr_t param);
static int aidk_battery_chipid(FAR struct battery_charger_dev_s *dev,
                               FAR unsigned int *value);
static int aidk_battery_get_voltage(FAR struct battery_charger_dev_s *dev,
                                    FAR int *value);
static int aidk_battery_voltage_info(FAR struct battery_charger_dev_s *dev,
                                     FAR int *value);
static int aidk_battery_get_protocol(FAR struct battery_charger_dev_s *dev,
                                     FAR int *value);

static const struct battery_charger_operations_s g_aidk_battery_ops =
{
  .state         = aidk_battery_state,
  .health        = aidk_battery_health,
  .online        = aidk_battery_online,
  .voltage       = aidk_battery_voltage,
  .current       = aidk_battery_current,
  .input_current = aidk_battery_input_current,
  .operate       = aidk_battery_operate,
  .chipid        = aidk_battery_chipid,
  .get_voltage   = aidk_battery_get_voltage,
  .voltage_info  = aidk_battery_voltage_info,
  .get_protocol  = aidk_battery_get_protocol,
};

static struct aidk_battery_dev_s g_aidk_battery =
{
  .dev.ops = &g_aidk_battery_ops,
};

static bool aidk_battery_5v_present(void)
{
  return bk_gpio_get_input((gpio_id_t)BK7258_BOARD_PIN_5V_DET) != 0;
}

static bool aidk_battery_full(void)
{
  return bk_gpio_get_input((gpio_id_t)BK7258_BOARD_PIN_FULL_DET) == 0;
}

static int aidk_battery_state(FAR struct battery_charger_dev_s *dev,
                              FAR int *status)
{
  (void)dev;

  if (status == NULL)
    {
      return -EINVAL;
    }

  if (!aidk_battery_5v_present())
    {
      *status = BATTERY_DISCHARGING;
    }
  else if (aidk_battery_full())
    {
      *status = BATTERY_FULL;
    }
  else
    {
      *status = BATTERY_CHARGING;
    }

  return OK;
}

static int aidk_battery_health(FAR struct battery_charger_dev_s *dev,
                               FAR int *health)
{
  (void)dev;

  if (health == NULL)
    {
      return -EINVAL;
    }

  /* ETA4322 exposes no fault pin or software status on this board. */

  *health = BATTERY_HEALTH_GOOD;
  return OK;
}

static int aidk_battery_online(FAR struct battery_charger_dev_s *dev,
                               FAR bool *online)
{
  (void)dev;

  if (online == NULL)
    {
      return -EINVAL;
    }

  *online = true;
  return OK;
}

static int aidk_battery_voltage(FAR struct battery_charger_dev_s *dev,
                                int value)
{
  (void)dev;
  (void)value;
  return -ENOSYS;
}

static int aidk_battery_current(FAR struct battery_charger_dev_s *dev,
                                int value)
{
  (void)dev;
  (void)value;
  return -ENOSYS;
}

static int aidk_battery_input_current(FAR struct battery_charger_dev_s *dev,
                                      int value)
{
  (void)dev;
  (void)value;
  return -ENOSYS;
}

static int aidk_battery_operate(FAR struct battery_charger_dev_s *dev,
                                uintptr_t param)
{
  (void)dev;
  (void)param;
  return -ENOSYS;
}

static int aidk_battery_chipid(FAR struct battery_charger_dev_s *dev,
                               FAR unsigned int *value)
{
  (void)dev;
  (void)value;
  return -ENOSYS;
}

static int aidk_battery_get_voltage(FAR struct battery_charger_dev_s *dev,
                                    FAR int *value)
{
  struct adc_msg_s sample;
  struct file adc;
  ssize_t nread;
  int ret;

  (void)dev;

  if (value == NULL)
    {
      return -EINVAL;
    }

  ret = file_open(&adc, BK7258_BOARD_VBAT_ADC_DEV, O_RDONLY);
  if (ret < 0)
    {
      return ret;
    }

  ret = file_ioctl(&adc, ANIOC_RESET_FIFO, 0);
  if (ret >= 0)
    {
      ret = file_ioctl(&adc, ANIOC_TRIGGER, 0);
    }

  if (ret >= 0)
    {
      nread = file_read(&adc, (FAR char *)&sample, sizeof(sample));
      ret = nread == sizeof(sample) ? OK :
            (nread < 0 ? (int)nread : -EIO);
    }

  file_close(&adc);
  if (ret < 0)
    {
      return ret;
    }

  if (sample.am_channel != 0 || sample.am_data < 0)
    {
      return -EIO;
    }

  /* Official AIDK SDK conversion for the ADC0 internal VBAT path. */

  *value = (int)(((uint32_t)sample.am_data *
                  BK7258_BOARD_VBAT_SCALE_NUMERATOR) /
                 BK7258_BOARD_VBAT_SCALE_DENOMINATOR +
                 BK7258_BOARD_VBAT_OFFSET_MV);
  return OK;
}

static int aidk_battery_voltage_info(FAR struct battery_charger_dev_s *dev,
                                     FAR int *value)
{
  return aidk_battery_get_voltage(dev, value);
}

static int aidk_battery_get_protocol(FAR struct battery_charger_dev_s *dev,
                                     FAR int *value)
{
  (void)dev;

  if (value == NULL)
    {
      return -EINVAL;
    }

  *value = BATTERY_PROTOCOL_DEFAULT;
  return OK;
}

int bk7258_aidk_battery_initialize(void)
{
  int ret;

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  if (bk_gpio_enable_input((gpio_id_t)BK7258_BOARD_PIN_5V_DET) != BK_OK ||
      bk_gpio_enable_input((gpio_id_t)BK7258_BOARD_PIN_FULL_DET) != BK_OK)
    {
      return -EIO;
    }

  ret = battery_charger_register(AIDK_BATTERY_DEVPATH,
                                 &g_aidk_battery.dev);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO, "AIDK ETA4322 registered: %s\n",
         AIDK_BATTERY_DEVPATH);
  return OK;
}

#endif /* CONFIG_BK7258_AIDK_BATTERY */
