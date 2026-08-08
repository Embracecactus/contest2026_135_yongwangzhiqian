/****************************************************************************
 * board/bk7258_t5ai/chip/ap/bk7258_peripherals.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register AP-owned peripheral lower halves that publish a NuttX character
 * device on their own.  Object-returning lower halves (GPIOE, I2S, SDIO and
 * SPI) remain available to their board or upper-half consumer and are not
 * claimed here.  LCD registration is driven by LCD_EXTERNINIT.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <arch/chip/bk7258_peripherals.h>

#ifdef CONFIG_BK7258_AUD
#  include <arch/chip/bk7258_aud.h>
#endif
#ifdef CONFIG_BK7258_I2C
#  include <arch/chip/bk7258_i2c.h>
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
#ifdef CONFIG_BK7258_SDMADC
#  include <arch/chip/bk7258_sdmadc.h>
#endif
#ifdef CONFIG_BK7258_TIMER
#  include <arch/chip/bk7258_timer.h>
#endif

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

  (void)ret;
  return 0;
}
