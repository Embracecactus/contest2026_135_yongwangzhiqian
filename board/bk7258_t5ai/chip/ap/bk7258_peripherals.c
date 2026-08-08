/****************************************************************************
 * board/bk7258_t5ai/chip/ap/bk7258_peripherals.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register AP-owned peripheral lower halves.  Self-registering drivers
 * (AUD, I2C, MIC, RTC, SARADC, SDMADC, TIMER) publish their character
 * device directly and are fatal on failure.  Object-returning lower halves
 * (GPIOE, I2S, LCD, SDIO, SPI) are bound here to their NuttX upper half so
 * the objects are actually reachable from user space; those bindings are
 * best-effort because an absent daughter board must not park the AP.
 * The LCD is reached indirectly: fb_register() calls up_fbinitialize(),
 * which calls board_graphics_setup() under LCD_EXTERNINIT.
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>

#include <arch/chip/bk7258_peripherals.h>

#ifdef CONFIG_BK7258_AUD
#  include <arch/chip/bk7258_aud.h>
#endif
#ifdef CONFIG_BK7258_GPIOE
#  include <nuttx/ioexpander/gpio.h>
#  include <nuttx/ioexpander/ioexpander.h>
#  include <arch/chip/bk7258_gpioe.h>
#endif
#ifdef CONFIG_BK7258_I2C
#  include <arch/chip/bk7258_i2c.h>
#endif
#ifdef CONFIG_BK7258_I2S
#  include <nuttx/audio/i2s.h>
#  include <arch/chip/bk7258_i2s.h>
#endif
#if defined(CONFIG_BK7258_LCD) && defined(CONFIG_VIDEO_FB)
#  include <nuttx/video/fb.h>
#endif
#ifdef CONFIG_BK7258_MIC
#  include <arch/chip/bk7258_mic.h>
#endif
#ifdef CONFIG_BK7258_RTC
#  include <arch/chip/bk7258_rtc.h>
#endif
#ifdef CONFIG_BK7258_SARADC
#  include <arch/chip/bk7258_saradc.h>
#endif
#ifdef CONFIG_BK7258_SDIO
#  include <nuttx/mmcsd.h>
#  include <nuttx/sdio.h>
#  include <arch/chip/bk7258_sdio.h>
#endif
#ifdef CONFIG_BK7258_SDMADC
#  include <arch/chip/bk7258_sdmadc.h>
#endif
#ifdef CONFIG_BK7258_SPI
#  include <nuttx/spi/spi.h>
#  include <nuttx/spi/spi_transfer.h>
#  include <arch/chip/bk7258_spi.h>
#endif
#ifdef CONFIG_BK7258_TIMER
#  include <arch/chip/bk7258_timer.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_SDIO_SLOTNO
#  define CONFIG_BK7258_SDIO_SLOTNO     0
#endif

#ifndef CONFIG_BK7258_I2S_MINOR
#  define CONFIG_BK7258_I2S_MINOR       0
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BK7258_GPIOE
/****************************************************************************
 * Name: bk7258_gpioe_bind
 *
 * Description:
 *   Publish the I/O expander pins as /dev/gpioN character devices.  Pins
 *   are exposed as inputs: the caller decides the direction at runtime via
 *   the GPIO ioctl interface, so claiming an output pintype here could
 *   drive a level onto a line the board has not configured yet.
 *
 ****************************************************************************/

static void bk7258_gpioe_bind(void)
{
  FAR struct ioexpander_dev_s *ioe;

  ioe = bk7258_gpioe_initialize();
  if (ioe == NULL)
    {
      gpioerr("ERROR: bk7258_gpioe_initialize failed\n");
      return;
    }

#ifdef CONFIG_GPIO_LOWER_HALF
  for (unsigned int pin = 0; pin < CONFIG_BK7258_GPIOE_NPINS; pin++)
    {
      int ret = gpio_lower_half(ioe, pin, GPIO_INPUT_PIN, (int)pin);
      if (ret < 0)
        {
          gpioerr("ERROR: gpio_lower_half pin %u failed: %d\n", pin, ret);
        }
    }
#endif
}
#endif

#ifdef CONFIG_BK7258_I2S
/****************************************************************************
 * Name: bk7258_i2s_bind
 *
 * Description:
 *   Bind the I2S lower half to the i2schar upper half so the bus is
 *   reachable as /dev/i2scharN.  Without AUDIO_I2SCHAR the object has no
 *   in-tree consumer and is left for a board-specific audio codec.
 *
 ****************************************************************************/

static void bk7258_i2s_bind(void)
{
  FAR struct i2s_dev_s *i2s;

  i2s = bk7258_i2s_initialize();
  if (i2s == NULL)
    {
      auderr("ERROR: bk7258_i2s_initialize failed\n");
      return;
    }

#ifdef CONFIG_AUDIO_I2SCHAR
  int ret = i2schar_register(i2s, CONFIG_BK7258_I2S_MINOR);
  if (ret < 0)
    {
      auderr("ERROR: i2schar_register failed: %d\n", ret);
    }
#endif
}
#endif

#ifdef CONFIG_BK7258_SDIO
/****************************************************************************
 * Name: bk7258_sdio_bind
 *
 * Description:
 *   Attach the SDIO lower half to the MMC/SD upper half.  A missing or
 *   unpowered card is normal at boot: mmcsd_slotinitialize only probes the
 *   card, and the slot stays usable once media is inserted.
 *
 ****************************************************************************/

static void bk7258_sdio_bind(void)
{
  FAR struct sdio_dev_s *sdio = NULL;
  int ret;

  ret = bk7258_sdio_initialize(&sdio);
  if (ret < 0)
    {
      mcerr("ERROR: bk7258_sdio_initialize failed: %d\n", ret);
      return;
    }

#ifdef CONFIG_MMCSD_SDIO
  /* The lower half has no hotplug detect: status() always reports the card
   * present, so mmcsd_slotinitialize() probes the slot directly and there is
   * no sdio_mediachange() to call.
   */

  ret = mmcsd_slotinitialize(CONFIG_BK7258_SDIO_SLOTNO, sdio);
  if (ret < 0)
    {
      mcerr("ERROR: mmcsd_slotinitialize failed: %d\n", ret);
    }
#endif
}
#endif

#ifdef CONFIG_BK7258_SPI
/****************************************************************************
 * Name: bk7258_spi_bind
 *
 * Description:
 *   Publish the SPI master as /dev/spiN so transfers can be driven from
 *   user space.  Chip select stays under board control via
 *   bk7258_spi_set_csinfo().
 *
 ****************************************************************************/

static void bk7258_spi_bind(void)
{
  FAR struct spi_dev_s *spi = NULL;
  int ret;

  ret = bk7258_spi_initialize(&spi);
  if (ret < 0)
    {
      spierr("ERROR: bk7258_spi_initialize failed: %d\n", ret);
      return;
    }

#ifdef CONFIG_SPI_DRIVER
  ret = spi_register(spi, 0);
  if (ret < 0)
    {
      spierr("ERROR: spi_register failed: %d\n", ret);
    }
#endif
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_peripherals_initialize(void)
{
  int ret;

#ifdef CONFIG_BK7258_AUD
  ret = bk7258_aud_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_I2C
  ret = bk7258_i2c_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_MIC
  ret = bk7258_mic_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_RTC
  ret = bk7258_rtc_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_SARADC
  ret = bk7258_saradc_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_SDMADC
  ret = bk7258_sdmadc_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_TIMER
  ret = bk7258_timer_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

  /* Object-returning lower halves.  These are best-effort: a failure means
   * the peripheral is unavailable, not that the AP is unhealthy, so we log
   * and continue instead of parking the core.
   */

#ifdef CONFIG_BK7258_GPIOE
  bk7258_gpioe_bind();
#endif

#ifdef CONFIG_BK7258_I2S
  bk7258_i2s_bind();
#endif

#ifdef CONFIG_BK7258_SDIO
  bk7258_sdio_bind();
#endif

#ifdef CONFIG_BK7258_SPI
  bk7258_spi_bind();
#endif

#if defined(CONFIG_BK7258_LCD) && defined(CONFIG_VIDEO_FB)
  /* fb_register() drives up_fbinitialize(), which is what reaches the panel
   * through board_graphics_setup() under LCD_EXTERNINIT.  Without this call
   * nothing references the LCD lower half and the linker discards it.
   */

  ret = fb_register(0, 0);
  if (ret < 0)
    {
      lcderr("ERROR: fb_register failed: %d\n", ret);
    }
#endif

  (void)ret;
  return 0;
}
