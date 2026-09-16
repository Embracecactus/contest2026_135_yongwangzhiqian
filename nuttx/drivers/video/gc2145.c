/****************************************************************************
 * drivers/video/gc2145.c
 * SPDX-License-Identifier: Apache-2.0
 *
 * GC2145 register controls. Register semantics: GalaxyCore GC2145 CSP
 * DataSheet V1.0 (2013-12-01), pages 22, 23, 32 and 35.
 ****************************************************************************/
#include <nuttx/config.h>
#ifdef CONFIG_VIDEO_GC2145_CONTROLS
#include <errno.h>
#include <string.h>
#include <nuttx/video/gc2145.h>

#define GC2145_PAGE       0xfe
#define GC2145_EXPOSURE_H 0x03
#define GC2145_EXPOSURE_L 0x04
#define GC2145_FLIP       0x17
#define GC2145_AE         0xb6

static const int32_t g_gc2145_ae[] =
{
  IMGSENSOR_EXPOSURE_AUTO, IMGSENSOR_EXPOSURE_MANUAL
};

int gc2145_get_supported_value(uint32_t id,
                               FAR imgsensor_supported_value_t *value)
{
  if (value == NULL)
    {
      return -EINVAL;
    }

  memset(value, 0, sizeof(*value));
  value->type = IMGSENSOR_CTRL_TYPE_INTEGER;
  value->u.range.step = 1;
  switch (id)
    {
      case IMGSENSOR_ID_EXPOSURE_AUTO:
        value->type = IMGSENSOR_CTRL_TYPE_INTEGER_MENU;
        value->u.discrete.nr_values = 2;
        value->u.discrete.values = g_gc2145_ae;
        value->u.discrete.default_value = IMGSENSOR_EXPOSURE_AUTO;
        return 0;
      case IMGSENSOR_ID_EXPOSURE:
        value->u.range.minimum = 1;
        value->u.range.maximum = 8191;
        value->u.range.default_value = 1250;
        return 0;
      case IMGSENSOR_ID_HFLIP_VIDEO:
      case IMGSENSOR_ID_VFLIP_VIDEO:
        value->type = IMGSENSOR_CTRL_TYPE_BOOLEAN;
        value->u.range.maximum = 1;
        return 0;
      default:
        return -ENOTTY;
    }
}

static int gc2145_begin(FAR const struct gc2145_control_s *c,
                        FAR uint8_t *page)
{
  int ret;
  if (c == NULL || c->lock == NULL || c->unlock == NULL ||
      c->read == NULL || c->write == NULL)
    {
      return -EINVAL;
    }

  ret = c->lock(c->arg);
  if (ret < 0)
    {
      return ret;
    }

  ret = c->read(c->arg, GC2145_PAGE, page);
  if (ret >= 0 && *page != 0)
    {
      /* Do not replay reset bits from an unexpected control-page value. */
      ret = (*page & ~7u) != 0 ? -EIO :
            c->write(c->arg, GC2145_PAGE, 0);
    }

  if (ret < 0)
    {
      c->unlock(c->arg);
    }

  return ret;
}

static int gc2145_end(FAR const struct gc2145_control_s *c,
                      uint8_t page, int ret)
{
  int restore = page == 0 ? 0 : c->write(c->arg, GC2145_PAGE, page);
  c->unlock(c->arg);
  return ret < 0 ? ret : restore;
}

int gc2145_get_value(FAR const struct gc2145_control_s *c,
                     uint32_t id, uint32_t size,
                     FAR imgsensor_value_t *value)
{
  imgsensor_supported_value_t supported;
  uint8_t page;
  uint8_t hi = 0;
  uint8_t lo;
  uint8_t reg;
  int result;
  int ret;

  if (value == NULL || (size != 0 && size != sizeof(int32_t)))
    {
      return -EINVAL;
    }

  ret = gc2145_get_supported_value(id, &supported);
  if (ret < 0)
    {
      return ret;
    }

  ret = gc2145_begin(c, &page);
  if (ret < 0)
    {
      return ret;
    }

  reg = id == IMGSENSOR_ID_EXPOSURE_AUTO ? GC2145_AE :
        id == IMGSENSOR_ID_EXPOSURE ? GC2145_EXPOSURE_H : GC2145_FLIP;
  ret = c->read(c->arg, reg, &hi);
  result = hi;
  if (ret >= 0 && id == IMGSENSOR_ID_EXPOSURE)
    {
      ret = c->read(c->arg, GC2145_EXPOSURE_L, &lo);
      if (ret >= 0)
        {
          result = ((hi & 0x1f) << 8) | lo;
        }
    }
  else if (ret >= 0 && id == IMGSENSOR_ID_EXPOSURE_AUTO)
    {
      result = (hi & 1) != 0 ? IMGSENSOR_EXPOSURE_AUTO :
                               IMGSENSOR_EXPOSURE_MANUAL;
    }
  else if (ret >= 0 && id == IMGSENSOR_ID_HFLIP_VIDEO)
    {
      result = hi & 1;
    }
  else if (ret >= 0 && id == IMGSENSOR_ID_VFLIP_VIDEO)
    {
      result = (hi >> 1) & 1;
    }

  ret = gc2145_end(c, page, ret);
  if (ret >= 0)
    {
      value->value32 = result;
    }

  return ret;
}

int gc2145_set_value(FAR const struct gc2145_control_s *c,
                     uint32_t id, uint32_t size, imgsensor_value_t value)
{
  imgsensor_supported_value_t supported;
  uint8_t page;
  uint8_t current;
  uint8_t reg;
  int32_t setting = value.value32;
  int ret;

  if ((size != 0 && size != sizeof(int32_t)))
    {
      return -EINVAL;
    }

  ret = gc2145_get_supported_value(id, &supported);
  if (ret < 0)
    {
      return ret;
    }

  if (id == IMGSENSOR_ID_EXPOSURE_AUTO ?
      (setting != IMGSENSOR_EXPOSURE_AUTO &&
       setting != IMGSENSOR_EXPOSURE_MANUAL) :
      (setting < supported.u.range.minimum ||
       setting > supported.u.range.maximum))
    {
      return -ERANGE;
    }

  ret = gc2145_begin(c, &page);
  if (ret < 0)
    {
      return ret;
    }

  if (id == IMGSENSOR_ID_EXPOSURE)
    {
      ret = c->read(c->arg, GC2145_AE, &current);
      if (ret >= 0 && (current & 1) != 0)
        {
          ret = -EBUSY;
        }

      if (ret >= 0)
        {
          ret = c->read(c->arg, GC2145_EXPOSURE_H, &current);
        }

      if (ret >= 0)
        {
          ret = c->write(c->arg, GC2145_EXPOSURE_H,
                         (current & 0xe0) | (setting >> 8));
        }

      if (ret >= 0)
        {
          ret = c->write(c->arg, GC2145_EXPOSURE_L, setting & 0xff);
        }
    }
  else
    {
      reg = id == IMGSENSOR_ID_EXPOSURE_AUTO ? GC2145_AE : GC2145_FLIP;
      ret = c->read(c->arg, reg, &current);
      if (ret >= 0)
        {
          uint8_t mask = id == IMGSENSOR_ID_VFLIP_VIDEO ? 2 : 1;
          bool enabled = id == IMGSENSOR_ID_EXPOSURE_AUTO ?
                         setting == IMGSENSOR_EXPOSURE_AUTO : setting != 0;
          current = (current & ~mask) | (enabled ? mask : 0);
          ret = c->write(c->arg, reg, current);
        }
    }

  return gc2145_end(c, page, ret);
}
#endif
