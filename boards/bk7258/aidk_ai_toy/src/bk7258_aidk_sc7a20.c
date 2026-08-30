/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_sc7a20.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SC7A20H lower half for the standard NuttX sensor upper half.
 *
 * The register values, I2C route and 0x11 identity come from Beken's official
 * AIDK SC7A20 implementation.  NuttX owns I2C0 and the public sensor ABI;
 * this board file contains only the chip-specific register operations that
 * the upstream LIS2DH driver cannot use because it requires identity 0x33.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_SC7A20

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mutex.h>
#include <nuttx/sensors/sensor.h>

#include <arch/board/board.h>

#define AIDK_SC7A20_I2C_DEVPATH       "/dev/i2c0"

#define AIDK_SC7A20_REG_CTRL1         0x20
#define AIDK_SC7A20_REG_CTRL4         0x23
#define AIDK_SC7A20_REG_OUT_X_L       0x28

#define AIDK_SC7A20_CTRL1_POWER_DOWN  0x08
#define AIDK_SC7A20_CTRL1_AXES        0x07
#define AIDK_SC7A20_CTRL4_FS2G        0x00
#define AIDK_SC7A20_AUTOINCREMENT     0x80

#define AIDK_SC7A20_DEFAULT_ODR       0x50
#define AIDK_SC7A20_MG_PER_COUNT      4.0f
#define AIDK_SC7A20_ONE_G             9.80665f

#if BK7258_BOARD_HAS_SC7A20 != 1 || \
    BK7258_BOARD_SC7A20_I2C_BUS != 0 || \
    BK7258_BOARD_SC7A20_I2C_SCL_GPIO != 20 || \
    BK7258_BOARD_SC7A20_I2C_SDA_GPIO != 21 || \
    BK7258_BOARD_SC7A20_I2C_ADDRESS != 0x18 || \
    BK7258_BOARD_SC7A20_WHO_AM_I_VALUE != 0x11
#  error "AIDK SC7A20H binding no longer matches the board"
#endif

struct aidk_sc7a20_dev_s
{
  struct sensor_lowerhalf_s lower;
  mutex_t lock;
  uint8_t odr;
  bool active;
};

static int aidk_sc7a20_activate(FAR struct sensor_lowerhalf_s *lower,
                                FAR struct file *filep, bool enable);
static int aidk_sc7a20_set_interval(FAR struct sensor_lowerhalf_s *lower,
                                    FAR struct file *filep,
                                    FAR uint32_t *period_us);
static int aidk_sc7a20_fetch(FAR struct sensor_lowerhalf_s *lower,
                             FAR struct file *filep, FAR char *buffer,
                             size_t buflen);

static const struct sensor_ops_s g_aidk_sc7a20_ops =
{
  .activate     = aidk_sc7a20_activate,
  .set_interval = aidk_sc7a20_set_interval,
  .fetch        = aidk_sc7a20_fetch,
};

static struct aidk_sc7a20_dev_s g_aidk_sc7a20 =
{
  .lower =
    {
      .type = SENSOR_TYPE_ACCELEROMETER,
      .nbuffer = 1,
      .ops = &g_aidk_sc7a20_ops,
    },
  .lock = NXMUTEX_INITIALIZER,
  .odr = AIDK_SC7A20_DEFAULT_ODR,
};

static int aidk_sc7a20_transfer(FAR struct i2c_msg_s *messages,
                                int message_count)
{
  struct i2c_transfer_s transfer;
  struct file i2c;
  int ret;

  ret = file_open(&i2c, AIDK_SC7A20_I2C_DEVPATH, O_RDWR);
  if (ret < 0)
    {
      return ret;
    }

  transfer.msgv = messages;
  transfer.msgc = message_count;
  ret = file_ioctl(&i2c, I2CIOC_TRANSFER,
                   (unsigned long)(uintptr_t)&transfer);
  file_close(&i2c);
  return ret;
}

static int aidk_sc7a20_write_reg(uint8_t reg, uint8_t value)
{
  struct i2c_msg_s message;
  uint8_t data[2] = {reg, value};

  message.frequency = BK7258_BOARD_SC7A20_I2C_FREQUENCY;
  message.addr = BK7258_BOARD_SC7A20_I2C_ADDRESS;
  message.flags = 0;
  message.buffer = data;
  message.length = sizeof(data);
  return aidk_sc7a20_transfer(&message, 1);
}

static int aidk_sc7a20_read_regs(uint8_t reg, FAR uint8_t *data,
                                 uint8_t length)
{
  struct i2c_msg_s messages[2];

  messages[0].frequency = BK7258_BOARD_SC7A20_I2C_FREQUENCY;
  messages[0].addr = BK7258_BOARD_SC7A20_I2C_ADDRESS;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = &reg;
  messages[0].length = 1;

  messages[1].frequency = BK7258_BOARD_SC7A20_I2C_FREQUENCY;
  messages[1].addr = BK7258_BOARD_SC7A20_I2C_ADDRESS;
  messages[1].flags = I2C_M_READ;
  messages[1].buffer = data;
  messages[1].length = length;

  return aidk_sc7a20_transfer(messages, 2);
}

static int aidk_sc7a20_activate(FAR struct sensor_lowerhalf_s *lower,
                                FAR struct file *filep, bool enable)
{
  FAR struct aidk_sc7a20_dev_s *priv =
    (FAR struct aidk_sc7a20_dev_s *)lower;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = aidk_sc7a20_write_reg(
    AIDK_SC7A20_REG_CTRL1,
    enable ? priv->odr | AIDK_SC7A20_CTRL1_AXES :
             AIDK_SC7A20_CTRL1_POWER_DOWN);
  if (ret >= 0)
    {
      priv->active = enable;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static int aidk_sc7a20_set_interval(FAR struct sensor_lowerhalf_s *lower,
                                    FAR struct file *filep,
                                    FAR uint32_t *period_us)
{
  FAR struct aidk_sc7a20_dev_s *priv =
    (FAR struct aidk_sc7a20_dev_s *)lower;
  uint32_t actual;
  uint8_t odr;
  int ret;

  (void)filep;

  if (period_us == NULL)
    {
      return -EINVAL;
    }

  if (*period_us <= 2500)
    {
      odr = 0x70;
      actual = 2500;
    }
  else if (*period_us <= 5000)
    {
      odr = 0x60;
      actual = 5000;
    }
  else if (*period_us <= 10000)
    {
      odr = 0x50;
      actual = 10000;
    }
  else if (*period_us <= 20000)
    {
      odr = 0x40;
      actual = 20000;
    }
  else if (*period_us <= 40000)
    {
      odr = 0x30;
      actual = 40000;
    }
  else if (*period_us <= 100000)
    {
      odr = 0x20;
      actual = 100000;
    }
  else
    {
      odr = 0x10;
      actual = 1000000;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->active)
    {
      ret = aidk_sc7a20_write_reg(AIDK_SC7A20_REG_CTRL1,
                                  odr | AIDK_SC7A20_CTRL1_AXES);
      if (ret < 0)
        {
          nxmutex_unlock(&priv->lock);
          return ret;
        }
    }

  priv->odr = odr;
  *period_us = actual;
  nxmutex_unlock(&priv->lock);
  return OK;
}

static int aidk_sc7a20_fetch(FAR struct sensor_lowerhalf_s *lower,
                             FAR struct file *filep, FAR char *buffer,
                             size_t buflen)
{
  FAR struct aidk_sc7a20_dev_s *priv =
    (FAR struct aidk_sc7a20_dev_s *)lower;
  struct sensor_accel sample;
  int16_t raw_x;
  int16_t raw_y;
  int16_t raw_z;
  uint8_t raw[6];
  int ret;

  (void)filep;

  if (buffer == NULL || buflen < sizeof(sample))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->active)
    {
      nxmutex_unlock(&priv->lock);
      return -EAGAIN;
    }

  ret = aidk_sc7a20_read_regs(
    AIDK_SC7A20_REG_OUT_X_L | AIDK_SC7A20_AUTOINCREMENT,
    raw, sizeof(raw));
  nxmutex_unlock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  /* In the official normal-mode configuration the 10-bit sample is left
   * justified in the 16-bit output.  At +/-2 g each count is 4 mg.
   */

  raw_x = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8)) / 64;
  raw_y = (int16_t)((uint16_t)raw[2] | ((uint16_t)raw[3] << 8)) / 64;
  raw_z = (int16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8)) / 64;

  sample.timestamp = sensor_get_timestamp();
  sample.x = raw_x * AIDK_SC7A20_MG_PER_COUNT *
             AIDK_SC7A20_ONE_G / 1000.0f;
  sample.y = raw_y * AIDK_SC7A20_MG_PER_COUNT *
             AIDK_SC7A20_ONE_G / 1000.0f;
  sample.z = raw_z * AIDK_SC7A20_MG_PER_COUNT *
             AIDK_SC7A20_ONE_G / 1000.0f;
  sample.temperature = NAN;
  sample.status = 0;

  memcpy(buffer, &sample, sizeof(sample));
  return sizeof(sample);
}

int bk7258_aidk_sc7a20_initialize(void)
{
  uint8_t whoami = 0;
  int ret;

#ifdef CONFIG_BK7258_AIDK_SC7A20_PHASE0
#  error "SC7A20H Phase 0 and production bindings are mutually exclusive"
#endif

  ret = nxmutex_lock(&g_aidk_sc7a20.lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = aidk_sc7a20_read_regs(BK7258_BOARD_SC7A20_WHO_AM_I_REG,
                              &whoami, 1);
  if (ret >= 0 && whoami != BK7258_BOARD_SC7A20_WHO_AM_I_VALUE)
    {
      ret = -ENODEV;
    }

  if (ret >= 0)
    {
      ret = aidk_sc7a20_write_reg(AIDK_SC7A20_REG_CTRL1,
                                  AIDK_SC7A20_CTRL1_POWER_DOWN);
    }

  if (ret >= 0)
    {
      ret = aidk_sc7a20_write_reg(AIDK_SC7A20_REG_CTRL4,
                                  AIDK_SC7A20_CTRL4_FS2G);
    }

  nxmutex_unlock(&g_aidk_sc7a20.lock);
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK SC7A20H probe/config failed: %d id=0x%02x\n",
             ret, whoami);
      return ret;
    }

  ret = sensor_register(&g_aidk_sc7a20.lower, 0);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO,
         "AIDK SC7A20H registered: /dev/uorb/sensor_accel0 id=0x%02x\n",
         whoami);
  return OK;
}

#endif /* CONFIG_BK7258_AIDK_SC7A20 */
