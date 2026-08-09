/****************************************************************************
 * board/bk7258/chip/ap/bk7258_peripherals.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register AP-owned peripheral lower halves.  Self-registering drivers
 * (AUD, I2C, MIC, RTC, SARADC, SDMADC, TIMER) publish their character
 * device directly and are fatal on failure.  I2S, SDIO and SPI objects are
 * bound here to their NuttX upper halves; those bindings are best-effort
 * because an absent daughter board must not park the AP.  GPIOE remains an
 * object-only lower half: a board consumer must explicitly choose and claim
 * each pin before publishing a GPIO character device.
 * The LCD wrapper registers its PSRAM-backed framebuffer directly.
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>

#include <arch/chip/bk7258_peripherals.h>
#include <arch/board/board.h>
#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
#  include <arch/chip/bk7258_sdk_runtime.h>
#endif

#ifdef CONFIG_BK7258_AUD
#  include <arch/chip/bk7258_aud.h>
#endif
#ifdef CONFIG_BK7258_I2C
#  include <arch/chip/bk7258_i2c.h>
#endif
#ifdef CONFIG_BK7258_I2S
#  include <nuttx/audio/i2s.h>
#  include <arch/chip/bk7258_i2s.h>
#endif
#ifdef CONFIG_BK7258_LCD
#  include <arch/chip/bk7258_lcd.h>
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
  /* The lower half samples the T5-Board card-detect input.  Probe media that
   * is already present; interrupt-driven hotplug notification can be added
   * later without changing the controller lower half.
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

#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
  /* SDK-backed AP drivers share system-register and mailbox services.
   * Establish them before any driver can make a synchronous SDK request.
   */

  ret = bk7258_sdk_runtime_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

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

#ifdef CONFIG_BK7258_I2S
  bk7258_i2s_bind();
#endif

#ifdef CONFIG_BK7258_SDIO
  bk7258_sdio_bind();
#endif

#ifdef CONFIG_BK7258_SPI
  bk7258_spi_bind();
#endif

#ifdef CONFIG_BK7258_LCD
  ret = bk7258_lcd_initialize();
  if (ret < 0)
    {
      lcderr("ERROR: LCD framebuffer registration failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_GT1151
  ret = bk7258_board_gt1151_initialize();
  if (ret < 0)
    {
      ierr("ERROR: GT1151 registration failed: %d\n", ret);
    }
#endif

  (void)ret;
  return 0;
}
