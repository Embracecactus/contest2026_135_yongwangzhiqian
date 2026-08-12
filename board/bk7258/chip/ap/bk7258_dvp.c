/****************************************************************************
 * board/bk7258/chip/ap/bk7258_dvp.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP DVP image-data lower-half.  The immutable v3.1.1.9 public DVP
 * header describes a vendor-owned camera stream.  This file adapts its
 * frame callback to NuttX imgdata_s without adding a character-device ABI.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>
#include <nuttx/video/imgdata.h>
#include <nuttx/video/video.h>
#include <nuttx/wqueue.h>

#include <components/dvp_camera.h>
#include <driver/dma.h>
#include <driver/h264.h>
#include <driver/jpeg_enc.h>
#include <driver/yuv_buf.h>
#include <sdkconfig.h>

#include <arch/chip/bk7258_dvp.h>
#include <arch/chip/bk7258_pm.h>
#ifdef CONFIG_BK7258_PSRAM
#  include <arch/chip/bk7258_psram.h>
#endif

#define BK7258_DVP_MAX_FRAMES 8
#define BK7258_DVP_EVENT_DEPTH 8
#define BK7258_DVP_RESULT_ERROR 1
#define BK7258_DVP_ALLOC_MAGIC 0x44565041u

#ifdef CONFIG_BK7258_PSRAM
struct bk7258_dvp_allocation_s
{
  FAR void *base;
  uint32_t magic;
};
#endif

struct bk7258_dvp_event_s
{
  FAR struct frame_buffer_t *frame;
  uint8_t result;
};

struct bk7258_dvp_s
{
  struct imgdata_s data;
  struct bk7258_dvp_config_s config;
  bk_dvp_config_t sdk_config;
  camera_handle_t handle;

  spinlock_t lock;
  mutex_t api_lock;
  struct work_s complete_work;
  struct bk7258_dvp_event_s events[BK7258_DVP_EVENT_DEPTH];
  struct frame_buffer_t frames[BK7258_DVP_MAX_FRAMES];
  bool frame_busy[BK7258_DVP_MAX_FRAMES];
  uint8_t event_head;
  uint8_t event_count;
  bool work_queued;
  bool worker_running;
  pid_t worker_tid;
  bool stopping;
  bool configured;
  bool pm_clock_held;
  bool pm_mclk_held;
  bool sdk_open;
  bool suspended;
  bool capture_active;
  FAR uint8_t *next_buffer;
  uint32_t next_size;
  imgdata_capture_t capture_cb;
  FAR void *capture_arg;
};

static struct bk7258_dvp_s g_bk7258_dvp =
{
  .api_lock = NXMUTEX_INITIALIZER,
};

static FAR struct frame_buffer_t *bk7258_dvp_frame_malloc(
  image_format_t format, uint32_t size);
static void bk7258_dvp_frame_complete(image_format_t format,
                                      FAR struct frame_buffer_t *frame,
                                      int result);
static int bk7258_dvp_data_uninit(FAR struct imgdata_s *data);

static const bk_dvp_callback_t g_bk7258_dvp_callback =
{
  .malloc = bk7258_dvp_frame_malloc,
  .complete = bk7258_dvp_frame_complete,
};

static int bk7258_dvp_error(bk_err_t error)
{
  switch (error)
    {
      case BK_OK:
        return 0;

      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
        return -EINVAL;

      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case BK_ERR_TIMEOUT:
        return -ETIMEDOUT;

      case BK_ERR_BUSY:
      case BK_ERR_IN_PROGRESS:
        return -EBUSY;

      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      case BK_ERR_NO_DEV:
      case BK_ERR_NOT_FOUND:
        return -ENODEV;

      case BK_ERR_SHUT_DOWN:
        return -ESHUTDOWN;

      default:
        /* BK_FAIL and module-specific vendor errors are not errno values. */
        return -EIO;
    }
}

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264
extern bk_err_t __real_bk_h264_encode_enable(void);
extern bk_err_t __real_bk_yuv_buf_start(yuv_mode_t work_mode);
extern int __real_video_register(FAR const char *devpath,
                                 FAR struct v4l2_s *ctx);
extern int32_t __real_sys_drv_int_group2_enable(uint32_t mask);
extern int32_t __real_sys_drv_core_intr_group2_enable(uint32_t core_id,
                                                      uint32_t mask);
extern int __real_dvp_camera_i2c_write_uint8(uint8_t addr, uint8_t reg,
                                             uint8_t value);

static bool g_bk7258_dvp_h264_opening;
static bool g_bk7258_dvp_h264_yuv_deferred;
static bool g_bk7258_dvp_h264_encode_deferred;
static bool g_bk7258_dvp_yuv_irq_deferred;
static bool g_bk7258_dvp_h264_irq_deferred;
static bool g_bk7258_dvp_sensor_output_deferred;
static uint32_t g_bk7258_dvp_h264_irq_core;
static FAR const struct v4l2_ops_s *g_bk7258_dvp_capture_vops;
static struct v4l2_ops_s g_bk7258_dvp_h264_vops;

#define BK7258_H264_GROUP2_MASK (1u << (46u - 32u))
#define BK7258_YUVB_GROUP2_MASK (1u << (58u - 32u))

/* The pinned NuttX capture upper-half publishes V4L2_PIX_FMT_H264 in its
 * public headers, but capture_try_fmt() does not yet accept it.  Keep the
 * user-visible ABI standard and translate only at the v4l2 capture boundary
 * to the existing compressed imgdata token consumed by this lower-half.
 */

static bool bk7258_dvp_h264_format_alias(FAR struct v4l2_format *format)
{
  if (format != NULL && format->fmt.pix.pixelformat == V4L2_PIX_FMT_H264)
    {
      format->fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG_WITH_SUBIMG;
      return true;
    }

  return false;
}

static int bk7258_dvp_h264_g_fmt(FAR struct file *filep,
                                 FAR struct v4l2_format *format)
{
  int ret = g_bk7258_dvp_capture_vops->g_fmt(filep, format);

  if (ret == OK && format->fmt.pix.pixelformat ==
                   V4L2_PIX_FMT_JPEG_WITH_SUBIMG)
    {
      format->fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    }

  return ret;
}

static int bk7258_dvp_h264_s_fmt(FAR struct file *filep,
                                 FAR struct v4l2_format *format)
{
  bool aliased = bk7258_dvp_h264_format_alias(format);
  int ret = g_bk7258_dvp_capture_vops->s_fmt(filep, format);

  if (aliased)
    {
      format->fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    }

  return ret;
}

static int bk7258_dvp_h264_try_fmt(FAR struct file *filep,
                                   FAR struct v4l2_format *format)
{
  bool aliased = bk7258_dvp_h264_format_alias(format);
  int ret = g_bk7258_dvp_capture_vops->try_fmt(filep, format);

  if (aliased)
    {
      format->fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    }

  return ret;
}

static int bk7258_dvp_h264_enum_fmt(FAR struct file *filep,
                                    FAR struct v4l2_fmtdesc *format)
{
  int ret = g_bk7258_dvp_capture_vops->enum_fmt(filep, format);

  if (ret == OK && format->pixelformat == V4L2_PIX_FMT_JPEG_WITH_SUBIMG)
    {
      format->pixelformat = V4L2_PIX_FMT_H264;
      strncpy((FAR char *)format->description, "H264",
              sizeof(format->description));
      format->description[sizeof(format->description) - 1u] = '\0';
    }

  return ret;
}

static int bk7258_dvp_h264_enum_frminterval(
  FAR struct file *filep, FAR struct v4l2_frmivalenum *interval)
{
  bool aliased = interval != NULL &&
                 interval->pixel_format == V4L2_PIX_FMT_H264;
  int ret;

  if (aliased)
    {
      interval->pixel_format = V4L2_PIX_FMT_JPEG_WITH_SUBIMG;
    }

  ret = g_bk7258_dvp_capture_vops->enum_frminterval(filep, interval);
  if (aliased)
    {
      interval->pixel_format = V4L2_PIX_FMT_H264;
    }

  return ret;
}

static int bk7258_dvp_h264_enum_frmsize(
  FAR struct file *filep, FAR struct v4l2_frmsizeenum *size)
{
  bool aliased = size != NULL && size->pixel_format == V4L2_PIX_FMT_H264;
  int ret;

  if (aliased)
    {
      size->pixel_format = V4L2_PIX_FMT_JPEG_WITH_SUBIMG;
    }

  ret = g_bk7258_dvp_capture_vops->enum_frmsize(filep, size);
  if (aliased)
    {
      size->pixel_format = V4L2_PIX_FMT_H264;
    }

  return ret;
}

int __wrap_video_register(FAR const char *devpath, FAR struct v4l2_s *ctx)
{
  FAR const struct v4l2_ops_s *original;
  int ret;

  if (devpath == NULL || ctx == NULL || ctx->vops == NULL ||
      strcmp(devpath, "/dev/video0") != 0)
    {
      return __real_video_register(devpath, ctx);
    }

  original = ctx->vops;
  g_bk7258_dvp_capture_vops = original;
  memcpy(&g_bk7258_dvp_h264_vops, original,
         sizeof(g_bk7258_dvp_h264_vops));
  g_bk7258_dvp_h264_vops.g_fmt = bk7258_dvp_h264_g_fmt;
  g_bk7258_dvp_h264_vops.s_fmt = bk7258_dvp_h264_s_fmt;
  g_bk7258_dvp_h264_vops.try_fmt = bk7258_dvp_h264_try_fmt;
  g_bk7258_dvp_h264_vops.enum_fmt = bk7258_dvp_h264_enum_fmt;
  g_bk7258_dvp_h264_vops.enum_frminterval =
    bk7258_dvp_h264_enum_frminterval;
  g_bk7258_dvp_h264_vops.enum_frmsize = bk7258_dvp_h264_enum_frmsize;
  ctx->vops = &g_bk7258_dvp_h264_vops;

  ret = __real_video_register(devpath, ctx);
  if (ret < 0)
    {
      ctx->vops = original;
      g_bk7258_dvp_capture_vops = NULL;
    }

  return ret;
}

int32_t __wrap_sys_drv_int_group2_enable(uint32_t mask)
{
  if (g_bk7258_dvp_h264_opening && mask == BK7258_YUVB_GROUP2_MASK)
    {
      g_bk7258_dvp_yuv_irq_deferred = true;
      return 0;
    }

  return __real_sys_drv_int_group2_enable(mask);
}

int32_t __wrap_sys_drv_core_intr_group2_enable(uint32_t core_id,
                                               uint32_t mask)
{
  if (g_bk7258_dvp_h264_opening && mask == BK7258_H264_GROUP2_MASK)
    {
      g_bk7258_dvp_h264_irq_deferred = true;
      g_bk7258_dvp_h264_irq_core = core_id;
      return 0;
    }

  return __real_sys_drv_core_intr_group2_enable(core_id, mask);
}

bk_err_t __wrap_bk_yuv_buf_start(yuv_mode_t work_mode)
{
  if (g_bk7258_dvp_h264_opening && work_mode == H264_MODE)
    {
      g_bk7258_dvp_h264_yuv_deferred = true;
      return BK_OK;
    }

  return __real_bk_yuv_buf_start(work_mode);
}

bk_err_t __wrap_bk_h264_encode_enable(void)
{
  if (g_bk7258_dvp_h264_opening)
    {
      g_bk7258_dvp_h264_encode_deferred = true;
      return BK_OK;
    }

  return __real_bk_h264_encode_enable();
}

int __wrap_dvp_camera_i2c_write_uint8(uint8_t addr, uint8_t reg,
                                      uint8_t value)
{
  uint8_t write_value = value;

  if (g_bk7258_dvp_h264_opening && reg == 0xf2u && value == 0x0fu)
    {
      g_bk7258_dvp_sensor_output_deferred = true;
      write_value = 0;
    }

  return __real_dvp_camera_i2c_write_uint8(addr, reg, write_value);
}
#endif

static int bk7258_dvp_pm_acquire(FAR struct bk7258_dvp_s *priv)
{
  enum bk7258_pm_clock_e video_clock;
  bool acquired_video = false;
  int ret;

  /* The T5 board and the SDK GC2145 descriptor both require 24 MHz.  Keep
   * the logical CP resource explicit so another board cannot silently use a
   * wrong divider when a different sensor clock is requested. */

  if (priv->sdk_config.clk_source != MCLK_24M)
    {
      return -ENOTSUP;
    }

  video_clock = priv->sdk_config.img_format == IMAGE_H264 ?
                BK7258_PM_CLOCK_H264 : BK7258_PM_CLOCK_JPEG;

  if (!priv->pm_clock_held)
    {
      ret = bk7258_pm_clock_get(video_clock);
      if (ret < 0)
        {
          return ret;
        }

      priv->pm_clock_held = true;
      acquired_video = true;
    }

  if (!priv->pm_mclk_held)
    {
      ret = bk7258_pm_clock_get(BK7258_PM_CLOCK_CAMERA_MCLK_24M);
      if (ret < 0)
        {
          if (acquired_video &&
              bk7258_pm_clock_put(video_clock) >= 0)
            {
              priv->pm_clock_held = false;
            }

          return ret;
        }

      priv->pm_mclk_held = true;
    }

  return 0;
}

static int bk7258_dvp_pm_release(FAR struct bk7258_dvp_s *priv)
{
  enum bk7258_pm_clock_e video_clock;
  int result = 0;
  int ret;

  video_clock = priv->sdk_config.img_format == IMAGE_H264 ?
                BK7258_PM_CLOCK_H264 : BK7258_PM_CLOCK_JPEG;

  if (priv->pm_mclk_held)
    {
      ret = bk7258_pm_clock_put(BK7258_PM_CLOCK_CAMERA_MCLK_24M);
      if (ret >= 0)
        {
          priv->pm_mclk_held = false;
        }
      else
        {
          result = ret;
        }
    }

  if (priv->pm_clock_held)
    {
      ret = bk7258_pm_clock_put(video_clock);
      if (ret >= 0)
        {
          priv->pm_clock_held = false;
        }
      else if (result >= 0)
        {
          result = ret;
        }
    }

  return result;
}

static inline FAR struct bk7258_dvp_s *bk7258_dvp_from_data(
  FAR struct imgdata_s *data)
{
  return (FAR struct bk7258_dvp_s *)((uintptr_t)data -
                                     offsetof(struct bk7258_dvp_s, data));
}

static int bk7258_dvp_frame_index(FAR struct bk7258_dvp_s *priv,
                                  FAR struct frame_buffer_t *frame)
{
  uint8_t i;

  for (i = 0; i < priv->config.frame_count; i++)
    {
      if (&priv->frames[i] == frame)
        {
          return i;
        }
    }

  return -1;
}

static void bk7258_dvp_release_frame_locked(FAR struct bk7258_dvp_s *priv,
                                            FAR struct frame_buffer_t *frame)
{
  int index = bk7258_dvp_frame_index(priv, frame);

  if (index >= 0)
    {
      priv->frame_busy[index] = false;
    }
}

static void bk7258_dvp_drop_events_locked(FAR struct bk7258_dvp_s *priv)
{
  while (priv->event_count != 0)
    {
      FAR struct bk7258_dvp_event_s *event =
        &priv->events[priv->event_head];

      bk7258_dvp_release_frame_locked(priv, event->frame);
      priv->event_head = (uint8_t)((priv->event_head + 1) %
                                   BK7258_DVP_EVENT_DEPTH);
      priv->event_count--;
    }
}

static void bk7258_dvp_schedule_failed(FAR struct bk7258_dvp_s *priv)
{
  irqstate_t flags = spin_lock_irqsave(&priv->lock);

  priv->work_queued = false;
  priv->worker_running = false;
  priv->worker_tid = (pid_t)-1;
  bk7258_dvp_drop_events_locked(priv);
  spin_unlock_irqrestore(&priv->lock, flags);
}

static bool bk7258_dvp_is_current_worker(FAR struct bk7258_dvp_s *priv)
{
  irqstate_t flags;
  bool current;
  pid_t tid = nxsched_gettid();

  flags = spin_lock_irqsave(&priv->lock);
  current = priv->worker_running && priv->worker_tid == tid;
  spin_unlock_irqrestore(&priv->lock, flags);
  return current;
}

static void bk7258_dvp_timestamp(FAR struct timeval *tv)
{
  struct timespec ts;

  /* The SDK timestamp is a 32-bit, build-dependent counter which can wrap
   * in roughly 71 minutes.  It is intentionally not exposed as a V4L2
   * timestamp; use the NuttX system clock for the upper-half ABI. */

  clock_systime_timespec(&ts);
  tv->tv_sec = ts.tv_sec;
  tv->tv_usec = ts.tv_nsec / 1000;
}

static void bk7258_dvp_complete_worker(FAR void *arg)
{
  FAR struct bk7258_dvp_s *priv = arg;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  priv->worker_running = true;
  priv->worker_tid = nxsched_gettid();
  spin_unlock_irqrestore(&priv->lock, flags);

  for (;;)
    {
      struct bk7258_dvp_event_s event;
      FAR uint8_t *target;
      FAR void *capture_arg;
      imgdata_capture_t capture_cb;
      uint32_t target_size;
      uint32_t length;
      struct timeval tv;
      uint8_t result;
      irqstate_t flags;

      flags = spin_lock_irqsave(&priv->lock);
      if (priv->event_count == 0)
        {
          priv->work_queued = false;
          priv->worker_running = false;
          priv->worker_tid = (pid_t)-1;
          spin_unlock_irqrestore(&priv->lock, flags);
          return;
        }

      event = priv->events[priv->event_head];
      priv->event_head = (uint8_t)((priv->event_head + 1) %
                                   BK7258_DVP_EVENT_DEPTH);
      priv->event_count--;
      target = priv->next_buffer;
      target_size = priv->next_size;
      priv->next_buffer = NULL;
      priv->next_size = 0;
      capture_cb = priv->capture_active ? priv->capture_cb : NULL;
      capture_arg = priv->capture_arg;
      spin_unlock_irqrestore(&priv->lock, flags);

      result = event.result;
      length = event.frame->length;
      if (capture_cb == NULL || target == NULL || result != 0)
        {
          length = 0;
        }
      else if (length > event.frame->size || length > target_size ||
               event.frame->frame == NULL)
        {
          result = BK7258_DVP_RESULT_ERROR;
          length = 0;
        }
      else
        {
          memcpy(target, event.frame->frame, length);
        }

      bk7258_dvp_timestamp(&tv);
      if (capture_cb != NULL)
        {
          /* This is the only path that calls the NuttX upper-half callback;
           * the SDK callback itself only enqueues a bounded event. */
          (void)capture_cb(result, length, &tv, capture_arg);
        }

      flags = spin_lock_irqsave(&priv->lock);
      bk7258_dvp_release_frame_locked(priv, event.frame);
      spin_unlock_irqrestore(&priv->lock, flags);
    }
}

static int bk7258_dvp_stop_stream(FAR struct bk7258_dvp_s *priv)
{
  irqstate_t flags;
  bool need_suspend;
  bool current_worker;
  int ret;
  int cancel_ret = 0;

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->sdk_open)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);
      return 0;
    }

  if (priv->stopping)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);

      /* A callback may synchronously call stop_capture while an external
       * stop is already quiescing the stream.  The callback must not wait
       * for itself, while an external caller must still wait for the worker
       * before returning. */
      if (bk7258_dvp_is_current_worker(priv))
        {
          return 0;
        }

      ret = work_cancel_sync(LPWORK, &priv->complete_work);
      if (ret == -ENOENT)
        {
          ret = 0;
        }
      /* The first stop owner performs the final queue drain and clears
       * stopping.  A concurrent external stop must not release that state
       * early and allow a new start/resume to cross the owner cleanup. */
      return ret;
    }

  priv->stopping = true;
  priv->capture_active = false;
  priv->capture_cb = NULL;
  priv->capture_arg = NULL;
  need_suspend = !priv->suspended;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (need_suspend)
    {
      ret = bk7258_dvp_error(bk_dvp_suspend(priv->handle));
      if (ret == 0)
        {
          priv->suspended = true;
        }
    }

  /* Do not hold api_lock while waiting for the work queue.  A capture
   * callback is allowed to call stop_capture synchronously. */
  nxmutex_unlock(&priv->api_lock);

  current_worker = bk7258_dvp_is_current_worker(priv);
  if (!current_worker)
    {
      cancel_ret = work_cancel_sync(LPWORK, &priv->complete_work);
      if (cancel_ret == -ENOENT)
        {
          cancel_ret = 0;
        }
    }

  if (ret == 0 && cancel_ret < 0)
    {
      ret = cancel_ret;
    }

  /* Reacquire only for the final bounded-queue cleanup and state release.
   * If this is the work thread itself, leave work_queued for the worker's
   * empty-queue exit path; synchronous self-cancel is never attempted. */
  if (nxmutex_lock(&priv->api_lock) < 0)
    {
      return ret < 0 ? ret : -EINTR;
    }
  flags = spin_lock_irqsave(&priv->lock);
  bk7258_dvp_drop_events_locked(priv);
  if (!current_worker)
    {
      /* work_cancel_sync() owns the queued work after it returns.  Mirror
       * that state in the wrapper so a later capture can schedule LPWORK
       * again.  A callback stopping itself must leave these fields for the
       * worker's normal empty-queue exit path. */

      priv->work_queued = false;
      priv->worker_running = false;
      priv->worker_tid = (pid_t)-1;
    }
  priv->stopping = false;
  spin_unlock_irqrestore(&priv->lock, flags);
  nxmutex_unlock(&priv->api_lock);
  return ret;
}

static FAR struct frame_buffer_t *bk7258_dvp_frame_malloc(
  image_format_t format, uint32_t size)
{
  FAR struct bk7258_dvp_s *priv = &g_bk7258_dvp;
  irqstate_t flags;
  uint8_t i;

  flags = spin_lock_irqsave(&priv->lock);
  for (i = 0; i < priv->config.frame_count; i++)
    {
      FAR const struct bk7258_dvp_frame_mem_s *memory =
        &priv->config.frames[i];

      if (!priv->frame_busy[i] && memory->addr != NULL &&
          memory->size >= size)
        {
          FAR struct frame_buffer_t *frame = &priv->frames[i];

          memset(frame, 0, sizeof(*frame));
          frame->frame = memory->addr;
          frame->size = memory->size;
          frame->type = DVP_CAMERA;
          frame->width = priv->sdk_config.width;
          frame->height = priv->sdk_config.height;
          frame->fmt = format == IMAGE_MJPEG ? PIXEL_FMT_JPEG :
                       format == IMAGE_H264 ? PIXEL_FMT_H264 :
                       PIXEL_FMT_UNKNOW;
          priv->frame_busy[i] = true;
          spin_unlock_irqrestore(&priv->lock, flags);
          return frame;
        }
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return NULL;
}

static void bk7258_dvp_frame_complete(image_format_t format,
                                      FAR struct frame_buffer_t *frame,
                                      int result)
{
  FAR struct bk7258_dvp_s *priv = &g_bk7258_dvp;
  irqstate_t flags;
  bool queue_work = false;
  int index;

  (void)format;

  flags = spin_lock_irqsave(&priv->lock);
  index = bk7258_dvp_frame_index(priv, frame);
  if (index < 0 || !priv->frame_busy[index] || !priv->capture_active ||
      priv->stopping)
    {
      if (index >= 0)
        {
          priv->frame_busy[index] = false;
        }
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }

  if (priv->event_count >= BK7258_DVP_EVENT_DEPTH)
    {
      /* An ISR cannot wait for NuttX video consumption.  Drop this completed
       * frame and return its descriptor to the bounded pool. */
      priv->frame_busy[index] = false;
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }

  priv->events[(priv->event_head + priv->event_count) %
               BK7258_DVP_EVENT_DEPTH].frame = frame;
  priv->events[(priv->event_head + priv->event_count) %
               BK7258_DVP_EVENT_DEPTH].result = (uint8_t)result;
  priv->event_count++;
  if (!priv->work_queued)
    {
      priv->work_queued = true;
      queue_work = true;
    }
  spin_unlock_irqrestore(&priv->lock, flags);

  if (queue_work && work_queue(LPWORK, &priv->complete_work,
                               bk7258_dvp_complete_worker, priv, 0) < 0)
    {
      bk7258_dvp_schedule_failed(priv);
    }
}

static bool bk7258_dvp_format_supported(FAR struct bk7258_dvp_s *priv,
                                         uint32_t format)
{
  if (priv->sdk_config.img_format == IMAGE_YUV)
    {
      return format == IMGDATA_PIX_FMT_YUYV ||
             format == IMGDATA_PIX_FMT_UYVY;
    }

  if (priv->sdk_config.img_format == IMAGE_MJPEG)
    {
      return format == IMGDATA_PIX_FMT_JPEG;
    }

  if (priv->sdk_config.img_format == IMAGE_H264)
    {
      /* This NuttX revision publishes V4L2_PIX_FMT_H264 but its private
       * V4L2-to-imgdata converter has no matching IMGDATA_PIX_FMT_H264.
       * Unknown compressed formats reach a lower-half as the legacy
       * JPEG_WITH_SUBIMG token.  Accept that token only when this instance
       * was explicitly configured as IMAGE_H264; the public ABI remains
       * standard V4L2 H.264 and no JPEG data is advertised or returned. */

      return format == IMGDATA_PIX_FMT_JPEG_WITH_SUBIMG;
    }

  return false;
}

static uint32_t bk7258_dvp_fps_hz(frame_fps_t fps)
{
  switch (fps)
    {
      case FPS5:
        return 5;
      case FPS10:
        return 10;
      case FPS15:
        return 15;
      case FPS20:
        return 20;
      case FPS25:
        return 25;
      case FPS30:
        return 30;
      default:
        return 0;
    }
}

static int bk7258_dvp_validate_frame_setting(
  FAR struct bk7258_dvp_s *priv, uint8_t nr_datafmts,
  FAR imgdata_format_t *datafmts, FAR imgdata_interval_t *interval)
{
  if (nr_datafmts == 0 || nr_datafmts > 1 || datafmts == NULL)
    {
      return -EINVAL;
    }

  if (datafmts[IMGDATA_FMT_MAIN].width != priv->sdk_config.width ||
      datafmts[IMGDATA_FMT_MAIN].height != priv->sdk_config.height ||
      !bk7258_dvp_format_supported(priv,
                                   datafmts[IMGDATA_FMT_MAIN].pixelformat))
    {
      return -ENOTSUP;
    }

  if (interval != NULL &&
      (interval->numerator == 0 || interval->denominator == 0))
    {
      return -EINVAL;
    }

  if (interval != NULL &&
      (uint64_t)bk7258_dvp_fps_hz((frame_fps_t)priv->sdk_config.fps) *
      interval->numerator !=
      interval->denominator)
    {
      return -ENOTSUP;
    }

  return 0;
}

static int bk7258_dvp_data_init(FAR struct imgdata_s *data)
{
  FAR struct bk7258_dvp_s *priv = bk7258_dvp_from_data(data);
  irqstate_t flags;
  bk_err_t error;
  int ret;

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->stopping)
    {
      nxmutex_unlock(&priv->api_lock);
      return -EBUSY;
    }

  if (priv->sdk_open)
    {
      nxmutex_unlock(&priv->api_lock);
      return 0;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->work_queued || priv->worker_running)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);
      return -EBUSY;
    }

  memset(priv->frame_busy, 0, sizeof(priv->frame_busy));
  priv->event_head = 0;
  priv->event_count = 0;
  priv->work_queued = false;
  priv->capture_active = false;
  priv->capture_cb = NULL;
  priv->capture_arg = NULL;
  priv->next_buffer = NULL;
  priv->next_size = 0;
  priv->stopping = false;
  priv->worker_running = false;
  priv->worker_tid = (pid_t)-1;
  spin_unlock_irqrestore(&priv->lock, flags);

  /* Tuya's T5AI GC2145 path powers the video/JPEG clock before it enables
   * the 24 MHz sensor MCLK and touches SCCB.  The immutable v3.1.1.9 DVP
   * detector configures MCLK before its JPEG driver acquires that clock.
   * Acquire it explicitly through the project-owned CP clock service so the
   * vendor detector observes the same prerequisite without bypassing the
   * NuttX/CP ownership boundary. */

  /* v3.1.1.9 treats DMA, YUV buffer and JPEG encoder as AP-wide shared
   * drivers.  The SDK common bring-up (and Tuya's T5AI port) initializes
   * all three before opening DVP.  NuttX does not call that vendor-wide
   * initializer, so perform the same idempotent operations at this wrapper
   * boundary.  Deliberately do not deinitialize them on camera close: LCD,
   * I2S and other AP clients share their global state, channel pool or IRQs. */

  ret = bk7258_dvp_error(bk_dma_driver_init());
  if (ret < 0)
    {
      nxmutex_unlock(&priv->api_lock);
      return ret;
    }

  ret = bk7258_dvp_error(bk_yuv_buf_driver_init());
  if (ret < 0)
    {
      nxmutex_unlock(&priv->api_lock);
      return ret;
    }

  if (priv->sdk_config.img_format == IMAGE_H264)
    {
      ret = bk7258_dvp_error(bk_h264_driver_init());
    }
  else
    {
      ret = bk7258_dvp_error(bk_jpeg_enc_driver_init());
    }
  if (ret < 0)
    {
      nxmutex_unlock(&priv->api_lock);
      return ret;
    }

  ret = bk7258_dvp_pm_acquire(priv);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->api_lock);
      return ret;
    }

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264
  if (priv->sdk_config.img_format == IMAGE_H264)
    {
      g_bk7258_dvp_h264_opening = true;
      g_bk7258_dvp_h264_yuv_deferred = false;
      g_bk7258_dvp_h264_encode_deferred = false;
      g_bk7258_dvp_yuv_irq_deferred = false;
      g_bk7258_dvp_h264_irq_deferred = false;
      g_bk7258_dvp_sensor_output_deferred = false;
    }
#endif

  error = bk_dvp_open(&priv->handle, &priv->sdk_config,
                      &g_bk7258_dvp_callback, priv->config.encode_buffer);
  ret = bk7258_dvp_error(error);
#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264
  if (priv->sdk_config.img_format == IMAGE_H264)
    {
      g_bk7258_dvp_h264_opening = false;
    }
#endif
  if (ret < 0)
    {
      priv->handle = NULL;
      (void)bk7258_dvp_pm_release(priv);

      nxmutex_unlock(&priv->api_lock);
      return ret;
    }

  /* From this point the SDK owns a live handle.  Publish that ownership
   * before replaying the board-deferred start operations so every later
   * failure is routed through the same bk_dvp_close()/PM cleanup path as a
   * normal imgdata uninitialize.  Tuya and the official SDK both pair a
   * successful hardware open with a complete deinit sequence. */

  priv->sdk_open = true;
  priv->suspended = false;

  /* v3.1.1.9 starts the H.264 data path before programming the GC2145.
   * On this board the sensor begins driving DVP near the OUTPUT entries of
   * its init table, which can raise YUV/H.264 IRQs while bk_dvp_open() still
   * owns partially initialized state.  Defer only the first H.264 start
   * until the immutable SDK has completed sensor init; later frame restarts
   * still pass straight through the wrappers above.
   */

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264
  if (priv->sdk_config.img_format == IMAGE_H264)
    {
      if (g_bk7258_dvp_yuv_irq_deferred)
        {
          error = __real_sys_drv_int_group2_enable(
            BK7258_YUVB_GROUP2_MASK);
          if (error != 0)
            {
              ret = -EIO;
              goto fail_open;
            }
        }

      if (g_bk7258_dvp_h264_irq_deferred)
        {
          error = __real_sys_drv_core_intr_group2_enable(
            g_bk7258_dvp_h264_irq_core, BK7258_H264_GROUP2_MASK);
          if (error != 0)
            {
              ret = -EIO;
              goto fail_open;
            }
        }

      if (g_bk7258_dvp_h264_yuv_deferred)
        {
          ret = bk7258_dvp_error(__real_bk_yuv_buf_start(H264_MODE));
          if (ret < 0)
            {
              goto fail_open;
            }
        }

      if (g_bk7258_dvp_h264_encode_deferred)
        {
          ret = bk7258_dvp_error(__real_bk_h264_encode_enable());
          if (ret < 0)
            {
              goto fail_open;
            }
        }

      if (g_bk7258_dvp_sensor_output_deferred)
        {
          ret = bk7258_dvp_error(__real_dvp_camera_i2c_write_uint8(
            0x78u >> 1, 0xf2u, 0x0fu));
          if (ret < 0)
            {
              goto fail_open;
            }
        }

    }
#endif

  /* bk_dvp_open() starts the vendor stream before returning.  Keep that
   * official first-start sequence intact: v3.1.1.9's resume path is meant
   * for an already-running stream and does not reproduce every open-time
   * transition.  Until V4L2 queues its first buffer, the bounded callback
   * pool safely drops completed frames. */

  nxmutex_unlock(&priv->api_lock);
  return 0;

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA_H264
fail_open:
  nxmutex_unlock(&priv->api_lock);
  (void)bk7258_dvp_data_uninit(data);
  return ret;
#endif
}

static int bk7258_dvp_data_uninit(FAR struct imgdata_s *data)
{
  FAR struct bk7258_dvp_s *priv = bk7258_dvp_from_data(data);
  irqstate_t flags;
  bool current_worker;
  int stop_ret;
  int close_ret;
  int pm_ret = 0;
  int cancel_ret = 0;
  int ret;

  stop_ret = bk7258_dvp_stop_stream(priv);

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->sdk_open)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);
      return stop_ret;
    }

  if (priv->stopping)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);
      return stop_ret < 0 ? stop_ret : -EBUSY;
    }

  priv->stopping = true;
  priv->capture_active = false;
  priv->capture_cb = NULL;
  priv->capture_arg = NULL;
  camera_handle_t handle = priv->handle;
  spin_unlock_irqrestore(&priv->lock, flags);
  nxmutex_unlock(&priv->api_lock);

  /* bk_dvp_close() may wait for the SDK's own frame path.  Do not hold the
   * wrapper mutex while it runs, and never synchronously cancel this worker
   * from itself. */
  close_ret = bk7258_dvp_error(bk_dvp_close(handle));
  current_worker = bk7258_dvp_is_current_worker(priv);
  if (!current_worker)
    {
      cancel_ret = work_cancel_sync(LPWORK, &priv->complete_work);
      if (cancel_ret == -ENOENT)
        {
          cancel_ret = 0;
        }
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return close_ret < 0 ? close_ret : ret;
    }
  flags = spin_lock_irqsave(&priv->lock);
  bk7258_dvp_drop_events_locked(priv);
  if (!current_worker)
    {
      priv->work_queued = false;
      priv->worker_running = false;
      priv->worker_tid = (pid_t)-1;
    }
  if (close_ret == 0)
    {
      priv->handle = NULL;
      priv->sdk_open = false;
      priv->suspended = false;
    }
  priv->stopping = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (close_ret == 0)
    {
      pm_ret = bk7258_dvp_pm_release(priv);
    }

  nxmutex_unlock(&priv->api_lock);

  if (close_ret < 0)
    {
      return close_ret;
    }
  if (stop_ret < 0)
    {
      return stop_ret;
    }
  if (cancel_ret < 0)
    {
      return cancel_ret;
    }

  return pm_ret < 0 ? pm_ret : 0;
}

static int bk7258_dvp_data_set_buf(FAR struct imgdata_s *data,
                                   uint8_t nr_datafmts,
                                   FAR imgdata_format_t *datafmts,
                                   FAR uint8_t *addr, uint32_t size)
{
  FAR struct bk7258_dvp_s *priv = bk7258_dvp_from_data(data);
  irqstate_t flags;
  int ret;

  ret = bk7258_dvp_validate_frame_setting(priv, nr_datafmts, datafmts,
                                          NULL);
  if (ret < 0 || addr == NULL || size == 0)
    {
      return ret < 0 ? ret : -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->sdk_open)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ENODEV;
    }
  if (priv->stopping)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }
  if (priv->next_buffer != NULL)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }
  priv->next_buffer = addr;
  priv->next_size = size;
  spin_unlock_irqrestore(&priv->lock, flags);
  return 0;
}

static int bk7258_dvp_data_validate_frame_setting(
  FAR struct imgdata_s *data, uint8_t nr_datafmts,
  FAR imgdata_format_t *datafmts, FAR imgdata_interval_t *interval)
{
  FAR struct bk7258_dvp_s *priv = bk7258_dvp_from_data(data);

  return bk7258_dvp_validate_frame_setting(priv, nr_datafmts, datafmts,
                                           interval);
}

static int bk7258_dvp_data_start_capture(
  FAR struct imgdata_s *data, uint8_t nr_datafmts,
  FAR imgdata_format_t *datafmts, FAR imgdata_interval_t *interval,
  imgdata_capture_t callback, FAR void *arg)
{
  FAR struct bk7258_dvp_s *priv = bk7258_dvp_from_data(data);
  irqstate_t flags;
  bool resume;
  int ret;

  ret = bk7258_dvp_validate_frame_setting(priv, nr_datafmts, datafmts,
                                          interval);
  if (ret < 0 || callback == NULL)
    {
      return ret < 0 ? ret : -EINVAL;
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->sdk_open)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);
      return -ENODEV;
    }
  if (priv->stopping)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);
      return -EBUSY;
    }
  if (priv->capture_active)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);
      return -EBUSY;
    }
  if (priv->next_buffer == NULL)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&priv->api_lock);
      return -EINVAL;
    }
  resume = priv->suspended;
  priv->capture_cb = callback;
  priv->capture_arg = arg;
  priv->capture_active = true;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (resume)
    {
      ret = bk7258_dvp_error(bk_dvp_resume(priv->handle));
      if (ret < 0)
        {
          flags = spin_lock_irqsave(&priv->lock);
          priv->capture_active = false;
          priv->capture_cb = NULL;
          priv->capture_arg = NULL;
          spin_unlock_irqrestore(&priv->lock, flags);
          nxmutex_unlock(&priv->api_lock);
          return ret;
        }
      priv->suspended = false;
    }

  nxmutex_unlock(&priv->api_lock);
  return 0;
}

static int bk7258_dvp_data_stop_capture(FAR struct imgdata_s *data)
{
  FAR struct bk7258_dvp_s *priv = bk7258_dvp_from_data(data);
  return bk7258_dvp_stop_stream(priv);
}

#ifdef CONFIG_BK7258_PSRAM
static FAR void *bk7258_dvp_data_alloc(FAR struct imgdata_s *data,
                                       uint32_t align_size, uint32_t size)
{
  FAR struct bk7258_dvp_allocation_s *allocation;
  FAR uint8_t *base;
  uintptr_t address;
  uintptr_t remainder;
  size_t overhead;

  (void)data;

  if (size == 0 || !bk7258_psram_ready())
    {
      return NULL;
    }

  if (align_size < sizeof(uintptr_t))
    {
      align_size = sizeof(uintptr_t);
    }

  overhead = sizeof(*allocation) + align_size - 1u;
  if (size > SIZE_MAX - overhead)
    {
      return NULL;
    }

  base = bk7258_psram_malloc(size + overhead);
  if (base == NULL)
    {
      return NULL;
    }

  address = (uintptr_t)(base + sizeof(*allocation));
  remainder = address % align_size;
  if (remainder != 0)
    {
      address += align_size - remainder;
    }

  allocation = (FAR struct bk7258_dvp_allocation_s *)address - 1;
  allocation->base = base;
  allocation->magic = BK7258_DVP_ALLOC_MAGIC;
  return (FAR void *)address;
}

static void bk7258_dvp_data_free(FAR struct imgdata_s *data, FAR void *addr)
{
  FAR struct bk7258_dvp_allocation_s *allocation;
  FAR void *base;

  (void)data;

  if (addr == NULL)
    {
      return;
    }

  allocation = (FAR struct bk7258_dvp_allocation_s *)addr - 1;
  if (allocation->magic != BK7258_DVP_ALLOC_MAGIC ||
      !bk7258_psram_heap_contains(allocation->base))
    {
      return;
    }

  base = allocation->base;
  allocation->magic = 0;
  allocation->base = NULL;
  bk7258_psram_free(base);
}
#endif

static const struct imgdata_ops_s g_bk7258_dvp_data_ops =
{
  .init = bk7258_dvp_data_init,
  .uninit = bk7258_dvp_data_uninit,
  .set_buf = bk7258_dvp_data_set_buf,
  .validate_frame_setting = bk7258_dvp_data_validate_frame_setting,
  .start_capture = bk7258_dvp_data_start_capture,
  .stop_capture = bk7258_dvp_data_stop_capture,
#ifdef CONFIG_BK7258_PSRAM
  .alloc = bk7258_dvp_data_alloc,
  .free = bk7258_dvp_data_free,
#endif
};

int bk7258_dvp_initialize(FAR const struct bk7258_dvp_config_s *config,
                          FAR struct bk7258_dvp_s **out)
{
  irqstate_t flags;
  uint64_t encode_buffer_size;
  uint64_t yuv_size;
  uint8_t i;
  int ret;

  if (config == NULL || out == NULL || config->frames == NULL ||
      config->frame_count < 2 || config->frame_count > BK7258_DVP_MAX_FRAMES)
    {
      return -EINVAL;
    }

  if (config->sdk.width == 0 || config->sdk.height == 0 ||
      bk7258_dvp_fps_hz((frame_fps_t)config->sdk.fps) == 0 ||
      (config->sdk.img_format != IMAGE_YUV &&
       config->sdk.img_format != IMAGE_MJPEG &&
       config->sdk.img_format != IMAGE_H264))
    {
      return -ENOTSUP;
    }

  if ((config->sdk.img_format == IMAGE_MJPEG ||
       config->sdk.img_format == IMAGE_H264) &&
      (config->encode_buffer == NULL || config->encode_buffer_size == 0))
    {
      return -EINVAL;
    }

  encode_buffer_size = (uint64_t)config->sdk.width *
                       (config->sdk.img_format == IMAGE_H264 ? 32u : 16u) *
                       2u;
  if (config->sdk.img_format != IMAGE_YUV &&
      (encode_buffer_size > UINT32_MAX ||
       config->encode_buffer_size < encode_buffer_size))
    {
      return -EINVAL;
    }

  yuv_size = (uint64_t)config->sdk.width * config->sdk.height * 2;
  if (yuv_size > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  for (i = 0; i < config->frame_count; i++)
    {
      uint32_t required = config->sdk.img_format == IMAGE_YUV ?
                          (uint32_t)yuv_size :
                          config->sdk.img_format == IMAGE_H264 ?
                          CONFIG_H264_FRAME_SIZE : CONFIG_JPEG_FRAME_SIZE;

      if (config->frames[i].addr == NULL ||
          config->frames[i].size < required)
        {
          return -EINVAL;
        }
    }

  ret = nxmutex_lock(&g_bk7258_dvp.api_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_dvp.configured)
    {
      bool same = g_bk7258_dvp.config.frames == config->frames &&
                  g_bk7258_dvp.config.frame_count == config->frame_count &&
                  g_bk7258_dvp.config.encode_buffer == config->encode_buffer &&
                  g_bk7258_dvp.config.encode_buffer_size ==
                  config->encode_buffer_size &&
                  memcmp(&g_bk7258_dvp.sdk_config, &config->sdk,
                         sizeof(config->sdk)) == 0;

      nxmutex_unlock(&g_bk7258_dvp.api_lock);
      if (same)
        {
          *out = &g_bk7258_dvp;
          return 0;
        }
      return -EBUSY;
    }

  spin_lock_init(&g_bk7258_dvp.lock);
  g_bk7258_dvp.config = *config;
  g_bk7258_dvp.sdk_config = config->sdk;
  g_bk7258_dvp.data.ops = &g_bk7258_dvp_data_ops;
  g_bk7258_dvp.configured = true;
  g_bk7258_dvp.handle = NULL;
  g_bk7258_dvp.pm_clock_held = false;
  g_bk7258_dvp.pm_mclk_held = false;
  g_bk7258_dvp.sdk_open = false;
  g_bk7258_dvp.suspended = false;
  flags = spin_lock_irqsave(&g_bk7258_dvp.lock);
  memset(g_bk7258_dvp.frame_busy, 0, sizeof(g_bk7258_dvp.frame_busy));
  g_bk7258_dvp.event_head = 0;
  g_bk7258_dvp.event_count = 0;
  g_bk7258_dvp.work_queued = false;
  g_bk7258_dvp.worker_running = false;
  g_bk7258_dvp.worker_tid = (pid_t)-1;
  g_bk7258_dvp.stopping = false;
  g_bk7258_dvp.capture_active = false;
  spin_unlock_irqrestore(&g_bk7258_dvp.lock, flags);
  *out = &g_bk7258_dvp;
  nxmutex_unlock(&g_bk7258_dvp.api_lock);
  return 0;
}

FAR struct imgdata_s *bk7258_dvp_get_imgdata(FAR struct bk7258_dvp_s *priv)
{
  return priv == &g_bk7258_dvp && priv->configured ? &priv->data : NULL;
}

int bk7258_dvp_uninitialize(FAR struct bk7258_dvp_s *priv)
{
  int ret;

  if (priv != &g_bk7258_dvp)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }
  if (priv->sdk_open)
    {
      nxmutex_unlock(&priv->api_lock);
      return -EBUSY;
    }

  if (priv->pm_mclk_held || priv->pm_clock_held)
    {
      ret = bk7258_dvp_pm_release(priv);
      if (ret < 0)
        {
          nxmutex_unlock(&priv->api_lock);
          return ret;
        }
    }

  priv->configured = false;
  memset(&priv->config, 0, sizeof(priv->config));
  memset(&priv->sdk_config, 0, sizeof(priv->sdk_config));
  priv->data.ops = NULL;
  nxmutex_unlock(&priv->api_lock);
  return 0;
}

int bk7258_dvp_suspend(FAR struct bk7258_dvp_s *priv)
{
  int ret;

  if (priv != &g_bk7258_dvp)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }
  if (!priv->sdk_open)
    {
      nxmutex_unlock(&priv->api_lock);
      return -ENODEV;
    }
  if (priv->suspended)
    {
      nxmutex_unlock(&priv->api_lock);
      return 0;
    }

  ret = bk7258_dvp_error(bk_dvp_suspend(priv->handle));
  if (ret == 0)
    {
      priv->suspended = true;
    }
  nxmutex_unlock(&priv->api_lock);
  return ret;
}

int bk7258_dvp_resume(FAR struct bk7258_dvp_s *priv)
{
  int ret;

  if (priv != &g_bk7258_dvp)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->api_lock);
  if (ret < 0)
    {
      return ret;
    }
  if (!priv->sdk_open)
    {
      nxmutex_unlock(&priv->api_lock);
      return -ENODEV;
    }
  if (!priv->suspended)
    {
      nxmutex_unlock(&priv->api_lock);
      return 0;
    }

  ret = bk7258_dvp_error(bk_dvp_resume(priv->handle));
  if (ret == 0)
    {
      priv->suspended = false;
    }
  nxmutex_unlock(&priv->api_lock);
  return ret;
}
