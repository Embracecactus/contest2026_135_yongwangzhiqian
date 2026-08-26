/****************************************************************************
 * board/bk7258/boards/t5_board/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board-specific peripheral registration hooks.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <unistd.h>

#include <debug.h>

#if defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && \
    defined(CONFIG_AI_AGENT_LVGL_UI)
#  include <nuttx/semaphore.h>
#  include <lvgl/lvgl.h>
#  include <uikit/uikit.h>
#  ifdef CONFIG_BK7258_LVGL_FB_ACCEL
#    include <arch/chip/bk7258_lvgl_fb.h>
#  endif
#endif

#include <arch/board/board.h>
#include <arch/chip/bk7258_board_binding.h>
#include <arch/chip/bk7258_gpio.h>

/* Physical-device entry points are private to the selected T5-Board
 * composition.  Do not expose them through the logical board's public
 * <arch/board/board.h> facade.
 */

#ifdef CONFIG_BK7258_GT1151
int bk7258_board_gt1151_initialize(void);
#endif

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA
int bk7258_t5_board_camera_initialize(void);
#endif

#ifdef CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION
int bk7258_t5_board_rgb_lcd_backlight_validation_initialize(void);
#endif

#ifdef CONFIG_BK7258_T5_BOARD_TF_VALIDATION
int bk7258_t5_board_tf_validation_initialize(void);
#endif

static int bk7258_t5_board_mic_initialize(void)
{
  /* The two analog microphone routes are fixed on the T5-Board schematic;
   * no runtime pin mux is required for capture.
   */

  return OK;
}

static const struct bk7258_mic_config_s g_bk7258_t5_board_mic_config =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_mic_config_s),
  .channels = 2,
  .flags = BK7258_MIC_BINDING_MIC1 | BK7258_MIC_BINDING_MIC2,
  .variant_name = "T5-Board",
};

static const struct bk7258_mic_binding_s g_bk7258_t5_board_mic_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_mic_binding_s),
  .config = &g_bk7258_t5_board_mic_config,
  .initialize = bk7258_t5_board_mic_initialize,
};

#ifdef CONFIG_BK7258_T5_BOARD_TF_SLOT
/* The SDIO physical binding remains implemented in the dedicated source;
 * these declarations keep the aggregate descriptor independent from its
 * legacy helper names while the generic host consumes only typed callbacks.
 */

extern int bk7258_board_sdio_initialize(bool widebus);
extern bool bk7258_board_sdio_card_present(void);
#ifdef CONFIG_FS_FAT
extern int bk7258_t5_board_tf_mount_initialize(void);
#endif

static const struct bk7258_sdio_config_s g_bk7258_t5_board_sdio_config =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_sdio_config_s),
  .card_detect_available = false,
  .media_poll_ms = 0,
};

static const struct bk7258_sdio_binding_s g_bk7258_t5_board_sdio_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_sdio_binding_s),
  .config = &g_bk7258_t5_board_sdio_config,
  .initialize = bk7258_board_sdio_initialize,
  .card_present = bk7258_board_sdio_card_present,
};
#endif

static const struct bk7258_board_binding_s g_bk7258_t5_board_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_board_binding_s),
  .mic = &g_bk7258_t5_board_mic_binding,
#ifdef CONFIG_BK7258_T5_BOARD_TF_SLOT
  .sdio = &g_bk7258_t5_board_sdio_binding,
#else
  .sdio = NULL,
#endif
#ifdef CONFIG_BK7258_AUD
  .audio = &g_bk7258_board_audio_binding,
#else
  .audio = NULL,
#endif
  .early_initialize = bk7258_board_early_initialize,
  .devices_initialize = bk7258_board_devices_initialize,
};

const struct bk7258_board_binding_s *bk7258_board_get_binding(void)
{
  return &g_bk7258_t5_board_binding;
}
#ifdef CONFIG_BK7258_AUD_LIFECYCLE_VALIDATION
#  include <arch/chip/bk7258_aud.h>
#endif

#ifdef CONFIG_BK7258_LCD
#  include <arch/chip/bk7258_lcd.h>
#endif

#ifdef CONFIG_BK7258_T5_BOARD_SARADC_KEY_VALIDATION
#  include <arch/chip/bk7258_saradc.h>

_Static_assert(BK7258_BOARD_ADC_KEY_GPIO == 12,
               "T5-Board ADC key must remain on P12");
_Static_assert(BK7258_BOARD_ADC_KEY_SARADC_CHAN ==
               CONFIG_BK7258_SARADC_CHAN,
               "T5-Board ADC key requires SARADC channel 14");
_Static_assert(BK7258_BOARD_ADC_KEY_ACTIVE_LOW == 1,
               "T5-Board ADC key must remain active-low");

static const struct bk7258_saradc_validation_config_s
g_bk7258_t5_board_adc_key_validation =
{
  .devpath = CONFIG_BK7258_SARADC_DEVNAME,
  .binding_id = BK7258_BOARD_ADC_KEY_BINDING_ID,
  .expected_channel = BK7258_BOARD_ADC_KEY_SARADC_CHAN,
  .active_direction = BK7258_SARADC_ACTIVE_LOW,
  .samples_per_phase = 64,
  .initial_delay_ms = 3000,
  .settle_ms = 50,
  .poll_interval_ms = 20,
  .phase_timeout_ms = 30000,
  .transition_confirm_samples = 3,
  .minimum_delta_raw = 256,
  .minimum_delta_permille = 800,
  .minimum_release_tolerance_raw = 64,
  .release_tolerance_permille = 100,
  .maximum_noise_permille = 100,
};
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bk7258_gpio_config_s g_bk7258_t5_board_gpio_config =
{
  .version                 = BK7258_GPIO_BINDING_VERSION,
  .size                    = sizeof(struct bk7258_gpio_config_s),
  .name                    = BK7258_BOARD_VARIANT_NAME,
  .user_led_gpio           = BK7258_BOARD_USER_LED_GPIO,
  .user_led_active_high    = BK7258_BOARD_USER_LED_ACTIVE_HIGH,
  .user_led_console_shared = BK7258_BOARD_USER_LED_CONSOLE_SHARED,
  .user_button_gpio        = BK7258_BOARD_USER_BUTTON_GPIO,
  .user_button_active_low  = BK7258_BOARD_USER_BUTTON_ACTIVE_LOW,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#if defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && \
    defined(CONFIG_AI_AGENT_LVGL_UI)

#define T5_LVGL_TASK_PRIORITY  45
#define T5_LVGL_TASK_STACKSIZE 32768

static sem_t g_t5_lvgl_ready = SEM_INITIALIZER(0);
static int g_t5_lvgl_status = -EINPROGRESS;
static bool g_t5_lvgl_started;
volatile uint32_t g_t5_lvgl_iterations;
volatile uint32_t g_t5_lvgl_last_idle;
volatile int g_t5_lvgl_last_sleep;

static int bk7258_t5_board_lvgl_loop(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t descriptor;
  lv_nuttx_result_t result = {0};
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  FAR lv_display_t *display;
#endif
  uint32_t idle;

  (void)argc;
  (void)argv;

  if (lv_is_initialized())
    {
      g_t5_lvgl_status = -EBUSY;
      nxsem_post(&g_t5_lvgl_ready);
      return 1;
    }

  lv_init();
  lv_nuttx_dsc_init(&descriptor);
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  display = bk7258_lvgl_fb_create("/dev/fb0");
  if (display != NULL)
    {
      descriptor.fb_path = NULL;
    }
#endif
  lv_nuttx_init(&descriptor, &result);
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  if (display != NULL)
    {
      result.disp = display;
      if (result.indev != NULL &&
          bk7258_lvgl_fb_bind_touch(display, result.indev) < 0)
        {
          lv_display_delete(display);
          result.disp = NULL;
          display = NULL;
        }
    }
#endif

  if (result.disp == NULL || result.indev == NULL)
    {
      g_t5_lvgl_status = -ENODEV;
      nxsem_post(&g_t5_lvgl_ready);
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 1;
    }

  /* UIKit stores its font manager in LVGL's external global context.  The
   * official initialization order is lv_init(), lv_nuttx_init(), vg_init().
   * Agent UI creates its CJK font later from this same event-loop task.
   */

  vg_init();

  g_t5_lvgl_status = OK;
  nxsem_post(&g_t5_lvgl_ready);

  for (; ; )
    {
      idle = lv_timer_handler();
      if (idle > 20u)
        {
          idle = 20u;
        }

      g_t5_lvgl_last_idle = idle;
      g_t5_lvgl_iterations++;
      g_t5_lvgl_last_sleep = usleep((idle > 0 ? idle : 1u) * 1000u);
    }

  return 0;
}

int bk7258_board_lvgl_initialize(void)
{
  pid_t pid;

  if (g_t5_lvgl_started)
    {
      return g_t5_lvgl_status == -EINPROGRESS ? OK : g_t5_lvgl_status;
    }

  g_t5_lvgl_started = true;
  pid = task_create("t5-lvgl", T5_LVGL_TASK_PRIORITY,
                    T5_LVGL_TASK_STACKSIZE,
                    bk7258_t5_board_lvgl_loop, NULL);
  if (pid < 0)
    {
      g_t5_lvgl_started = false;
      g_t5_lvgl_status = (int)pid;
      return (int)pid;
    }

  return OK;
}

int bk7258_board_lvgl_wait_ready(void)
{
  int ret;

  if (!g_t5_lvgl_started)
    {
      return -EAGAIN;
    }

  if (g_t5_lvgl_status == -EINPROGRESS)
    {
      ret = nxsem_wait_uninterruptible(&g_t5_lvgl_ready);
      if (ret < 0)
        {
          return ret;
        }
    }

  return g_t5_lvgl_status;
}

#endif

FAR const struct bk7258_gpio_config_s *bk7258_board_gpio_config(void)
{
  return &g_bk7258_t5_board_gpio_config;
}

int bk7258_board_early_initialize(void)
{
#ifdef CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION
  return bk7258_t5_board_rgb_lcd_backlight_validation_initialize();
#else
  return OK;
#endif
}

int bk7258_board_devices_initialize(void)
{
  int ret = OK;

#if defined(CONFIG_BK7258_T5_BOARD_TF_SLOT) && defined(CONFIG_FS_FAT) && \
    !defined(CONFIG_BK7258_T5_BOARD_TF_VALIDATION)
  ret = bk7258_t5_board_tf_mount_initialize();
  if (ret < 0)
    {
      _err("ERROR: T5-Board TF mount worker failed: %d\n", ret);
    }
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

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA
  ret = bk7258_t5_board_camera_initialize();
  if (ret < 0)
    {
      verr("ERROR: T5-Board camera registration failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_BK7258_T5_BOARD_TF_VALIDATION
  ret = bk7258_t5_board_tf_validation_initialize();
  if (ret < 0)
    {
      _err("ERROR: T5-Board TF validation worker failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_AUD_LIFECYCLE_VALIDATION
  ret = bk7258_aud_validation_start();
  if (ret < 0)
    {
      _err("ERROR: T5-Board speaker validation worker failed: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_T5_BOARD_SARADC_KEY_VALIDATION
  ret = bk7258_saradc_validation_start(
          &g_bk7258_t5_board_adc_key_validation);
  if (ret < 0)
    {
      _err("ERROR: T5-Board ADC-key validation worker failed: %d\n", ret);
      return ret;
    }
#endif

  (void)ret;
  return OK;
}
