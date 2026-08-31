/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK board hook: UART0 console, audio, SD NAND, dual GC9D01 displays,
 * GC2145 camera, sensors, NFC, battery status and recovery transports.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <syslog.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_gpio.h>
#include <arch/chip/bk7258_ota_source_usb.h>
#include <arch/chip/bk7258_usbmode.h>

#ifdef CONFIG_BK7258_AP_CORE
#ifdef CONFIG_BK7258_AIDK_CAMERA
extern int bk7258_aidk_camera_initialize(void);
#endif
#ifdef CONFIG_BK7258_AIDK_SC7A20_PHASE0
extern int bk7258_aidk_sc7a20_phase0_probe(void);
#endif
#ifdef CONFIG_BK7258_AIDK_SC7A20
extern int bk7258_aidk_sc7a20_initialize(void);
#endif
#ifdef CONFIG_BK7258_AIDK_MFRC522
extern int bk7258_aidk_mfrc522_initialize(void);
#endif
#ifdef CONFIG_BK7258_AIDK_BATTERY
extern int bk7258_aidk_battery_initialize(void);
#endif
#ifdef CONFIG_BK7258_AIDK_DUAL_LCD
extern int bk7258_aidk_dual_lcd_initialize(void);
#endif
static const struct bk7258_mic_config_s g_bk7258_aidk_mic_config =
{
  /* MIC1 is the primary microphone; MIC2 carries the speaker loopback used
   * as the AEC reference.  The input flag names are SoC ADC input names, not
   * a claim that two physical microphones are fitted.
   */

  .channels = BK7258_BOARD_CAPTURE_CHANNELS,
  .flags = BK7258_MIC_INPUT_MIC1 | BK7258_MIC_INPUT_MIC2 |
           BK7258_MIC_INPUT_MIC2_AEC_REFERENCE,
  .aec_delay_samples = BK7258_BOARD_MIC_AEC_DELAY_SAMPLES,
  .mic1_ana_gain = BK7258_BOARD_MIC1_ANA_GAIN,
  .mic2_ana_gain = BK7258_BOARD_MIC2_ANA_GAIN,
  .variant_name = BK7258_BOARD_VARIANT_NAME,
};

#ifdef CONFIG_BK7258_SDIO
extern const struct bk7258_sdio_pin_config_s g_bk7258_board_sdio_pins;
extern int bk7258_board_sdio_prepare(bool widebus);
extern bool bk7258_board_sdio_card_present(void);

static const struct bk7258_sdio_board_s g_bk7258_aidk_sdio =
{
  .pins = &g_bk7258_board_sdio_pins,
  .card_detect_available = false,
  .media_poll_ms = BK7258_BOARD_SDIO_MEDIA_POLL_MS,
  .prepare = bk7258_board_sdio_prepare,
  .card_present = bk7258_board_sdio_card_present,
};
#endif
#endif /* CONFIG_BK7258_AP_CORE */

const struct bk7258_gpio_config_s g_bk7258_board_gpio_config =
{
  .name                    = BK7258_BOARD_VARIANT_NAME,
  .user_led_gpio           = BK7258_BOARD_USER_LED_GPIO,
  .user_led_active_high    = BK7258_BOARD_USER_LED_ACTIVE_HIGH,
  .user_led_console_shared = BK7258_BOARD_USER_LED_CONSOLE_SHARED,
  .user_button_gpio        = BK7258_BOARD_USER_BUTTON_GPIO,
  .user_button_active_low  = BK7258_BOARD_USER_BUTTON_ACTIVE_LOW,
};

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_board_ap_initialize(void)
{
  FAR const struct bk7258_aud_board_s *audio = NULL;
  FAR const struct bk7258_sdio_board_s *sdio = NULL;
  int ret;

#ifdef CONFIG_BK7258_AUD
  audio = &g_bk7258_board_audio;
#endif

#ifdef CONFIG_BK7258_SDIO
  sdio = &g_bk7258_aidk_sdio;
#endif

  ret = bk7258_board_ap_controllers_initialize(
          &g_bk7258_aidk_mic_config, audio);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_AIDK_SC7A20_PHASE0
  ret = bk7258_aidk_sc7a20_phase0_probe();
  if (ret < 0)
    {
      return ret;
    }
#endif

  ret = bk7258_board_ap_buses_initialize(NULL, sdio);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_AIDK_DUAL_LCD
  /* SDIO initialization above releases the SDK's stale P2-P4 mapping before
   * LCD1 claims QSPI1.  Initialize LCD2 before camera and battery bring-up,
   * which subsequently reclaim QSPI0's unused P27 and P26 lanes.
   */

  ret = bk7258_aidk_dual_lcd_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK dual GC9D01 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_AIDK_CAMERA
  ret = bk7258_aidk_camera_initialize();
  if (ret < 0)
    {
      /* Match the proven T5-Board policy: an attached camera is useful but
       * it is not a prerequisite for AP/RPTUN health or signed recovery.
       * Keep the failure visible while allowing the USB OTA path below to
       * start, so a camera-only fault can be repaired without another full
       * provisioning cycle.
       */

      syslog(LOG_ERR, "AIDK GC2145 registration failed: %d\n", ret);
    }
#endif

  ret = bk7258_board_ap_finalize_initialize();
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_AIDK_SC7A20
  ret = bk7258_aidk_sc7a20_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK SC7A20H init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_AIDK_MFRC522
  ret = bk7258_aidk_mfrc522_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK MFRC522 init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_AIDK_BATTERY
  ret = bk7258_aidk_battery_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK battery init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_USBMODE
  ret = bk7258_usbmode_initialize();
  if (ret < 0)
    {
      return ret;
    }
#elif defined(CONFIG_BK7258_OTA_SOURCE_USB)
  ret = bk7258_ota_source_usb_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

  return OK;
}
#endif /* CONFIG_BK7258_AP_CORE */
