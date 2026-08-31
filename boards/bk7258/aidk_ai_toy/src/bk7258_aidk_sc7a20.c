/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_sc7a20.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK AI Toy physical binding for the generic NuttX SC7A20 driver.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_SC7A20

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/sensors/sc7a20.h>
#include <nuttx/signal.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_pinmux.h>

#define AIDK_SC7A20_STRINGIFY_(value) #value
#define AIDK_SC7A20_STRINGIFY(value)  AIDK_SC7A20_STRINGIFY_(value)
#define AIDK_SC7A20_I2C_DEVPATH       \
  "/dev/i2c" AIDK_SC7A20_STRINGIFY(BK7258_BOARD_SC7A20_I2C_BUS)
#define AIDK_SC7A20_I2C_MUX_MODE      0
#define AIDK_SC7A20_RECOVERY_CLOCKS   9
#define AIDK_SC7A20_RECOVERY_HALF_US  5

#if BK7258_BOARD_HAS_SC7A20 != 1 || \
    BK7258_BOARD_SC7A20_I2C_BUS != 0 || \
    BK7258_BOARD_SC7A20_I2C_SCL_GPIO != 20 || \
    BK7258_BOARD_SC7A20_I2C_SDA_GPIO != 21 || \
    BK7258_BOARD_SC7A20_I2C_ADDRESS != 0x18 || \
    BK7258_BOARD_SC7A20_POWER_ALWAYS_ON != 1
#  error "AIDK SC7A20H binding no longer matches the board"
#endif

#if CONFIG_BK7258_I2C_BUS != BK7258_BOARD_SC7A20_I2C_BUS
#  error "AIDK AP profile must publish the SC7A20H hardware I2C bus"
#endif

struct aidk_sc7a20_i2c_s
{
  FAR const char *devpath;
  uint32_t frequency;
  uint16_t address;
};

static int aidk_sc7a20_restore_i2c_pinmux(void)
{
  static const struct bk7258_pinmux_config_s configs[] =
  {
    {
      BK7258_BOARD_SC7A20_I2C_SCL_GPIO,
      AIDK_SC7A20_I2C_MUX_MODE,
      true
    },
    {
      BK7258_BOARD_SC7A20_I2C_SDA_GPIO,
      AIDK_SC7A20_I2C_MUX_MODE,
      true
    },
  };

  return bk7258_pinmux_apply(configs,
                             sizeof(configs) / sizeof(configs[0]));
}

static int aidk_sc7a20_read_lines(FAR bool *scl, FAR bool *sda)
{
  int ret;

  ret = bk7258_gpio_read_input(BK7258_BOARD_SC7A20_I2C_SCL_GPIO, scl);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_gpio_read_input(BK7258_BOARD_SC7A20_I2C_SDA_GPIO, sda);
}

static int aidk_sc7a20_recover_bus(void)
{
  bool scl_before = false;
  bool sda_before = false;
  bool scl_after = false;
  bool sda_after = false;
  int restore_ret;
  int pulses = 0;
  int ret;

  ret = bk7258_gpio_configure_open_drain(
          BK7258_BOARD_SC7A20_I2C_SCL_GPIO, BK7258_GPIO_PULL_UP);
  if (ret < 0)
    {
      goto restore;
    }

  ret = bk7258_gpio_configure_open_drain(
          BK7258_BOARD_SC7A20_I2C_SDA_GPIO, BK7258_GPIO_PULL_UP);
  if (ret < 0)
    {
      goto restore;
    }

  (void)bk7258_gpio_open_drain_write(
          BK7258_BOARD_SC7A20_I2C_SCL_GPIO, true);
  (void)bk7258_gpio_open_drain_write(
          BK7258_BOARD_SC7A20_I2C_SDA_GPIO, true);
  (void)nxsig_usleep(AIDK_SC7A20_RECOVERY_HALF_US);

  ret = aidk_sc7a20_read_lines(&scl_before, &sda_before);
  if (ret < 0)
    {
      goto restore;
    }

  if (!scl_before)
    {
      ret = -ETIMEDOUT;
      goto sample;
    }

  while (!sda_before && pulses < AIDK_SC7A20_RECOVERY_CLOCKS)
    {
      (void)bk7258_gpio_open_drain_write(
              BK7258_BOARD_SC7A20_I2C_SCL_GPIO, false);
      (void)nxsig_usleep(AIDK_SC7A20_RECOVERY_HALF_US);
      (void)bk7258_gpio_open_drain_write(
              BK7258_BOARD_SC7A20_I2C_SCL_GPIO, true);
      (void)nxsig_usleep(AIDK_SC7A20_RECOVERY_HALF_US);
      pulses++;

      ret = aidk_sc7a20_read_lines(&scl_after, &sda_before);
      if (ret < 0 || !scl_after)
        {
          ret = ret < 0 ? ret : -ETIMEDOUT;
          goto sample;
        }
    }

  /* Generate a STOP after releasing a slave that was waiting for clocks. */

  (void)bk7258_gpio_open_drain_write(
          BK7258_BOARD_SC7A20_I2C_SDA_GPIO, false);
  (void)nxsig_usleep(AIDK_SC7A20_RECOVERY_HALF_US);
  (void)bk7258_gpio_open_drain_write(
          BK7258_BOARD_SC7A20_I2C_SCL_GPIO, true);
  (void)nxsig_usleep(AIDK_SC7A20_RECOVERY_HALF_US);
  (void)bk7258_gpio_open_drain_write(
          BK7258_BOARD_SC7A20_I2C_SDA_GPIO, true);
  (void)nxsig_usleep(AIDK_SC7A20_RECOVERY_HALF_US);

sample:
  if (aidk_sc7a20_read_lines(&scl_after, &sda_after) < 0)
    {
      scl_after = false;
      sda_after = false;
      if (ret >= 0)
        {
          ret = -EIO;
        }
    }
  else if (ret >= 0 && (!scl_after || !sda_after))
    {
      ret = -EBUSY;
    }

restore:
  restore_ret = aidk_sc7a20_restore_i2c_pinmux();
  if (ret >= 0 && restore_ret < 0)
    {
      ret = restore_ret;
    }

  syslog(ret < 0 ? LOG_ERR : LOG_WARNING,
         "AIDK SC7A20H I2C recovery: lines=%d/%d->%d/%d "
         "clocks=%d ret=%d restore=%d\n",
         scl_before, sda_before, scl_after, sda_after,
         pulses, ret, restore_ret);
  return ret;
}

static int aidk_sc7a20_transfer_once(FAR struct aidk_sc7a20_i2c_s *i2c,
                                     FAR struct i2c_msg_s *messages,
                                     int message_count)
{
  struct i2c_transfer_s transfer;
  struct file filep;
  int ret;

  ret = file_open(&filep, i2c->devpath, O_RDWR);
  if (ret < 0)
    {
      return ret;
    }

  transfer.msgv = messages;
  transfer.msgc = message_count;
  ret = file_ioctl(&filep, I2CIOC_TRANSFER,
                   (unsigned long)(uintptr_t)&transfer);
  file_close(&filep);
  return ret;
}

static int aidk_sc7a20_transfer(FAR struct aidk_sc7a20_i2c_s *i2c,
                                FAR struct i2c_msg_s *messages,
                                int message_count)
{
  int recovery_ret;
  int ret;

  ret = aidk_sc7a20_transfer_once(i2c, messages, message_count);
  if (ret != -ETIMEDOUT && ret != -EBUSY)
    {
      return ret;
    }

  recovery_ret = aidk_sc7a20_recover_bus();
  if (recovery_ret < 0)
    {
      return ret;
    }

  ret = aidk_sc7a20_transfer_once(i2c, messages, message_count);
  syslog(ret < 0 ? LOG_ERR : LOG_WARNING,
         "AIDK SC7A20H I2C retry after timeout: %d\n", ret);
  return ret;
}

static int aidk_sc7a20_read(FAR void *arg, uint8_t reg,
                            FAR uint8_t *buffer, size_t buflen)
{
  FAR struct aidk_sc7a20_i2c_s *i2c =
    (FAR struct aidk_sc7a20_i2c_s *)arg;
  struct i2c_msg_s messages[2];

  messages[0].frequency = i2c->frequency;
  messages[0].addr = i2c->address;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = &reg;
  messages[0].length = 1;

  messages[1].frequency = i2c->frequency;
  messages[1].addr = i2c->address;
  messages[1].flags = I2C_M_READ;
  messages[1].buffer = buffer;
  messages[1].length = buflen;

  return aidk_sc7a20_transfer(i2c, messages, 2);
}

static int aidk_sc7a20_write(FAR void *arg, uint8_t reg, uint8_t value)
{
  FAR struct aidk_sc7a20_i2c_s *i2c =
    (FAR struct aidk_sc7a20_i2c_s *)arg;
  struct i2c_msg_s message;
  uint8_t data[2] = {reg, value};

  message.frequency = i2c->frequency;
  message.addr = i2c->address;
  message.flags = 0;
  message.buffer = data;
  message.length = sizeof(data);

  return aidk_sc7a20_transfer(i2c, &message, 1);
}

static struct aidk_sc7a20_i2c_s g_aidk_sc7a20_i2c =
{
  .devpath = AIDK_SC7A20_I2C_DEVPATH,
  .frequency = BK7258_BOARD_SC7A20_I2C_FREQUENCY,
  .address = BK7258_BOARD_SC7A20_I2C_ADDRESS,
};

static const struct sc7a20_config_s g_aidk_sc7a20_config =
{
  .arg = &g_aidk_sc7a20_i2c,
  .read = aidk_sc7a20_read,
  .write = aidk_sc7a20_write,
};

int bk7258_aidk_sc7a20_initialize(void)
{
  int ret;

#ifdef CONFIG_BK7258_AIDK_SC7A20_PHASE0
#  error "SC7A20H Phase 0 and production bindings are mutually exclusive"
#endif

  ret = sc7a20_register(0, &g_aidk_sc7a20_config);
  if (ret >= 0)
    {
      syslog(LOG_INFO,
             "AIDK SC7A20H registered: /dev/uorb/sensor_accel0\n");
    }

  return ret;
}

#endif /* CONFIG_BK7258_AIDK_SC7A20 */
