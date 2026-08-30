/****************************************************************************
 * chips/bk7258/ap/bk7258_lcd_spi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SPI-over-QSPI transport for NuttX LCD panel drivers.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_LCD_SPI

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_lcd_spi.h>

#include <sdkconfig.h>
#include <driver/gpio.h>
#include <driver/lcd_spi.h>
#include <driver/lcd_types.h>

#define BK7258_LCD_SPI_CONTROLLERS     2
#define BK7258_LCD_SPI_PIXEL_BYTES     2u
#define BK7258_LCD_SPI_DMA_ALIGNMENT  16u

#if !CONFIG_LCD_SPI_REFRESH_WITH_QSPI_MAPPING_MODE
#  error "BK7258 NuttX LCD transport requires QSPI mapping-mode refresh"
#endif

#if CONFIG_LCD_SPI_COLOR_DEPTH_BYTE != BK7258_LCD_SPI_PIXEL_BYTES
#  error "BK7258 NuttX LCD transport requires the SDK RGB565 data path"
#endif

struct bk7258_lcd_spi_bus_s
{
  struct bk7258_lcd_spi_config_s config;
  FAR uint8_t *txbuf_alloc;
  FAR uint8_t *txbuf;
  size_t txbuf_bytes;
};

static mutex_t g_bk7258_lcd_spi_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_lcd_spi_used[BK7258_LCD_SPI_CONTROLLERS];

int bk7258_lcd_spi_bus_initialize(
  FAR const struct bk7258_lcd_spi_config_s *config,
  FAR struct bk7258_lcd_spi_bus_s **bus)
{
  FAR struct bk7258_lcd_spi_bus_s *priv;
  lcd_qspi_init_cmd_t empty_init = {0};
  lcd_spi_t spi_config;
  lcd_device_t device;
  size_t bytes;
  int ret;

  if (config == NULL || bus == NULL || config->name == NULL ||
      config->spi_id >= BK7258_LCD_SPI_CONTROLLERS ||
      config->width == 0 || config->height == 0)
    {
      return -EINVAL;
    }

  bytes = (size_t)config->width * config->height *
          BK7258_LCD_SPI_PIXEL_BYTES;
  if (bytes / BK7258_LCD_SPI_PIXEL_BYTES / config->width != config->height)
    {
      return -EOVERFLOW;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->txbuf_alloc = kmm_malloc(bytes + BK7258_LCD_SPI_DMA_ALIGNMENT - 1u);
  if (priv->txbuf_alloc == NULL)
    {
      kmm_free(priv);
      return -ENOMEM;
    }

  priv->txbuf = (FAR uint8_t *)
    (((uintptr_t)priv->txbuf_alloc + BK7258_LCD_SPI_DMA_ALIGNMENT - 1u) &
     ~(uintptr_t)(BK7258_LCD_SPI_DMA_ALIGNMENT - 1u));
  priv->txbuf_bytes = bytes;
  memcpy(&priv->config, config, sizeof(*config));

  ret = nxmutex_lock(&g_bk7258_lcd_spi_lock);
  if (ret < 0)
    {
      kmm_free(priv->txbuf_alloc);
      kmm_free(priv);
      return ret;
    }

  if (g_bk7258_lcd_spi_used[config->spi_id])
    {
      nxmutex_unlock(&g_bk7258_lcd_spi_lock);
      kmm_free(priv->txbuf_alloc);
      kmm_free(priv);
      return -EBUSY;
    }

  g_bk7258_lcd_spi_used[config->spi_id] = true;
  nxmutex_unlock(&g_bk7258_lcd_spi_lock);

  /* The SDK owns only the BK7258 QSPI/DMA controller implementation.  This
   * empty descriptor initializes that transport; the NuttX panel driver owns
   * every GC9D01 command and state transition.
   */

  memset(&spi_config, 0, sizeof(spi_config));
  spi_config.clk = LCD_QSPI_60M;
  spi_config.init_cmd = &empty_init;
  spi_config.device_init_cmd_len = 0;
  spi_config.frame_len = bytes;

  memset(&device, 0, sizeof(device));
  device.name = (FAR char *)config->name;
  device.type = LCD_TYPE_SPI;
  device.width = config->width;
  device.height = config->height;
  device.spi = &spi_config;

  bk_lcd_spi_init(config->spi_id, &device, config->reset_gpio,
                  config->dc_gpio);

  *bus = priv;
  return OK;
}

void bk7258_lcd_spi_bus_uninitialize(
  FAR struct bk7258_lcd_spi_bus_s *bus)
{
  if (bus == NULL)
    {
      return;
    }

  bk_lcd_spi_deinit(bus->config.spi_id, bus->config.reset_gpio,
                    bus->config.dc_gpio);

  if (nxmutex_lock(&g_bk7258_lcd_spi_lock) == OK)
    {
      g_bk7258_lcd_spi_used[bus->config.spi_id] = false;
      nxmutex_unlock(&g_bk7258_lcd_spi_lock);
    }

  kmm_free(bus->txbuf_alloc);
  kmm_free(bus);
}

int bk7258_lcd_spi_reset(FAR struct bk7258_lcd_spi_bus_s *bus,
                         bool asserted)
{
  bk_err_t ret;

  if (bus == NULL)
    {
      return -EINVAL;
    }

  ret = bk_gpio_set_output_value((gpio_id_t)bus->config.reset_gpio,
                                 asserted ? 0 : 1);
  return ret == BK_OK ? OK : -EIO;
}

int bk7258_lcd_spi_writecmd(FAR struct bk7258_lcd_spi_bus_s *bus,
                            uint8_t cmd, FAR const uint8_t *params,
                            size_t nparams)
{
  if (bus == NULL || (nparams > 0 && params == NULL) ||
      nparams > UINT32_MAX)
    {
      return -EINVAL;
    }

  bk_lcd_spi_send_cmd(bus->config.spi_id, cmd);
  if (nparams > 0)
    {
      bk_lcd_spi_send_data(bus->config.spi_id, (FAR uint8_t *)params,
                           (uint32_t)nparams);
    }

  return OK;
}

int bk7258_lcd_spi_writegram(FAR struct bk7258_lcd_spi_bus_s *bus,
                             FAR const uint16_t *pixels, size_t width,
                             size_t height, size_t stride)
{
  FAR const uint8_t *src;
  FAR uint8_t *dst;
  size_t row_bytes;
  size_t bytes;
  size_t y;
  size_t x;

  if (bus == NULL || pixels == NULL || width == 0 || height == 0)
    {
      return -EINVAL;
    }

  row_bytes = width * BK7258_LCD_SPI_PIXEL_BYTES;
  if (row_bytes / BK7258_LCD_SPI_PIXEL_BYTES != width ||
      stride < row_bytes || height > SIZE_MAX / row_bytes)
    {
      return -EOVERFLOW;
    }

  bytes = row_bytes * height;
  if (bytes > bus->txbuf_bytes || bytes > UINT32_MAX)
    {
      return -E2BIG;
    }

  src = (FAR const uint8_t *)pixels;
  dst = bus->txbuf;
  for (y = 0; y < height; y++)
    {
      for (x = 0; x < row_bytes; x += BK7258_LCD_SPI_PIXEL_BYTES)
        {
          *dst++ = src[x + 1u];
          *dst++ = src[x];
        }

      src += stride;
    }

  bk_lcd_spi_send_data_with_qspi_mapping_mode(bus->config.spi_id,
                                               bus->txbuf,
                                               (uint32_t)bytes);
  return bk_lcd_spi_wait_display_complete(bus->config.spi_id) == BK_OK ?
         OK : -EIO;
}

#endif /* CONFIG_BK7258_LCD_SPI */
