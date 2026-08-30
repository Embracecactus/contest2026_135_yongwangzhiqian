/****************************************************************************
 * drivers/lcd/gc9d01.c
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

#ifdef CONFIG_LCD_GC9D01

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

#include <debug.h>
#include <nuttx/lcd/gc9d01.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include "gc9d01.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct gc9d01_initcmd_s
{
  uint8_t cmd;
  uint8_t nparams;
  uint16_t delay_ms;
  uint8_t params[32];
};

struct gc9d01_dev_s
{
  struct lcd_dev_s dev;
  FAR struct gc9d01_lcd_s *lcd;
  mutex_t lock;
  uint16_t runbuffer[GC9D01_XRES];
  uint8_t power;
  bool awake;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* This initialization sequence is derived from Beken BK-AVDK v3.1.1.9
 * components/bk_peripheral/src/lcd/spi/lcd_spi_gc9d01.c (Apache-2.0).
 * The table is kept in this hardware-independent NuttX panel driver so the
 * product does not depend on the SDK's private lcd_device_t registry.
 */

static const struct gc9d01_initcmd_s g_gc9d01_init[] =
{
  {0xfe, 0,   0, {0}},
  {0xef, 0,   0, {0}},
  {0x80, 1,   0, {0xff}},
  {0x81, 1,   0, {0xff}},
  {0x82, 1,   0, {0xff}},
  {0x83, 1,   0, {0xff}},
  {0x84, 1,   0, {0xff}},
  {0x85, 1,   0, {0xff}},
  {0x86, 1,   0, {0xff}},
  {0x87, 1,   0, {0xff}},
  {0x88, 1,   0, {0xff}},
  {0x89, 1,   0, {0xff}},
  {0x8a, 1,   0, {0xff}},
  {0x8b, 1,   0, {0xff}},
  {0x8c, 1,   0, {0xff}},
  {0x8d, 1,   0, {0xff}},
  {0x8e, 1,   0, {0xff}},
  {0x8f, 1,   0, {0xff}},
  {GC9D01_COLMOD, 1, 0, {0x05}},
  {0xec, 1,   0, {0x01}},
  {0x74, 7,   0, {0x02, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00}},
  {0x98, 3,   0, {0x3e, 0x99, 0x3e}},
  {0xb5, 2,   0, {0x0d, 0x0d}},
  {0x60, 4,   0, {0x38, 0x0f, 0x79, 0x67}},
  {0x61, 4,   0, {0x38, 0x11, 0x79, 0x67}},
  {0x64, 6,   0, {0x38, 0x17, 0x71, 0x5f, 0x79, 0x67}},
  {0x65, 6,   0, {0x38, 0x13, 0x71, 0x5b, 0x79, 0x67}},
  {0x6a, 2,   0, {0x00, 0x00}},
  {0x6c, 7,   0, {0x22, 0x02, 0x22, 0x02, 0x22, 0x22, 0x50}},
  {0x6e, 32,  0, {0x03, 0x03, 0x01, 0x01, 0x00, 0x00, 0x0f, 0x0f,
                    0x0d, 0x0d, 0x0b, 0x0b, 0x09, 0x09, 0x00, 0x00,
                    0x00, 0x00, 0x0a, 0x0a, 0x0c, 0x0c, 0x0e, 0x0e,
                    0x10, 0x10, 0x00, 0x00, 0x02, 0x02, 0x04, 0x04}},
  {0xbf, 1,   0, {0x01}},
  {0xf9, 1,   0, {0x40}},
  {0x9b, 1,   0, {0x3b}},
  {0x93, 3,   0, {0x33, 0x7f, 0x00}},
  {0x7e, 1,   0, {0x30}},
  {0x70, 6,   0, {0x0d, 0x02, 0x08, 0x0d, 0x02, 0x08}},
  {0x71, 3,   0, {0x0d, 0x02, 0x08}},
  {0x91, 2,   0, {0x0e, 0x09}},
  {0xc3, 1,   0, {0x18}},
  {0xc4, 1,   0, {0x18}},
  {0xc9, 1,   0, {0x3c}},
  {0xf0, 6,   0, {0x13, 0x15, 0x04, 0x05, 0x01, 0x38}},
  {0xf2, 6,   0, {0x13, 0x15, 0x04, 0x05, 0x01, 0x34}},
  {0xf1, 6,   0, {0x4b, 0xb8, 0x7b, 0x34, 0x35, 0xef}},
  {0xf3, 6,   0, {0x47, 0xb4, 0x72, 0x34, 0x35, 0xda}},
  {GC9D01_MADCTL, 1, 0, {0x00}},
  {GC9D01_TEOFF, 0, 0, {0}},
  {GC9D01_SLPOUT, 0, 120, {0}},
  {GC9D01_DISPON, 0, 0, {0}},
};

static struct gc9d01_dev_s
  g_gc9d01[CONFIG_LCD_GC9D01_NINTERFACES];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int gc9d01_command(FAR struct gc9d01_dev_s *priv, uint8_t cmd,
                           FAR const uint8_t *params, size_t nparams)
{
  return priv->lcd->writecmd(priv->lcd, cmd, params, nparams);
}

static int gc9d01_setarea(FAR struct gc9d01_dev_s *priv,
                          fb_coord_t row_start, fb_coord_t row_end,
                          fb_coord_t col_start, fb_coord_t col_end)
{
  uint8_t params[4];
  int ret;

  params[0] = (uint8_t)((uint16_t)col_start >> 8);
  params[1] = (uint8_t)col_start;
  params[2] = (uint8_t)((uint16_t)col_end >> 8);
  params[3] = (uint8_t)col_end;
  ret = gc9d01_command(priv, GC9D01_CASET, params, sizeof(params));
  if (ret < 0)
    {
      return ret;
    }

  params[0] = (uint8_t)((uint16_t)row_start >> 8);
  params[1] = (uint8_t)row_start;
  params[2] = (uint8_t)((uint16_t)row_end >> 8);
  params[3] = (uint8_t)row_end;
  ret = gc9d01_command(priv, GC9D01_RASET, params, sizeof(params));
  if (ret < 0)
    {
      return ret;
    }

  return gc9d01_command(priv, GC9D01_RAMWR, NULL, 0);
}

static int gc9d01_getvideoinfo(FAR struct lcd_dev_s *dev,
                               FAR struct fb_videoinfo_s *vinfo)
{
  if (dev == NULL || vinfo == NULL)
    {
      return -EINVAL;
    }

  vinfo->fmt = FB_FMT_RGB16_565;
  vinfo->xres = GC9D01_XRES;
  vinfo->yres = GC9D01_YRES;
  vinfo->nplanes = 1;
  return OK;
}

static int gc9d01_putarea(FAR struct lcd_dev_s *dev,
                          fb_coord_t row_start, fb_coord_t row_end,
                          fb_coord_t col_start, fb_coord_t col_end,
                          FAR const uint8_t *buffer, fb_coord_t stride)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;
  size_t width;
  size_t height;
  int ret;

  if (buffer == NULL || row_start < 0 || col_start < 0 ||
      row_start > row_end || col_start > col_end ||
      row_end >= GC9D01_YRES || col_end >= GC9D01_XRES)
    {
      return -EINVAL;
    }

  width = (size_t)(col_end - col_start + 1);
  height = (size_t)(row_end - row_start + 1);
  if ((size_t)stride < width * sizeof(uint16_t))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = gc9d01_setarea(priv, row_start, row_end, col_start, col_end);
  if (ret == OK)
    {
      ret = priv->lcd->writegram(priv->lcd,
                                 (FAR const uint16_t *)buffer,
                                 width, height, (size_t)stride);
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static int gc9d01_putrun(FAR struct lcd_dev_s *dev, fb_coord_t row,
                         fb_coord_t col, FAR const uint8_t *buffer,
                         size_t npixels)
{
  if (npixels == 0 || npixels > GC9D01_XRES ||
      (size_t)col + npixels > GC9D01_XRES)
    {
      return -EINVAL;
    }

  return gc9d01_putarea(dev, row, row, col,
                        (fb_coord_t)((size_t)col + npixels - 1),
                        buffer, (fb_coord_t)(npixels * sizeof(uint16_t)));
}

static int gc9d01_getplaneinfo(FAR struct lcd_dev_s *dev,
                               unsigned int planeno,
                               FAR struct lcd_planeinfo_s *pinfo)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;

  if (planeno != 0 || pinfo == NULL)
    {
      return -EINVAL;
    }

  pinfo->putrun = gc9d01_putrun;
  pinfo->putarea = gc9d01_putarea;
  pinfo->getrun = NULL;
  pinfo->getarea = NULL;
  pinfo->redraw = NULL;
  pinfo->buffer = (FAR uint8_t *)priv->runbuffer;
  pinfo->bpp = GC9D01_BPP;
  pinfo->dev = dev;
  return OK;
}

static int gc9d01_getpower(FAR struct lcd_dev_s *dev)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;
  return priv->power;
}

static int gc9d01_setpower(FAR struct lcd_dev_s *dev, int power)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;
  int ret;

  if (power < 0 || power > CONFIG_LCD_MAXPOWER)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (power > 0)
    {
      if (!priv->awake)
        {
          ret = gc9d01_command(priv, GC9D01_SLPOUT, NULL, 0);
          if (ret < 0)
            {
              goto out;
            }

          (void)nxsig_usleep(120000);
          ret = gc9d01_command(priv, GC9D01_DISPON, NULL, 0);
          if (ret < 0)
            {
              goto out;
            }

          priv->awake = true;
        }

      if (priv->lcd->backlight != NULL)
        {
          ret = priv->lcd->backlight(priv->lcd, power);
          if (ret < 0)
            {
              goto out;
            }
        }

      priv->power = (uint8_t)power;
    }
  else if (priv->power > 0)
    {
      if (priv->lcd->backlight != NULL)
        {
          ret = priv->lcd->backlight(priv->lcd, 0);
          if (ret < 0)
            {
              goto out;
            }
        }

      ret = gc9d01_command(priv, GC9D01_DISPOFF, NULL, 0);
      if (ret < 0)
        {
          goto out;
        }

      ret = gc9d01_command(priv, GC9D01_SLPIN, NULL, 0);
      if (ret < 0)
        {
          goto out;
        }

      (void)nxsig_usleep(120000);
      priv->awake = false;
      priv->power = 0;
    }

  ret = OK;

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int gc9d01_getcontrast(FAR struct lcd_dev_s *dev)
{
  (void)dev;
  return -ENOSYS;
}

static int gc9d01_setcontrast(FAR struct lcd_dev_s *dev,
                              unsigned int contrast)
{
  (void)dev;
  (void)contrast;
  return -ENOSYS;
}

static int gc9d01_hwinitialize(FAR struct gc9d01_dev_s *priv)
{
  size_t i;
  int ret;

  ret = priv->lcd->reset(priv->lcd, true);
  if (ret < 0)
    {
      return ret;
    }

  (void)nxsig_usleep(100000);
  ret = priv->lcd->reset(priv->lcd, false);
  if (ret < 0)
    {
      return ret;
    }

  (void)nxsig_usleep(120000);

  for (i = 0; i < nitems(g_gc9d01_init); i++)
    {
      FAR const struct gc9d01_initcmd_s *entry = &g_gc9d01_init[i];

      ret = gc9d01_command(priv, entry->cmd, entry->params,
                            entry->nparams);
      if (ret < 0)
        {
          return ret;
        }

      if (entry->delay_ms > 0)
        {
          (void)nxsig_usleep((useconds_t)entry->delay_ms * 1000u);
        }
    }

  if (priv->lcd->backlight != NULL)
    {
      ret = priv->lcd->backlight(priv->lcd, 0);
      if (ret < 0)
        {
          return ret;
        }
    }

  priv->awake = true;
  priv->power = 0;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct lcd_dev_s *gc9d01_lcdinitialize(
  FAR struct gc9d01_lcd_s *lcd, int devno)
{
  FAR struct gc9d01_dev_s *priv;
  int ret;

  if (lcd == NULL || lcd->reset == NULL || lcd->writecmd == NULL ||
      lcd->writegram == NULL || devno < 0 ||
      devno >= CONFIG_LCD_GC9D01_NINTERFACES)
    {
      return NULL;
    }

  priv = &g_gc9d01[devno];
  if (priv->lcd != NULL)
    {
      return NULL;
    }

  memset(priv, 0, sizeof(*priv));
  nxmutex_init(&priv->lock);
  priv->dev.getvideoinfo = gc9d01_getvideoinfo;
  priv->dev.getplaneinfo = gc9d01_getplaneinfo;
  priv->dev.getpower = gc9d01_getpower;
  priv->dev.setpower = gc9d01_setpower;
  priv->dev.getcontrast = gc9d01_getcontrast;
  priv->dev.setcontrast = gc9d01_setcontrast;
  priv->lcd = lcd;

  ret = gc9d01_hwinitialize(priv);
  if (ret < 0)
    {
      lcderr("ERROR: GC9D01 initialization failed: %d\n", ret);
      priv->lcd = NULL;
      nxmutex_destroy(&priv->lock);
      return NULL;
    }

  lcdinfo("GC9D01 instance %d initialized\n", devno);
  return &priv->dev;
}

#endif /* CONFIG_LCD_GC9D01 */
