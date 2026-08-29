/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_camera.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK AI Toy GC2145 binding for the reusable BK7258 DVP lower half.
 * The immutable SDK owns sensor discovery and the DVP/JPEG stream while
 * NuttX owns the public V4L2 ABI exposed at /dev/video0.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_CAMERA

#include <sys/videoio.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>
#include <nuttx/video/imgsensor.h>
#include <nuttx/video/v4l2_cap.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_dvp.h>
#include <arch/chip/bk7258_psram.h>

#include <driver/gpio.h>
#include <sdkconfig.h>

#include "bk7258_aidk_camera_glue.h"

#define AIDK_CAMERA_ENCODE_BUFFER_SIZE \
  (BK7258_BOARD_CAMERA_WIDTH * 16u * 2u)
#define AIDK_CAMERA_FRAME_SIZE          CONFIG_JPEG_FRAME_SIZE
#define AIDK_CAMERA_POWER_SETTLE_US     10000u

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);

#if defined(CONFIG_BK7258_AIDK_CAMERA_PHASE0)
#  error "AIDK camera Phase 0 and production bindings are mutually exclusive"
#endif

#if BK7258_BOARD_DVP_POWER_GPIO != 49 || \
    BK7258_BOARD_DVP_POWER_ACTIVE_HIGH != 1 || \
    BK7258_BOARD_DVP_RESET_GPIO != 28 || \
    BK7258_BOARD_DVP_RESET_ACTIVE_LOW != 1
#  error "AIDK GC2145 power/reset binding no longer matches the schematic"
#endif

#if BK7258_BOARD_CAMERA_WIDTH != 640u || \
    BK7258_BOARD_CAMERA_HEIGHT != 480u || \
    BK7258_BOARD_CAMERA_FPS != 30u || \
    BK7258_BOARD_CAMERA_FRAME_COUNT != 2u
#  error "AIDK first camera profile must remain VGA/30 with two SDK frames"
#endif

static mutex_t g_aidk_camera_lock = NXMUTEX_INITIALIZER;
static struct bk7258_dvp_frame_mem_s
  g_aidk_camera_frames[BK7258_BOARD_CAMERA_FRAME_COUNT];
static FAR void *g_aidk_camera_frame_bases[
  BK7258_BOARD_CAMERA_FRAME_COUNT];
static FAR void *g_aidk_camera_encode_base;
static FAR struct bk7258_dvp_s *g_aidk_camera_dvp;
static bool g_aidk_camera_powered;
static bool g_aidk_camera_registered;

static int aidk_camera_power_on(void)
{
  gpio_id_t power = (gpio_id_t)BK7258_BOARD_DVP_POWER_GPIO;

  if (g_aidk_camera_powered)
    {
      return OK;
    }

  if (bk_gpio_driver_init() != BK_OK ||
      gpio_dev_unmap(power) != BK_OK ||
      bk_gpio_disable_input(power) != BK_OK ||
      bk_gpio_set_output_low(power) != BK_OK ||
      bk_gpio_enable_output(power) != BK_OK ||
      bk_gpio_set_output_high(power) != BK_OK)
    {
      return -EIO;
    }

  (void)nxsig_usleep(AIDK_CAMERA_POWER_SETTLE_US);
  g_aidk_camera_powered = true;
  return OK;
}

static bool aidk_camera_sensor_is_available(FAR struct imgsensor_s *sensor)
{
  (void)sensor;

  /* The immutable SDK performs the physical GC2145 probe when imgdata is
   * opened.  At registration time this reports only the assembled route.
   */

  return BK7258_BOARD_HAS_CAMERA != 0;
}

static int aidk_camera_sensor_init(FAR struct imgsensor_s *sensor)
{
  (void)sensor;

  /* v4l2_cap opens the sensor facade before the imgdata lower half, so this
   * is the earliest board-owned point that can enable both camera rails.
   */

  return aidk_camera_power_on();
}

static int aidk_camera_sensor_uninit(FAR struct imgsensor_s *sensor)
{
  (void)sensor;

  /* The pinned V4L2 upper half calls sensor uninit before imgdata uninit.
   * Keep P49 asserted so bk_dvp_close() can finish while the GC2145 is still
   * powered.  The rail remains on after first use until the next reset.
   */

  return OK;
}

static FAR const char *
aidk_camera_sensor_get_driver_name(FAR struct imgsensor_s *sensor)
{
  (void)sensor;
  return "bk7258-gc2145";
}

static int aidk_camera_sensor_validate(
  FAR struct imgsensor_s *sensor, imgsensor_stream_type_t type,
  uint8_t nr_datafmts, FAR imgsensor_format_t *datafmts,
  FAR imgsensor_interval_t *interval)
{
  (void)sensor;

  if ((type != IMGSENSOR_STREAM_TYPE_VIDEO &&
       type != IMGSENSOR_STREAM_TYPE_STILL) ||
      nr_datafmts != 1 || datafmts == NULL)
    {
      return -EINVAL;
    }

  if (datafmts[IMGSENSOR_FMT_MAIN].width !=
        BK7258_BOARD_CAMERA_WIDTH ||
      datafmts[IMGSENSOR_FMT_MAIN].height !=
        BK7258_BOARD_CAMERA_HEIGHT ||
      datafmts[IMGSENSOR_FMT_MAIN].pixelformat != IMGSENSOR_PIX_FMT_JPEG)
    {
      return -ENOTSUP;
    }

  if (interval == NULL || interval->numerator == 0 ||
      interval->denominator == 0)
    {
      return -EINVAL;
    }

  return (uint64_t)BK7258_BOARD_CAMERA_FPS * interval->numerator ==
         interval->denominator ? OK : -ENOTSUP;
}

static int aidk_camera_sensor_start(
  FAR struct imgsensor_s *sensor, imgsensor_stream_type_t type,
  uint8_t nr_datafmts, FAR imgsensor_format_t *datafmts,
  FAR imgsensor_interval_t *interval)
{
  /* The imgdata start operation owns the combined SDK stream. */

  return aidk_camera_sensor_validate(sensor, type, nr_datafmts, datafmts,
                                     interval);
}

static int aidk_camera_sensor_stop(FAR struct imgsensor_s *sensor,
                                   imgsensor_stream_type_t type)
{
  (void)sensor;

  return type == IMGSENSOR_STREAM_TYPE_VIDEO ||
         type == IMGSENSOR_STREAM_TYPE_STILL ? OK : -EINVAL;
}

static int aidk_camera_sensor_get_interval(
  FAR struct imgsensor_s *sensor, imgsensor_stream_type_t type,
  FAR imgsensor_interval_t *interval)
{
  (void)sensor;

  if (interval == NULL ||
      (type != IMGSENSOR_STREAM_TYPE_VIDEO &&
       type != IMGSENSOR_STREAM_TYPE_STILL))
    {
      return -EINVAL;
    }

  interval->numerator = 1;
  interval->denominator = BK7258_BOARD_CAMERA_FPS;
  return OK;
}

static const struct imgsensor_ops_s g_aidk_camera_sensor_ops =
{
  .is_available = aidk_camera_sensor_is_available,
  .init = aidk_camera_sensor_init,
  .uninit = aidk_camera_sensor_uninit,
  .get_driver_name = aidk_camera_sensor_get_driver_name,
  .validate_frame_setting = aidk_camera_sensor_validate,
  .start_capture = aidk_camera_sensor_start,
  .stop_capture = aidk_camera_sensor_stop,
  .get_frame_interval = aidk_camera_sensor_get_interval,
};

static const struct v4l2_fmtdesc g_aidk_camera_formats[] =
{
  {
    .index = 0,
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .flags = V4L2_FMT_FLAG_COMPRESSED,
    .description = "MJPEG",
    .pixelformat = V4L2_PIX_FMT_JPEG,
  },
};

static const struct v4l2_frmsizeenum g_aidk_camera_sizes[] =
{
  {
    .index = 0,
    .buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .pixel_format = V4L2_PIX_FMT_JPEG,
    .type = V4L2_FRMSIZE_TYPE_DISCRETE,
    .discrete =
    {
      .width = BK7258_BOARD_CAMERA_WIDTH,
      .height = BK7258_BOARD_CAMERA_HEIGHT,
    },
  },
};

static const struct v4l2_frmivalenum g_aidk_camera_intervals[] =
{
  {
    .index = 0,
    .buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .pixel_format = V4L2_PIX_FMT_JPEG,
    .width = BK7258_BOARD_CAMERA_WIDTH,
    .height = BK7258_BOARD_CAMERA_HEIGHT,
    .type = V4L2_FRMIVAL_TYPE_DISCRETE,
    .discrete =
    {
      .numerator = 1,
      .denominator = BK7258_BOARD_CAMERA_FPS,
    },
  },
};

static struct imgsensor_s g_aidk_camera_sensor =
{
  .ops = &g_aidk_camera_sensor_ops,
  .fmtdescs_num = 1,
  .fmtdescs = g_aidk_camera_formats,
  .frmsizes_num = 1,
  .frmsizes = g_aidk_camera_sizes,
  .frmintervals_num = 1,
  .frmintervals = g_aidk_camera_intervals,
};

static FAR uint8_t *aidk_camera_alloc_aligned(size_t size,
                                               FAR void **allocation)
{
  FAR uint8_t *base;
  uintptr_t address;

  if (allocation == NULL ||
      size > SIZE_MAX - (BK7258_BOARD_CAMERA_DMA_ALIGNMENT - 1u))
    {
      return NULL;
    }

  base = bk7258_psram_malloc(
    size + BK7258_BOARD_CAMERA_DMA_ALIGNMENT - 1u);
  if (base == NULL)
    {
      return NULL;
    }

  address = ((uintptr_t)base + BK7258_BOARD_CAMERA_DMA_ALIGNMENT - 1u) &
            ~((uintptr_t)BK7258_BOARD_CAMERA_DMA_ALIGNMENT - 1u);
  *allocation = base;
  return (FAR uint8_t *)address;
}

static void aidk_camera_release_memory(void)
{
  uint8_t index;

  for (index = 0; index < BK7258_BOARD_CAMERA_FRAME_COUNT; index++)
    {
      if (g_aidk_camera_frame_bases[index] != NULL)
        {
          bk7258_psram_free(g_aidk_camera_frame_bases[index]);
        }

      g_aidk_camera_frame_bases[index] = NULL;
      g_aidk_camera_frames[index].addr = NULL;
      g_aidk_camera_frames[index].size = 0;
    }

  kmm_free(g_aidk_camera_encode_base);
  g_aidk_camera_encode_base = NULL;
}

int bk7258_aidk_camera_initialize(void)
{
  struct bk7258_dvp_config_s config;
  FAR struct imgsensor_s *sensors[] =
  {
    &g_aidk_camera_sensor
  };
  FAR uint8_t *encode_buffer;
  bk_dvp_config_t sdk = BK_DVP_864X480_30FPS_MJPEG_CONFIG();
  uint8_t index;
  int ret;

  ret = nxmutex_lock(&g_aidk_camera_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_aidk_camera_registered)
    {
      nxmutex_unlock(&g_aidk_camera_lock);
      return OK;
    }

  if (!bk7258_psram_ready())
    {
      nxmutex_unlock(&g_aidk_camera_lock);
      return -ENODEV;
    }

  memset(&config, 0, sizeof(config));
  for (index = 0; index < BK7258_BOARD_CAMERA_FRAME_COUNT; index++)
    {
      g_aidk_camera_frames[index].addr =
        aidk_camera_alloc_aligned(AIDK_CAMERA_FRAME_SIZE,
                                  &g_aidk_camera_frame_bases[index]);
      if (g_aidk_camera_frames[index].addr == NULL)
        {
          ret = -ENOMEM;
          goto errout_with_memory;
        }

      g_aidk_camera_frames[index].size = AIDK_CAMERA_FRAME_SIZE;
    }

  /* The SDK uses this AP-SRAM buffer as a two-bank, 16-line JPEG cache.
   * Completed compressed frames remain in the caller-owned PSRAM stores.
   */

  encode_buffer = kmm_memalign(BK7258_BOARD_CAMERA_DMA_ALIGNMENT,
                               AIDK_CAMERA_ENCODE_BUFFER_SIZE);
  if (encode_buffer == NULL)
    {
      ret = -ENOMEM;
      goto errout_with_memory;
    }

  g_aidk_camera_encode_base = encode_buffer;

  sdk.i2c_config.id = BK7258_BOARD_DVP_I2C_BUS;
  sdk.i2c_config.scl_pin = BK7258_BOARD_DVP_I2C_SCL_GPIO;
  sdk.i2c_config.sda_pin = BK7258_BOARD_DVP_I2C_SDA_GPIO;
  sdk.i2c_config.baud_rate = BK7258_BOARD_DVP_I2C_FREQUENCY;
  sdk.reset_pin = BK7258_BOARD_DVP_RESET_GPIO;
  sdk.pwdn_pin = BK7258_BOARD_DVP_PWDN_GPIO;
  sdk.io_config.data_width = SENSOR_BITS_WIDTH_8BIT;
  sdk.io_config.data_pin[0] = BK7258_BOARD_DVP_D0_GPIO;
  sdk.io_config.data_pin[1] = BK7258_BOARD_DVP_D1_GPIO;
  sdk.io_config.data_pin[2] = BK7258_BOARD_DVP_D2_GPIO;
  sdk.io_config.data_pin[3] = BK7258_BOARD_DVP_D3_GPIO;
  sdk.io_config.data_pin[4] = BK7258_BOARD_DVP_D4_GPIO;
  sdk.io_config.data_pin[5] = BK7258_BOARD_DVP_D5_GPIO;
  sdk.io_config.data_pin[6] = BK7258_BOARD_DVP_D6_GPIO;
  sdk.io_config.data_pin[7] = BK7258_BOARD_DVP_D7_GPIO;
  sdk.io_config.vsync_pin = BK7258_BOARD_DVP_VSYNC_GPIO;
  sdk.io_config.hsync_pin = BK7258_BOARD_DVP_HSYNC_GPIO;
  sdk.io_config.xclk_pin = BK7258_BOARD_DVP_MCLK_GPIO;
  sdk.io_config.pclk_pin = BK7258_BOARD_DVP_PCLK_GPIO;
  sdk.clk_source = MCLK_24M;
  sdk.width = BK7258_BOARD_CAMERA_WIDTH;
  sdk.height = BK7258_BOARD_CAMERA_HEIGHT;
  sdk.fps = FPS30;
  sdk.img_format = IMAGE_MJPEG;

  config.sdk = sdk;
  config.binding = bk7258_aidk_camera_binding();
  config.frames = g_aidk_camera_frames;
  config.frame_count = BK7258_BOARD_CAMERA_FRAME_COUNT;
  config.encode_buffer = encode_buffer;
  config.encode_buffer_size = AIDK_CAMERA_ENCODE_BUFFER_SIZE;

  ret = bk7258_dvp_initialize(&config, &g_aidk_camera_dvp);
  if (ret < 0)
    {
      goto errout_with_memory;
    }

  ret = capture_register(BK7258_BOARD_CAMERA_DEVPATH,
                         bk7258_dvp_get_imgdata(g_aidk_camera_dvp),
                         sensors, 1);
  if (ret < 0)
    {
      (void)bk7258_dvp_uninitialize(g_aidk_camera_dvp);
      g_aidk_camera_dvp = NULL;
      goto errout_with_memory;
    }

  g_aidk_camera_registered = true;
  nxmutex_unlock(&g_aidk_camera_lock);
  syslog(LOG_INFO, "AIDK GC2145 registered: %s %ux%u@%u MJPEG\n",
         BK7258_BOARD_CAMERA_DEVPATH, BK7258_BOARD_CAMERA_WIDTH,
         BK7258_BOARD_CAMERA_HEIGHT, BK7258_BOARD_CAMERA_FPS);
  return OK;

errout_with_memory:
  aidk_camera_release_memory();
  nxmutex_unlock(&g_aidk_camera_lock);
  return ret;
}

#endif /* CONFIG_BK7258_AIDK_CAMERA */
