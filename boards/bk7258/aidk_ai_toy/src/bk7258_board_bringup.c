/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK board hook: UART0 console, audio, SD NAND, dual GC9D01 displays,
 * GC2145 camera, sensors, NFC, battery status and recovery transports.
 ****************************************************************************/

#include <nuttx/config.h>

#include <sched.h>
#include <stdbool.h>
#include <syslog.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>

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

#ifdef CONFIG_BK7258_BOARD_DEFERRED_INIT
static bool g_bk7258_aidk_deferred_started;

#  ifdef CONFIG_BK7258_AP_SUPERVISOR
_Static_assert(CONFIG_BK7258_AIDK_DEFERRED_PRIORITY <
               CONFIG_BK7258_AP_SUPERVISOR_PRIORITY,
               "AIDK device worker must remain below the AP supervisor");
#  endif
#endif

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
  int ret;

#ifdef CONFIG_BK7258_AUD
  audio = &g_bk7258_board_audio;
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

  /* RTC, ADC and object-only buses are bounded startup prerequisites.  The
   * soldered SD NAND probe is deliberately omitted here: MMC identification
   * is media I/O and must never gate the AP READY/heartbeat contract.
   */

  ret = bk7258_board_ap_buses_initialize(NULL, NULL);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_board_ap_finalize_initialize();
  if (ret < 0)
    {
      return ret;
    }

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

#ifdef CONFIG_BK7258_BOARD_DEFERRED_INIT
static int bk7258_aidk_deferred_worker(int argc, FAR char *argv[])
{
  clock_t started = clock_systime_ticks();
  unsigned int failures = 0;
  int ret;

  (void)argc;
  (void)argv;

  syslog(LOG_INFO, "AIDK DEFERRED START\n");

#ifdef CONFIG_BK7258_SDIO
  syslog(LOG_INFO, "AIDK DEFERRED stage=sdio-enter\n");
  ret = bk7258_board_ap_sdio_initialize(&g_bk7258_aidk_sdio);
  if (ret < 0)
    {
      failures++;
      syslog(LOG_ERR,
             "AIDK DEFERRED stage=sdio-fail ret=%d elapsed=%lu ms\n",
             ret,
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
  else
    {
      syslog(LOG_INFO, "AIDK DEFERRED stage=sdio-pass elapsed=%lu ms\n",
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
#endif

#ifdef CONFIG_BK7258_AIDK_DUAL_LCD
  /* SDIO preparation releases the SDK's stale P2-P4 mapping before LCD1
   * claims QSPI1.  LCD2 precedes camera and battery, which later reclaim
   * QSPI0's unused P27 and P26 lanes.
   */

  syslog(LOG_INFO, "AIDK DEFERRED stage=lcd-enter\n");
  ret = bk7258_aidk_dual_lcd_initialize();
  if (ret < 0)
    {
      failures++;
      syslog(LOG_ERR,
             "AIDK DEFERRED stage=lcd-fail ret=%d elapsed=%lu ms\n",
             ret,
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
  else
    {
      syslog(LOG_INFO, "AIDK DEFERRED stage=lcd-pass elapsed=%lu ms\n",
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
#endif

#ifdef CONFIG_BK7258_AIDK_CAMERA
  syslog(LOG_INFO, "AIDK DEFERRED stage=camera-enter\n");
  ret = bk7258_aidk_camera_initialize();
  if (ret < 0)
    {
      failures++;
      syslog(LOG_ERR,
             "AIDK DEFERRED stage=camera-fail ret=%d elapsed=%lu ms\n",
             ret,
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
  else
    {
      syslog(LOG_INFO, "AIDK DEFERRED stage=camera-pass elapsed=%lu ms\n",
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
#endif

#ifdef CONFIG_BK7258_AIDK_SC7A20
  syslog(LOG_INFO, "AIDK DEFERRED stage=sc7a20-enter\n");
  ret = bk7258_aidk_sc7a20_initialize();
  if (ret < 0)
    {
      failures++;
      syslog(LOG_ERR,
             "AIDK DEFERRED stage=sc7a20-fail ret=%d elapsed=%lu ms\n",
             ret,
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
  else
    {
      syslog(LOG_INFO,
             "AIDK DEFERRED stage=sc7a20-pass elapsed=%lu ms\n",
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
#endif

#ifdef CONFIG_BK7258_AIDK_MFRC522
  syslog(LOG_INFO, "AIDK DEFERRED stage=mfrc522-enter\n");
  ret = bk7258_aidk_mfrc522_initialize();
  if (ret < 0)
    {
      failures++;
      syslog(LOG_ERR,
             "AIDK DEFERRED stage=mfrc522-fail ret=%d elapsed=%lu ms\n",
             ret,
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
  else
    {
      syslog(LOG_INFO,
             "AIDK DEFERRED stage=mfrc522-pass elapsed=%lu ms\n",
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
#endif

#ifdef CONFIG_BK7258_AIDK_BATTERY
  syslog(LOG_INFO, "AIDK DEFERRED stage=battery-enter\n");
  ret = bk7258_aidk_battery_initialize();
  if (ret < 0)
    {
      failures++;
      syslog(LOG_ERR,
             "AIDK DEFERRED stage=battery-fail ret=%d elapsed=%lu ms\n",
             ret,
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
  else
    {
      syslog(LOG_INFO,
             "AIDK DEFERRED stage=battery-pass elapsed=%lu ms\n",
             (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
    }
#endif

  syslog(failures == 0 ? LOG_INFO : LOG_WARNING,
         "AIDK DEFERRED DONE failures=%u elapsed=%lu ms\n", failures,
         (unsigned long)TICK2MSEC(clock_systime_ticks() - started));
  return OK;
}

int bk7258_board_ap_deferred_initialize(void)
{
  int pid;

  if (g_bk7258_aidk_deferred_started)
    {
      return OK;
    }

  /* bk7258_ap_main calls this immediately after publishing READY while its
   * coordinator priority still outranks this task.  kthread_create therefore
   * cannot transfer control into media probing before the primary heartbeat
   * enters its supervisor loop.
   */

  pid = kthread_create("aidk-devices",
                       CONFIG_BK7258_AIDK_DEFERRED_PRIORITY,
                       CONFIG_BK7258_AIDK_DEFERRED_STACKSIZE,
                       bk7258_aidk_deferred_worker, NULL);
  if (pid < 0)
    {
      return pid;
    }

  g_bk7258_aidk_deferred_started = true;
  syslog(LOG_INFO, "AIDK DEFERRED SCHEDULED pid=%d priority=%d\n",
         pid, CONFIG_BK7258_AIDK_DEFERRED_PRIORITY);
  return OK;
}
#endif
#endif /* CONFIG_BK7258_AP_CORE */
