/****************************************************************************
 * drivers/sensors/sc7a20.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_SENSORS_SC7A20

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/sensors/sc7a20.h>
#include <nuttx/sensors/sensor.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SC7A20_REG_WHO_AM_I          0x0f
#define SC7A20_REG_CTRL1             0x20
#define SC7A20_REG_CTRL4             0x23
#define SC7A20_REG_OUT_X_L           0x28

#define SC7A20_WHO_AM_I              0x11
#define SC7A20_CTRL1_POWER_DOWN      0x08
#define SC7A20_CTRL1_AXES            0x07
#define SC7A20_CTRL4_FS2G            0x00
#define SC7A20_AUTOINCREMENT         0x80

#define SC7A20_DEFAULT_ODR           0x50
#define SC7A20_MG_PER_COUNT          4.0f
#define SC7A20_ONE_G                 9.80665f

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sc7a20_dev_s
{
  struct sensor_lowerhalf_s lower;
  struct sc7a20_config_s config;
  mutex_t lock;
  uint8_t odr;
  bool active;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int sc7a20_activate(FAR struct sensor_lowerhalf_s *lower,
                           FAR struct file *filep, bool enable);
static int sc7a20_set_interval(FAR struct sensor_lowerhalf_s *lower,
                               FAR struct file *filep,
                               FAR uint32_t *period_us);
static int sc7a20_fetch(FAR struct sensor_lowerhalf_s *lower,
                        FAR struct file *filep, FAR char *buffer,
                        size_t buflen);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct sensor_ops_s g_sc7a20_ops =
{
  .activate     = sc7a20_activate,
  .set_interval = sc7a20_set_interval,
  .fetch        = sc7a20_fetch,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int sc7a20_write_reg(FAR struct sc7a20_dev_s *priv, uint8_t reg,
                            uint8_t value)
{
  return priv->config.write(priv->config.arg, reg, value);
}

static int sc7a20_read_regs(FAR struct sc7a20_dev_s *priv, uint8_t reg,
                            FAR uint8_t *buffer, size_t buflen)
{
  return priv->config.read(priv->config.arg, reg, buffer, buflen);
}

static int sc7a20_activate(FAR struct sensor_lowerhalf_s *lower,
                           FAR struct file *filep, bool enable)
{
  FAR struct sc7a20_dev_s *priv = (FAR struct sc7a20_dev_s *)lower;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = sc7a20_write_reg(priv, SC7A20_REG_CTRL1,
                         enable ? priv->odr | SC7A20_CTRL1_AXES :
                                  SC7A20_CTRL1_POWER_DOWN);
  if (ret >= 0)
    {
      priv->active = enable;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static int sc7a20_set_interval(FAR struct sensor_lowerhalf_s *lower,
                               FAR struct file *filep,
                               FAR uint32_t *period_us)
{
  FAR struct sc7a20_dev_s *priv = (FAR struct sc7a20_dev_s *)lower;
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
      ret = sc7a20_write_reg(priv, SC7A20_REG_CTRL1,
                             odr | SC7A20_CTRL1_AXES);
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

static int sc7a20_fetch(FAR struct sensor_lowerhalf_s *lower,
                        FAR struct file *filep, FAR char *buffer,
                        size_t buflen)
{
  FAR struct sc7a20_dev_s *priv = (FAR struct sc7a20_dev_s *)lower;
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

  ret = sc7a20_read_regs(priv,
                         SC7A20_REG_OUT_X_L | SC7A20_AUTOINCREMENT,
                         raw, sizeof(raw));
  nxmutex_unlock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  /* Normal-mode samples are left-justified 10-bit values.  At +/-2 g each
   * count represents 4 mg.
   */

  raw_x = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8)) / 64;
  raw_y = (int16_t)((uint16_t)raw[2] | ((uint16_t)raw[3] << 8)) / 64;
  raw_z = (int16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8)) / 64;

  sample.timestamp = sensor_get_timestamp();
  sample.x = raw_x * SC7A20_MG_PER_COUNT * SC7A20_ONE_G / 1000.0f;
  sample.y = raw_y * SC7A20_MG_PER_COUNT * SC7A20_ONE_G / 1000.0f;
  sample.z = raw_z * SC7A20_MG_PER_COUNT * SC7A20_ONE_G / 1000.0f;
  sample.temperature = NAN;
  sample.status = 0;

  memcpy(buffer, &sample, sizeof(sample));
  return sizeof(sample);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sc7a20_register(int devno, FAR const struct sc7a20_config_s *config)
{
  FAR struct sc7a20_dev_s *priv;
  uint8_t whoami = 0;
  int ret;

  if (devno < 0 || config == NULL || config->read == NULL ||
      config->write == NULL)
    {
      return -EINVAL;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  ret = nxmutex_init(&priv->lock);
  if (ret < 0)
    {
      kmm_free(priv);
      return ret;
    }

  priv->config = *config;
  priv->odr = SC7A20_DEFAULT_ODR;
  priv->lower.type = SENSOR_TYPE_ACCELEROMETER;
  priv->lower.nbuffer = 1;
  priv->lower.ops = &g_sc7a20_ops;

  ret = nxmutex_lock(&priv->lock);
  if (ret >= 0)
    {
      ret = sc7a20_read_regs(priv, SC7A20_REG_WHO_AM_I, &whoami, 1);
      if (ret >= 0 && whoami != SC7A20_WHO_AM_I)
        {
          ret = -ENODEV;
        }

      if (ret >= 0)
        {
          ret = sc7a20_write_reg(priv, SC7A20_REG_CTRL1,
                                 SC7A20_CTRL1_POWER_DOWN);
        }

      if (ret >= 0)
        {
          ret = sc7a20_write_reg(priv, SC7A20_REG_CTRL4,
                                 SC7A20_CTRL4_FS2G);
        }

      nxmutex_unlock(&priv->lock);
    }

  if (ret >= 0)
    {
      ret = sensor_register(&priv->lower, devno);
    }

  if (ret < 0)
    {
      nxmutex_destroy(&priv->lock);
      kmm_free(priv);
    }

  return ret;
}

#endif /* CONFIG_SENSORS_SC7A20 */
