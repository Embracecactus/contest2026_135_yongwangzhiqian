/****************************************************************************
 * board/bk7258/boards/t5_board/src/bk7258_t5_board_camera.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board V1.0.2 binding for the P10 DVP camera connector.
 *
 * The immutable SDK owns sensor discovery and the combined sensor/data
 * stream.  NuttX still owns the public V4L2 ABI: this file supplies the
 * board wiring, a truthful sensor facade, PSRAM backing stores, and the
 * capture_register() call.  No private camera character-device ABI is added.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA

#include <sys/videoio.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/kthread.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/video/imgsensor.h>
#include <nuttx/video/v4l2_cap.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_dvp.h>
#include <arch/chip/bk7258_pm.h>
#include <arch/chip/bk7258_psram.h>

#include <sdkconfig.h>

#define T5_CAMERA_DEVPATH            "/dev/video0"
#define T5_CAMERA_WIDTH              640u
#define T5_CAMERA_HEIGHT             480u
#define T5_CAMERA_FPS                30u
#define T5_CAMERA_FRAME_COUNT        2u
#define T5_CAMERA_DMA_ALIGNMENT      32u
#define T5_CAMERA_RAW_FRAME_SIZE     (T5_CAMERA_WIDTH * T5_CAMERA_HEIGHT * 2u)
#define T5_CAMERA_VALIDATION_TIMEOUT 3000
#define T5_CAMERA_VALIDATION_DELAY_US 1000000u
#define T5_CAMERA_VALIDATION_STACK    4096

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264
#  define T5_CAMERA_V4L2_FORMAT      V4L2_PIX_FMT_H264
/* bk7258_dvp.c keeps H264 public at the V4L2 boundary and maps it to the
 * pinned capture upper-half's internal compressed token. */
#  define T5_CAMERA_SENSOR_FORMAT    IMGSENSOR_PIX_FMT_JPEG_WITH_SUBIMG
#  define T5_CAMERA_SDK_FORMAT       IMAGE_H264
#  define T5_CAMERA_FRAME_SIZE       CONFIG_H264_FRAME_SIZE
#  define T5_CAMERA_ENCODE_BUFFER_SIZE (T5_CAMERA_WIDTH * 32u * 2u)
#  define T5_CAMERA_FORMAT_FLAGS     V4L2_FMT_FLAG_COMPRESSED
#  define T5_CAMERA_FORMAT_NAME      "H264"
#elif defined(CONFIG_BK7258_T5_BOARD_CAMERA_RAW_YUV)
#  define T5_CAMERA_V4L2_FORMAT      V4L2_PIX_FMT_YUYV
#  define T5_CAMERA_SENSOR_FORMAT    IMGSENSOR_PIX_FMT_YUYV
#  define T5_CAMERA_SDK_FORMAT       IMAGE_YUV
#  define T5_CAMERA_FRAME_SIZE       T5_CAMERA_RAW_FRAME_SIZE
#  define T5_CAMERA_ENCODE_BUFFER_SIZE 0u
#  define T5_CAMERA_FORMAT_FLAGS     0
#  define T5_CAMERA_FORMAT_NAME      "YUYV"
#else
#  define T5_CAMERA_V4L2_FORMAT      V4L2_PIX_FMT_JPEG
#  define T5_CAMERA_SENSOR_FORMAT    IMGSENSOR_PIX_FMT_JPEG
#  define T5_CAMERA_SDK_FORMAT       IMAGE_MJPEG
#  define T5_CAMERA_FRAME_SIZE       CONFIG_JPEG_FRAME_SIZE
#  define T5_CAMERA_ENCODE_BUFFER_SIZE (T5_CAMERA_WIDTH * 16u * 2u)
#  define T5_CAMERA_FORMAT_FLAGS     V4L2_FMT_FLAG_COMPRESSED
#  define T5_CAMERA_FORMAT_NAME      "MJPEG"
#endif

static mutex_t g_t5_camera_lock = NXMUTEX_INITIALIZER;
static struct bk7258_dvp_frame_mem_s
  g_t5_camera_frames[T5_CAMERA_FRAME_COUNT];
static FAR void *g_t5_camera_frame_bases[T5_CAMERA_FRAME_COUNT];
static FAR void *g_t5_camera_encode_base;
static FAR struct bk7258_dvp_s *g_t5_camera_dvp;
static bool g_t5_camera_registered;

static bool t5_camera_sensor_is_available(FAR struct imgsensor_s *sensor)
{
  (void)sensor;

  /* The SDK performs the real I2C probe from IMGDATA_INIT/bk_dvp_open().
   * capture_register() asks this question before that combined resource is
   * opened, so availability here means that the physical board route exists.
   * An absent or unsupported module is reported as -ENODEV on open. */

  return BK7258_BOARD_HAS_DVP_CONNECTOR != 0;
}

static int t5_camera_sensor_init(FAR struct imgsensor_s *sensor)
{
  (void)sensor;
  return OK;
}

static int t5_camera_sensor_uninit(FAR struct imgsensor_s *sensor)
{
  (void)sensor;
  return OK;
}

static FAR const char *
t5_camera_sensor_get_driver_name(FAR struct imgsensor_s *sensor)
{
  (void)sensor;
  return "bk7258-sdk-dvp";
}

static int t5_camera_sensor_validate(
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

  if (datafmts[IMGSENSOR_FMT_MAIN].width != T5_CAMERA_WIDTH ||
      datafmts[IMGSENSOR_FMT_MAIN].height != T5_CAMERA_HEIGHT ||
      datafmts[IMGSENSOR_FMT_MAIN].pixelformat != T5_CAMERA_SENSOR_FORMAT)
    {
      return -ENOTSUP;
    }

  if (interval == NULL || interval->numerator == 0 ||
      interval->denominator == 0)
    {
      return -EINVAL;
    }

  return (uint64_t)T5_CAMERA_FPS * interval->numerator ==
         interval->denominator ? OK : -ENOTSUP;
}

static int t5_camera_sensor_start(
  FAR struct imgsensor_s *sensor, imgsensor_stream_type_t type,
  uint8_t nr_datafmts, FAR imgsensor_format_t *datafmts,
  FAR imgsensor_interval_t *interval)
{
  /* The matching imgdata start runs first in v4l2_cap.c and resumes the
   * combined SDK resource.  Revalidate here, but do not start it twice. */

  return t5_camera_sensor_validate(sensor, type, nr_datafmts, datafmts,
                                   interval);
}

static int t5_camera_sensor_stop(FAR struct imgsensor_s *sensor,
                                 imgsensor_stream_type_t type)
{
  (void)sensor;

  /* The matching imgdata stop runs first and suspends the SDK resource. */

  return type == IMGSENSOR_STREAM_TYPE_VIDEO ||
         type == IMGSENSOR_STREAM_TYPE_STILL ? OK : -EINVAL;
}

static int t5_camera_sensor_get_interval(
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
  interval->denominator = T5_CAMERA_FPS;
  return OK;
}

static const struct imgsensor_ops_s g_t5_camera_sensor_ops =
{
  .is_available = t5_camera_sensor_is_available,
  .init = t5_camera_sensor_init,
  .uninit = t5_camera_sensor_uninit,
  .get_driver_name = t5_camera_sensor_get_driver_name,
  .validate_frame_setting = t5_camera_sensor_validate,
  .start_capture = t5_camera_sensor_start,
  .stop_capture = t5_camera_sensor_stop,
  .get_frame_interval = t5_camera_sensor_get_interval,
};

static const struct v4l2_fmtdesc g_t5_camera_formats[] =
{
  {
    .index = 0,
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .flags = T5_CAMERA_FORMAT_FLAGS,
    .description = T5_CAMERA_FORMAT_NAME,
    .pixelformat = T5_CAMERA_V4L2_FORMAT,
  },
};

static const struct v4l2_frmsizeenum g_t5_camera_sizes[] =
{
  {
    .index = 0,
    .buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .pixel_format = T5_CAMERA_V4L2_FORMAT,
    .type = V4L2_FRMSIZE_TYPE_DISCRETE,
    .discrete =
    {
      .width = T5_CAMERA_WIDTH,
      .height = T5_CAMERA_HEIGHT,
    },
  },
};

static const struct v4l2_frmivalenum g_t5_camera_intervals[] =
{
  {
    .index = 0,
    .buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .pixel_format = T5_CAMERA_V4L2_FORMAT,
    .width = T5_CAMERA_WIDTH,
    .height = T5_CAMERA_HEIGHT,
    .type = V4L2_FRMIVAL_TYPE_DISCRETE,
    .discrete =
    {
      .numerator = 1,
      .denominator = T5_CAMERA_FPS,
    },
  },
};

static struct imgsensor_s g_t5_camera_sensor =
{
  .ops = &g_t5_camera_sensor_ops,
  .fmtdescs_num = 1,
  .fmtdescs = g_t5_camera_formats,
  .frmsizes_num = 1,
  .frmsizes = g_t5_camera_sizes,
  .frmintervals_num = 1,
  .frmintervals = g_t5_camera_intervals,
};

static FAR uint8_t *t5_camera_alloc_aligned(size_t size,
                                            FAR void **allocation)
{
  FAR uint8_t *base;
  uintptr_t address;

  if (allocation == NULL ||
      size > SIZE_MAX - (T5_CAMERA_DMA_ALIGNMENT - 1u))
    {
      return NULL;
    }

  base = bk7258_psram_malloc(size + T5_CAMERA_DMA_ALIGNMENT - 1u);
  if (base == NULL)
    {
      return NULL;
    }

  address = ((uintptr_t)base + T5_CAMERA_DMA_ALIGNMENT - 1u) &
            ~((uintptr_t)T5_CAMERA_DMA_ALIGNMENT - 1u);
  *allocation = base;
  return (FAR uint8_t *)address;
}

#ifdef CONFIG_BK7258_PSRAM_MEDIA
static FAR uint8_t *t5_camera_alloc_media_aligned(
  enum bk7258_psram_media_heap_e heap, size_t size,
  FAR void **allocation)
{
  FAR uint8_t *base;
  uintptr_t address;

  if (allocation == NULL ||
      size > SIZE_MAX - (T5_CAMERA_DMA_ALIGNMENT - 1u))
    {
      return NULL;
    }

  base = bk7258_psram_media_malloc(heap,
                                   size + T5_CAMERA_DMA_ALIGNMENT - 1u);
  if (base == NULL)
    {
      return NULL;
    }

  address = ((uintptr_t)base + T5_CAMERA_DMA_ALIGNMENT - 1u) &
            ~((uintptr_t)T5_CAMERA_DMA_ALIGNMENT - 1u);
  *allocation = base;
  return (FAR uint8_t *)address;
}
#endif

static void t5_camera_release_memory(void)
{
  uint8_t index;

  for (index = 0; index < T5_CAMERA_FRAME_COUNT; index++)
    {
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_RAW_YUV
      bk7258_psram_media_free(g_t5_camera_frame_bases[index]);
#else
      bk7258_psram_free(g_t5_camera_frame_bases[index]);
#endif
      g_t5_camera_frame_bases[index] = NULL;
      g_t5_camera_frames[index].addr = NULL;
      g_t5_camera_frames[index].size = 0;
    }

  kmm_free(g_t5_camera_encode_base);
  g_t5_camera_encode_base = NULL;
}

#if defined(CONFIG_BK7258_T5_BOARD_CAMERA_VALIDATION) || \
    defined(CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION)
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION
static uint32_t t5_camera_checksum(FAR const uint8_t *data, size_t length)
{
  uint32_t checksum = 2166136261u;
  size_t index;

  for (index = 0; index < length; index++)
    {
      checksum ^= data[index];
      checksum *= 16777619u;
    }

  return checksum;
}

static bool t5_camera_h264_has_annexb(FAR const uint8_t *data,
                                      size_t length)
{
  size_t index;

  for (index = 0; index + 3u < length; index++)
    {
      if (data[index] == 0 && data[index + 1u] == 0 &&
          ((data[index + 2u] == 1) ||
           (data[index + 2u] == 0 && data[index + 3u] == 1)))
        {
          return true;
        }
    }

  return false;
}

#endif

static int t5_camera_validate_frame(void)
{
  struct v4l2_requestbuffers req;
  struct v4l2_buffer buffer;
  struct v4l2_format format;
  struct pollfd pollfd;
  FAR uint8_t *frame = NULL;
  FAR void *frame_base = NULL;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  bool streaming = false;
  FAR const char *stage = "open";
  uint32_t bytesused = 0;
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION
  uint32_t checksum = 0;
  struct bk7258_pm_frequency_status_s dvfs_before;
  struct bk7258_pm_frequency_status_s dvfs_active;
  struct bk7258_pm_frequency_status_s dvfs_after;
#endif
  int fd = -1;
  int ret = 0;

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION
  /* The v3.1.1.9 DVP lower half acquires its JPEG/video frequency vote
   * while the device is opened and releases it when the device is closed.
   * Sample the idle policy before open(), not after buffer setup. */

  stage = "dvfs-before";
  ret = bk7258_pm_frequency_get_status(&dvfs_before);
  if (ret < 0 || dvfs_before.current != BK7258_PM_CPU_FREQ_120M)
    {
      if (ret >= 0)
        {
          ret = -ERANGE;
        }

      goto out;
    }
#endif

  stage = "open";
  fd = open(T5_CAMERA_DEVPATH, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      ret = -errno;
      goto out;
    }

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_RAW_YUV
  frame = t5_camera_alloc_media_aligned(BK7258_PSRAM_MEDIA_YUV,
                                        T5_CAMERA_FRAME_SIZE, &frame_base);
#else
  frame = t5_camera_alloc_aligned(T5_CAMERA_FRAME_SIZE, &frame_base);
#endif
  if (frame == NULL)
    {
      stage = "alloc";
      ret = -ENOMEM;
      goto out;
    }

  memset(&format, 0, sizeof(format));
  format.type = type;
  format.fmt.pix.width = T5_CAMERA_WIDTH;
  format.fmt.pix.height = T5_CAMERA_HEIGHT;
  format.fmt.pix.field = V4L2_FIELD_ANY;
  format.fmt.pix.pixelformat = T5_CAMERA_V4L2_FORMAT;
  stage = "s-fmt";
  if (ioctl(fd, VIDIOC_S_FMT, (uintptr_t)&format) < 0)
    {
      ret = -errno;
      goto out;
    }

  memset(&req, 0, sizeof(req));
  req.type = type;
  req.memory = V4L2_MEMORY_USERPTR;
  req.count = 1;
  req.mode = V4L2_BUF_MODE_FIFO;
  stage = "reqbufs";
  if (ioctl(fd, VIDIOC_REQBUFS, (uintptr_t)&req) < 0)
    {
      ret = -errno;
      goto out;
    }

  memset(&buffer, 0, sizeof(buffer));
  buffer.type = type;
  buffer.memory = V4L2_MEMORY_USERPTR;
  buffer.index = 0;
  buffer.m.userptr = (uintptr_t)frame;
  buffer.length = T5_CAMERA_FRAME_SIZE;
  stage = "qbuf";
  if (ioctl(fd, VIDIOC_QBUF, (uintptr_t)&buffer) < 0)
    {
      ret = -errno;
      goto out;
    }

  stage = "streamon";
  if (ioctl(fd, VIDIOC_STREAMON, (uintptr_t)&type) < 0)
    {
      ret = -errno;
      goto out;
    }

  streaming = true;
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION
  stage = "dvfs-active";
  ret = bk7258_pm_frequency_get_status(&dvfs_active);
  if (ret < 0 || dvfs_active.current != BK7258_PM_CPU_FREQ_480M)
    {
      if (ret >= 0)
        {
          ret = -ERANGE;
        }

      goto out;
    }

  syslog(LOG_INFO, "BKCAMH264 STREAMON\n");
#endif
  memset(&pollfd, 0, sizeof(pollfd));
  pollfd.fd = fd;
  pollfd.events = POLLIN;
  stage = "poll";
  ret = poll(&pollfd, 1, T5_CAMERA_VALIDATION_TIMEOUT);
  if (ret == 0)
    {
      ret = -ETIMEDOUT;
      goto out;
    }
  else if (ret < 0)
    {
      ret = -errno;
      goto out;
    }

  memset(&buffer, 0, sizeof(buffer));
  buffer.type = type;
  buffer.memory = V4L2_MEMORY_USERPTR;
  stage = "dqbuf";
  if (ioctl(fd, VIDIOC_DQBUF, (uintptr_t)&buffer) < 0)
    {
      ret = -errno;
      goto out;
    }

  bytesused = buffer.bytesused;
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION
  stage = "h264";
  if (bytesused < 5 || bytesused > T5_CAMERA_FRAME_SIZE ||
      !t5_camera_h264_has_annexb(frame, bytesused))
    {
      ret = -EBADMSG;
      goto out;
    }

  checksum = t5_camera_checksum(frame, bytesused);
#elif defined(CONFIG_BK7258_T5_BOARD_CAMERA_RAW_YUV)
  stage = "yuyv";
  if (bytesused != T5_CAMERA_RAW_FRAME_SIZE)
    {
      ret = -EBADMSG;
      goto out;
    }
#else
  stage = "jpeg";
  if (bytesused < 4 || bytesused > T5_CAMERA_FRAME_SIZE ||
      frame[0] != 0xff || frame[1] != 0xd8 ||
      frame[bytesused - 2] != 0xff || frame[bytesused - 1] != 0xd9)
    {
      ret = -EBADMSG;
      goto out;
    }
#endif

  ret = 0;

out:
  if (streaming && ioctl(fd, VIDIOC_STREAMOFF, (uintptr_t)&type) < 0)
    {
      if (ret == 0)
        {
          stage = "streamoff";
          ret = -errno;
        }
    }
  else if (streaming)
    {
      streaming = false;
    }

  if (fd >= 0)
    {
      if (close(fd) < 0 && ret == 0)
        {
          stage = "close";
          ret = -errno;
        }

      fd = -1;
    }

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION
  /* STREAMOFF stops frame flow, while close() synchronously tears down the
   * SDK DVP/YUV instance and returns its frequency vote to DEFAULT. */

  if (ret == 0)
    {
      stage = "dvfs-after";
      ret = bk7258_pm_frequency_get_status(&dvfs_after);
      if (ret >= 0 &&
          (dvfs_after.current != BK7258_PM_CPU_FREQ_120M ||
           dvfs_after.peak < BK7258_PM_CPU_FREQ_480M ||
           dvfs_active.transitions <= dvfs_before.transitions ||
           dvfs_after.transitions < dvfs_before.transitions + 2u))
        {
          ret = -ERANGE;
        }
    }
#endif

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_RAW_YUV
  bk7258_psram_media_free(frame_base);
#else
  bk7258_psram_free(frame_base);
#endif
  if (ret == 0)
    {
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION
      syslog(LOG_INFO, "BKCAMH264 PASS bytes=%" PRIu32
             " checksum=%08" PRIx32 " size=%ux%u dvfs=%" PRIu32
             "->%" PRIu32 "->%" PRIu32 " transitions=%" PRIu32 "\n",
             bytesused, checksum,
             T5_CAMERA_WIDTH, T5_CAMERA_HEIGHT,
             dvfs_before.current, dvfs_active.current, dvfs_after.current,
             dvfs_after.transitions - dvfs_before.transitions);
#else
      syslog(LOG_INFO, "BKCAM PASS bytes=%" PRIu32
             " format=MJPEG size=%ux%u\n", bytesused,
             T5_CAMERA_WIDTH, T5_CAMERA_HEIGHT);
#endif
    }
  else
    {
      syslog(LOG_ERR, "BKCAM%s FAIL stage=%s ret=%d\n",
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION
             "H264",
#else
             "",
#endif
             stage, ret);
    }

  return ret;
}

static int t5_camera_validation_thread(int argc, FAR char *argv[])
{
  (void)argc;
  (void)argv;

  /* Peripheral registration runs before the AP publishes READY.  Do not
   * hold that startup gate while a physical sensor probe/frame capture may
   * take seconds.  The one-shot validation profile waits until RPTUN/syslog
   * have had a chance to connect, then exercises the normal V4L2 ABI.
   */

  usleep(T5_CAMERA_VALIDATION_DELAY_US);
  (void)t5_camera_validate_frame();
  return 0;
}
#endif

int bk7258_t5_board_camera_initialize(void)
{
  struct bk7258_dvp_config_s config;
  FAR struct imgsensor_s *sensors[] = {&g_t5_camera_sensor};
  FAR uint8_t *encode_buffer = NULL;
  bk_dvp_config_t sdk = BK_DVP_864X480_30FPS_MJPEG_CONFIG();
  uint8_t index;
  int ret;

  ret = nxmutex_lock(&g_t5_camera_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_t5_camera_registered)
    {
      nxmutex_unlock(&g_t5_camera_lock);
      return OK;
    }

  if (!bk7258_psram_ready())
    {
      nxmutex_unlock(&g_t5_camera_lock);
      return -ENODEV;
    }

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_RAW_YUV
  ret = bk7258_psram_media_initialize();
  if (ret < 0)
    {
      nxmutex_unlock(&g_t5_camera_lock);
      return ret;
    }
#endif

  memset(&config, 0, sizeof(config));
  for (index = 0; index < T5_CAMERA_FRAME_COUNT; index++)
    {
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_RAW_YUV
      g_t5_camera_frames[index].addr = t5_camera_alloc_media_aligned(
        BK7258_PSRAM_MEDIA_YUV, T5_CAMERA_FRAME_SIZE,
        &g_t5_camera_frame_bases[index]);
#else
      g_t5_camera_frames[index].addr =
        t5_camera_alloc_aligned(T5_CAMERA_FRAME_SIZE,
                                &g_t5_camera_frame_bases[index]);
#endif
      if (g_t5_camera_frames[index].addr == NULL)
        {
          ret = -ENOMEM;
          goto errout_with_memory;
        }

      g_t5_camera_frames[index].size = T5_CAMERA_FRAME_SIZE;
    }

#ifndef CONFIG_BK7258_T5_BOARD_CAMERA_RAW_YUV
  /* This is the YUV/H264 hardware's line cache, not a completed-frame
   * buffer.  Match v3.1.1.9 bk_camera_dvp_ctlr_new(), which allocates it
   * from os_malloc() in AP SRAM.  Completed encoded frames remain in PSRAM
   * and are drained there by DMA1. */

  encode_buffer = kmm_memalign(T5_CAMERA_DMA_ALIGNMENT,
                               T5_CAMERA_ENCODE_BUFFER_SIZE);
  if (encode_buffer == NULL)
    {
      ret = -ENOMEM;
      goto errout_with_memory;
    }

  g_t5_camera_encode_base = encode_buffer;
#endif

  sdk.i2c_config.id = BK7258_BOARD_DVP_I2C_BUS;
  sdk.i2c_config.scl_pin = BK7258_BOARD_DVP_I2C_SCL_GPIO;
  sdk.i2c_config.sda_pin = BK7258_BOARD_DVP_I2C_SDA_GPIO;
  sdk.i2c_config.baud_rate = 100000;
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
  sdk.width = T5_CAMERA_WIDTH;
  sdk.height = T5_CAMERA_HEIGHT;
  sdk.fps = FPS30;
  sdk.img_format = T5_CAMERA_SDK_FORMAT;

  config.sdk = sdk;
  config.frames = g_t5_camera_frames;
  config.frame_count = T5_CAMERA_FRAME_COUNT;
  config.encode_buffer = encode_buffer;
  config.encode_buffer_size = encode_buffer == NULL ? 0 :
                              T5_CAMERA_ENCODE_BUFFER_SIZE;

  ret = bk7258_dvp_initialize(&config, &g_t5_camera_dvp);
  if (ret < 0)
    {
      goto errout_with_memory;
    }

  ret = capture_register(T5_CAMERA_DEVPATH,
                         bk7258_dvp_get_imgdata(g_t5_camera_dvp),
                         sensors, 1);
  if (ret < 0)
    {
      (void)bk7258_dvp_uninitialize(g_t5_camera_dvp);
      g_t5_camera_dvp = NULL;
      goto errout_with_memory;
    }

  g_t5_camera_registered = true;
  nxmutex_unlock(&g_t5_camera_lock);

#if defined(CONFIG_BK7258_T5_BOARD_CAMERA_VALIDATION) || \
    defined(CONFIG_BK7258_T5_BOARD_CAMERA_H264_VALIDATION)
  ret = kthread_create("bkcam-validate", SCHED_PRIORITY_DEFAULT,
                       T5_CAMERA_VALIDATION_STACK,
                       t5_camera_validation_thread, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BKCAM FAIL stage=worker ret=%d\n", ret);
    }
#endif

  return OK;

errout_with_memory:
  t5_camera_release_memory();
  nxmutex_unlock(&g_t5_camera_lock);
  return ret;
}

#endif /* CONFIG_BK7258_T5_BOARD_CAMERA */
