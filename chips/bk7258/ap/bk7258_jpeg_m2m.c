/****************************************************************************
 * chips/bk7258/ap/bk7258_jpeg_m2m.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP JPEG decoder V4L2 memory-to-memory adapter.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <debug.h>

#include <nuttx/cache.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/video/v4l2_m2m.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bk7258_jpeg_decoder.h>
#include <arch/chip/bk7258_jpeg_m2m.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_JPEG_M2M_NAME              "bk7258-jpeg"
#define BK7258_JPEG_M2M_WORKER_NAME       "bkjpeg-m2m"
#define BK7258_JPEG_M2M_BUFFER_COUNT      2u
#define BK7258_JPEG_M2M_PIXEL_BYTES       2u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_jpeg_m2m_file_s
{
  mutex_t life_lock;
  mutex_t state_lock;
  FAR struct kwork_wqueue_s *wqueue;
  struct work_s work;
  FAR void *cookie;
  FAR struct bk7258_jpeg_decoder_s *decoder;
  struct v4l2_format output_fmt;
  struct v4l2_format capture_fmt;
  uint32_t epoch;
  uint32_t sequence;
  bool output_on;
  bool capture_on;
  bool closing;
  bool inflight;
  bool formats_locked;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_jpeg_m2m_open(FAR void *cookie, FAR void **priv);
static int bk7258_jpeg_m2m_close(FAR void *arg);
static int bk7258_jpeg_m2m_capture_streamon(FAR void *arg);
static int bk7258_jpeg_m2m_output_streamon(FAR void *arg);
static int bk7258_jpeg_m2m_capture_streamoff(FAR void *arg);
static int bk7258_jpeg_m2m_output_streamoff(FAR void *arg);
static int bk7258_jpeg_m2m_available(FAR void *arg);
static int bk7258_jpeg_m2m_querycap(
  FAR void *arg, FAR struct v4l2_capability *cap);
static int bk7258_jpeg_m2m_capture_enum_fmt(
  FAR void *arg, FAR struct v4l2_fmtdesc *fmt);
static int bk7258_jpeg_m2m_output_enum_fmt(
  FAR void *arg, FAR struct v4l2_fmtdesc *fmt);
static int bk7258_jpeg_m2m_capture_g_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt);
static int bk7258_jpeg_m2m_output_g_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt);
static int bk7258_jpeg_m2m_capture_s_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt);
static int bk7258_jpeg_m2m_output_s_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt);
static int bk7258_jpeg_m2m_capture_try_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt);
static int bk7258_jpeg_m2m_output_try_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt);
static size_t bk7258_jpeg_m2m_capture_g_bufsize(FAR void *arg);
static size_t bk7258_jpeg_m2m_output_g_bufsize(FAR void *arg);
static size_t bk7258_jpeg_m2m_g_bufcnt(FAR void *arg);
static int bk7258_jpeg_m2m_try_memory(
  FAR void *arg, enum v4l2_memory memory);
static void bk7258_jpeg_m2m_worker(FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct codec_ops_s g_bk7258_jpeg_m2m_ops =
{
  .open               = bk7258_jpeg_m2m_open,
  .close              = bk7258_jpeg_m2m_close,
  .capture_streamon   = bk7258_jpeg_m2m_capture_streamon,
  .output_streamon    = bk7258_jpeg_m2m_output_streamon,
  .capture_streamoff  = bk7258_jpeg_m2m_capture_streamoff,
  .output_streamoff   = bk7258_jpeg_m2m_output_streamoff,
  .capture_available  = bk7258_jpeg_m2m_available,
  .output_available   = bk7258_jpeg_m2m_available,
  .querycap           = bk7258_jpeg_m2m_querycap,
  .capture_enum_fmt   = bk7258_jpeg_m2m_capture_enum_fmt,
  .output_enum_fmt    = bk7258_jpeg_m2m_output_enum_fmt,
  .capture_g_fmt      = bk7258_jpeg_m2m_capture_g_fmt,
  .output_g_fmt       = bk7258_jpeg_m2m_output_g_fmt,
  .capture_s_fmt      = bk7258_jpeg_m2m_capture_s_fmt,
  .output_s_fmt       = bk7258_jpeg_m2m_output_s_fmt,
  .capture_try_fmt    = bk7258_jpeg_m2m_capture_try_fmt,
  .output_try_fmt     = bk7258_jpeg_m2m_output_try_fmt,
  .capture_g_bufsize  = bk7258_jpeg_m2m_capture_g_bufsize,
  .output_g_bufsize   = bk7258_jpeg_m2m_output_g_bufsize,
  .capture_g_bufcnt   = bk7258_jpeg_m2m_g_bufcnt,
  .output_g_bufcnt    = bk7258_jpeg_m2m_g_bufcnt,
  .capture_try_memory = bk7258_jpeg_m2m_try_memory,
  .output_try_memory  = bk7258_jpeg_m2m_try_memory,
};

static struct codec_s g_bk7258_jpeg_m2m_codec =
{
  .ops = &g_bk7258_jpeg_m2m_ops,
};

/* The immutable SDK decoder is a singleton.  A failed uninitialize keeps
 * that SDK owner alive, so retain it outside the per-open object and retry
 * cleanup before the next initialize instead of losing the only valid
 * handle. */

static mutex_t g_bk7258_jpeg_m2m_device_lock = NXMUTEX_INITIALIZER;
static FAR struct bk7258_jpeg_decoder_s *g_bk7258_jpeg_m2m_orphan;
static bool g_bk7258_jpeg_m2m_opened;
static bool g_bk7258_jpeg_m2m_registered;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_jpeg_m2m_exact_output_type(uint32_t type)
{
  return type == V4L2_BUF_TYPE_VIDEO_OUTPUT;
}

static bool bk7258_jpeg_m2m_exact_capture_type(uint32_t type)
{
  return type == V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

static int bk7258_jpeg_m2m_capture_sizes(uint32_t width, uint32_t height,
                                         FAR uint32_t *payload,
                                         FAR uint32_t *storage,
                                         FAR size_t *cache_line)
{
  uint64_t payload64;
  uint64_t storage64;
  size_t line;

  if (payload == NULL || storage == NULL)
    {
      return -EINVAL;
    }

  line = up_get_dcache_linesize();
  if (line == 0)
    {
      line = sizeof(uintptr_t);
    }

  if ((line & (line - 1)) != 0)
    {
      return -EIO;
    }

  payload64 = (uint64_t)width * height * BK7258_JPEG_M2M_PIXEL_BYTES;
  if (payload64 == 0 || payload64 > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  storage64 = (payload64 + line - 1) & ~((uint64_t)line - 1);
  if (storage64 > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  *payload = (uint32_t)payload64;
  *storage = (uint32_t)storage64;
  if (cache_line != NULL)
    {
      *cache_line = line;
    }

  return 0;
}

static int bk7258_jpeg_m2m_check_config(void)
{
  uint32_t capture_payload;
  uint32_t capture_storage;

  if (CONFIG_BK7258_JPEG_M2M_DEFAULT_HEIGHT < 1 ||
      CONFIG_BK7258_JPEG_M2M_MAX_WIDTH < 2 ||
      CONFIG_BK7258_JPEG_M2M_MAX_HEIGHT < 1 ||
      CONFIG_BK7258_JPEG_M2M_DEFAULT_WIDTH >
        CONFIG_BK7258_JPEG_M2M_MAX_WIDTH ||
      CONFIG_BK7258_JPEG_M2M_DEFAULT_HEIGHT >
        CONFIG_BK7258_JPEG_M2M_MAX_HEIGHT ||
      CONFIG_BK7258_JPEG_M2M_MAX_WIDTH > UINT16_MAX ||
      CONFIG_BK7258_JPEG_M2M_MAX_HEIGHT > UINT16_MAX ||
      CONFIG_BK7258_JPEG_M2M_MAX_INPUT_SIZE < 1 ||
      CONFIG_BK7258_JPEG_M2M_WORKER_STACKSIZE < 1)
    {
      return -EINVAL;
    }

  return bk7258_jpeg_m2m_capture_sizes(
    CONFIG_BK7258_JPEG_M2M_MAX_WIDTH & ~1u,
    CONFIG_BK7258_JPEG_M2M_MAX_HEIGHT,
    &capture_payload, &capture_storage, NULL);
}

static uint32_t bk7258_jpeg_m2m_width(uint32_t width)
{
  uint32_t maximum = CONFIG_BK7258_JPEG_M2M_MAX_WIDTH & ~1u;

  if (width < 2)
    {
      width = CONFIG_BK7258_JPEG_M2M_DEFAULT_WIDTH;
    }

  if (width < 2)
    {
      width = 2;
    }

  if (width > maximum)
    {
      width = maximum;
    }

  return width & ~1u;
}

static uint32_t bk7258_jpeg_m2m_height(uint32_t height)
{
  if (height < 1)
    {
      height = CONFIG_BK7258_JPEG_M2M_DEFAULT_HEIGHT;
    }

  if (height > CONFIG_BK7258_JPEG_M2M_MAX_HEIGHT)
    {
      height = CONFIG_BK7258_JPEG_M2M_MAX_HEIGHT;
    }

  return height;
}

static void bk7258_jpeg_m2m_make_output_format(
  FAR struct v4l2_format *fmt, uint32_t width, uint32_t height)
{
  memset(fmt, 0, sizeof(*fmt));
  fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  fmt->fmt.pix.width = width;
  fmt->fmt.pix.height = height;
  fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
  fmt->fmt.pix.field = V4L2_FIELD_NONE;
  fmt->fmt.pix.bytesperline = 0;
  fmt->fmt.pix.sizeimage = CONFIG_BK7258_JPEG_M2M_MAX_INPUT_SIZE;
  fmt->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
}

static int bk7258_jpeg_m2m_make_capture_format(
  FAR struct v4l2_format *fmt, uint32_t width, uint32_t height)
{
  uint32_t payload;
  uint32_t storage;
  int ret;

  ret = bk7258_jpeg_m2m_capture_sizes(width, height, &payload, &storage,
                                       NULL);
  if (ret < 0)
    {
      return ret;
    }

  memset(fmt, 0, sizeof(*fmt));
  fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt->fmt.pix.width = width;
  fmt->fmt.pix.height = height;
  fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  fmt->fmt.pix.field = V4L2_FIELD_NONE;
  fmt->fmt.pix.bytesperline = payload / height;
  fmt->fmt.pix.sizeimage = storage;
  fmt->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
  return 0;
}

static int bk7258_jpeg_m2m_retry_orphan_locked(void)
{
  int ret;

  if (g_bk7258_jpeg_m2m_orphan == NULL)
    {
      return 0;
    }

  ret = bk7258_jpeg_decoder_uninitialize(g_bk7258_jpeg_m2m_orphan);
  if (ret >= 0)
    {
      g_bk7258_jpeg_m2m_orphan = NULL;
    }

  return ret;
}

/* Initialize can return an error together with a recovery-only handle when
 * immutable-SDK rollback itself fails.  Preserve that handle as the sole
 * orphan so a later open can finish teardown instead of creating a second
 * hardware owner.  The device lock is held by every caller. */

static int bk7258_jpeg_m2m_initialize_backend_locked(
  FAR struct bk7258_jpeg_m2m_file_s *priv)
{
  FAR struct bk7258_jpeg_decoder_s *decoder = NULL;
  int ret;

  ret = bk7258_jpeg_decoder_initialize(&decoder);
  if (ret < 0)
    {
      if (decoder != NULL)
        {
          DEBUGASSERT(g_bk7258_jpeg_m2m_orphan == NULL);
          g_bk7258_jpeg_m2m_orphan = decoder;
        }

      priv->decoder = NULL;
      return ret;
    }

  if (decoder == NULL)
    {
      return -EIO;
    }

  priv->decoder = decoder;
  return 0;
}

static int bk7258_jpeg_m2m_claim_backend(
  FAR struct bk7258_jpeg_m2m_file_s *priv)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_jpeg_m2m_device_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_jpeg_m2m_opened)
    {
      ret = -EBUSY;
      goto out_unlock;
    }

  ret = bk7258_jpeg_m2m_retry_orphan_locked();
  if (ret < 0)
    {
      goto out_unlock;
    }

  ret = bk7258_jpeg_m2m_initialize_backend_locked(priv);
  if (ret >= 0)
    {
      g_bk7258_jpeg_m2m_opened = true;
    }

out_unlock:
  nxmutex_unlock(&g_bk7258_jpeg_m2m_device_lock);
  return ret;
}

static int bk7258_jpeg_m2m_ensure_backend(
  FAR struct bk7258_jpeg_m2m_file_s *priv)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_jpeg_m2m_device_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->decoder != NULL)
    {
      ret = 0;
    }
  else
    {
      ret = bk7258_jpeg_m2m_retry_orphan_locked();
      if (ret >= 0)
        {
          ret = bk7258_jpeg_m2m_initialize_backend_locked(priv);
        }
    }

  nxmutex_unlock(&g_bk7258_jpeg_m2m_device_lock);
  return ret;
}

static int bk7258_jpeg_m2m_reset_backend(
  FAR struct bk7258_jpeg_m2m_file_s *priv)
{
  FAR struct bk7258_jpeg_decoder_s *decoder;
  int ret;

  ret = nxmutex_lock(&g_bk7258_jpeg_m2m_device_lock);
  if (ret < 0)
    {
      return ret;
    }

  decoder = priv->decoder;
  priv->decoder = NULL;
  if (decoder != NULL)
    {
      ret = bk7258_jpeg_decoder_uninitialize(decoder);
      if (ret < 0)
        {
          g_bk7258_jpeg_m2m_orphan = decoder;
          goto out_unlock;
        }
    }

  ret = bk7258_jpeg_m2m_retry_orphan_locked();
  if (ret >= 0)
    {
      ret = bk7258_jpeg_m2m_initialize_backend_locked(priv);
    }

out_unlock:
  nxmutex_unlock(&g_bk7258_jpeg_m2m_device_lock);
  return ret;
}

static int bk7258_jpeg_m2m_release_backend(
  FAR struct bk7258_jpeg_m2m_file_s *priv)
{
  FAR struct bk7258_jpeg_decoder_s *decoder;
  int ret = 0;
  int lockret;

  lockret = nxmutex_lock(&g_bk7258_jpeg_m2m_device_lock);
  if (lockret < 0)
    {
      return lockret;
    }

  decoder = priv->decoder;
  priv->decoder = NULL;
  if (decoder != NULL)
    {
      ret = bk7258_jpeg_decoder_uninitialize(decoder);
      if (ret < 0)
        {
          g_bk7258_jpeg_m2m_orphan = decoder;
        }
    }

  g_bk7258_jpeg_m2m_opened = false;
  nxmutex_unlock(&g_bk7258_jpeg_m2m_device_lock);
  return ret;
}

static bool bk7258_jpeg_m2m_fatal_error(int ret)
{
  return ret == -EIO || ret == -EBUSY || ret == -ETIMEDOUT ||
         ret == -ENODEV || ret == -ESHUTDOWN;
}

/* state_lock is held.  Queueing while holding it closes the race in which
 * close marks the file closing, synchronously cancels the work, and an
 * already-entered available callback queues the same work afterwards. */

static int bk7258_jpeg_m2m_kick_locked(
  FAR struct bk7258_jpeg_m2m_file_s *priv)
{
  if (priv->closing || !priv->output_on || !priv->capture_on ||
      priv->wqueue == NULL)
    {
      return 0;
    }

  return work_queue_wq(priv->wqueue, &priv->work,
                       bk7258_jpeg_m2m_worker, priv, 0);
}

static void bk7258_jpeg_m2m_fail_buffer(
  FAR struct v4l2_buffer *buf, uint32_t type)
{
  buf->type = type;
  buf->flags &= ~V4L2_BUF_FLAG_LAST;
  buf->flags |= V4L2_BUF_FLAG_ERROR;
  if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      buf->bytesused = 0;
      buf->field = V4L2_FIELD_NONE;
    }
}

/* state_lock is held and no worker is queued or running. */

static void bk7258_jpeg_m2m_drain_locked(
  FAR struct bk7258_jpeg_m2m_file_s *priv, uint32_t type)
{
  FAR struct v4l2_buffer *buf;
  int ret;

  for (; ; )
    {
      if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
        {
          buf = codec_output_get_buf(priv->cookie);
        }
      else
        {
          buf = codec_capture_get_buf(priv->cookie);
        }

      if (buf == NULL)
        {
          break;
        }

      bk7258_jpeg_m2m_fail_buffer(buf, type);
      ret = type == V4L2_BUF_TYPE_VIDEO_OUTPUT ?
            codec_output_put_buf(priv->cookie, buf) :
            codec_capture_put_buf(priv->cookie, buf);
      if (ret < 0)
        {
          verr("JPEG M2M buffer drain failed: %d\n", ret);
          break;
        }
    }
}

static int bk7258_jpeg_m2m_process(
  FAR struct bk7258_jpeg_m2m_file_s *priv,
  FAR struct v4l2_buffer *src,
  FAR struct v4l2_buffer *dst,
  FAR const struct v4l2_format *capture_fmt,
  FAR uint32_t *bytesused)
{
  struct bk7258_jpeg_decoder_frame_s input;
  struct bk7258_jpeg_decoder_frame_s output;
  struct bk7258_jpeg_decoder_info_s info;
  uint32_t payload;
  uint32_t required;
  size_t cache_line;
  int ret;

  if (!bk7258_jpeg_m2m_exact_output_type(src->type) ||
      !bk7258_jpeg_m2m_exact_capture_type(dst->type) ||
      src->memory != V4L2_MEMORY_USERPTR ||
      dst->memory != V4L2_MEMORY_USERPTR ||
      src->m.vaddr == NULL || dst->m.vaddr == NULL ||
      src->bytesused == 0 || src->length == 0 ||
      src->bytesused > src->length ||
      src->length > CONFIG_BK7258_JPEG_M2M_MAX_INPUT_SIZE ||
      src->bytesused > CONFIG_BK7258_JPEG_M2M_MAX_INPUT_SIZE)
    {
      return -EINVAL;
    }

  ret = bk7258_jpeg_m2m_capture_sizes(
    capture_fmt->fmt.pix.width, capture_fmt->fmt.pix.height,
    &payload, &required, &cache_line);
  if (ret < 0)
    {
      return ret;
    }

  if (required != capture_fmt->fmt.pix.sizeimage ||
      ((uintptr_t)dst->m.vaddr & (cache_line - 1)) != 0)
    {
      return -EINVAL;
    }

  if (required == 0 || dst->length < required)
    {
      return -ENOSPC;
    }

  input = (struct bk7258_jpeg_decoder_frame_s)
  {
    .data = src->m.vaddr,
    .capacity = src->length,
    .length = src->bytesused,
  };

  output = (struct bk7258_jpeg_decoder_frame_s)
  {
    .data = dst->m.vaddr,
    .capacity = dst->length,
  };

  ret = bk7258_jpeg_m2m_ensure_backend(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_decoder_get_info(priv->decoder, &input, &info);
  if (ret < 0)
    {
      return ret;
    }

  if (info.width != capture_fmt->fmt.pix.width ||
      info.height != capture_fmt->fmt.pix.height)
    {
      return -EINVAL;
    }

  ret = bk7258_jpeg_decoder_decode(priv->decoder, &input, &output);
  if (ret < 0)
    {
      return ret;
    }

  if (output.width != info.width || output.height != info.height ||
      output.length != payload)
    {
      return -EIO;
    }

  *bytesused = output.length;
  return 0;
}

static void bk7258_jpeg_m2m_worker(FAR void *arg)
{
  FAR struct bk7258_jpeg_m2m_file_s *priv = arg;
  FAR struct v4l2_buffer *src;
  FAR struct v4l2_buffer *dst;
  struct v4l2_format capture_fmt;
  uint32_t bytesused = 0;
  uint32_t epoch;
  bool stale;
  int resetret;
  int ret;

  ret = nxmutex_lock(&priv->state_lock);
  if (ret < 0)
    {
      return;
    }

  if (priv->closing || !priv->output_on || !priv->capture_on)
    {
      nxmutex_unlock(&priv->state_lock);
      return;
    }

  src = codec_output_get_buf(priv->cookie);
  dst = codec_capture_get_buf(priv->cookie);
  if (src == NULL || dst == NULL)
    {
      nxmutex_unlock(&priv->state_lock);
      return;
    }

  epoch = priv->epoch;
  capture_fmt = priv->capture_fmt;
  priv->inflight = true;
  nxmutex_unlock(&priv->state_lock);

  ret = bk7258_jpeg_m2m_process(priv, src, dst, &capture_fmt, &bytesused);
  if (bk7258_jpeg_m2m_fatal_error(ret))
    {
      resetret = bk7258_jpeg_m2m_reset_backend(priv);
      if (resetret < 0)
        {
          verr("JPEG M2M backend recovery failed: %d\n", resetret);
        }
    }

  if (nxmutex_lock(&priv->state_lock) < 0)
    {
      return;
    }

  stale = priv->closing || epoch != priv->epoch ||
          !priv->output_on || !priv->capture_on;

  src->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  dst->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  dst->field = V4L2_FIELD_NONE;
  src->flags &= ~V4L2_BUF_FLAG_LAST;
  dst->flags &= ~V4L2_BUF_FLAG_LAST;

  if (ret < 0 || stale)
    {
      src->flags |= V4L2_BUF_FLAG_ERROR;
      dst->flags |= V4L2_BUF_FLAG_ERROR;
      dst->bytesused = 0;
    }
  else
    {
      src->flags &= ~V4L2_BUF_FLAG_ERROR;
      dst->flags &= ~V4L2_BUF_FLAG_ERROR;
      dst->bytesused = bytesused;
      dst->timestamp = src->timestamp;
      dst->sequence = priv->sequence++;
    }

  ret = codec_output_put_buf(priv->cookie, src);
  if (ret < 0)
    {
      verr("JPEG M2M output completion failed: %d\n", ret);
    }

  ret = codec_capture_put_buf(priv->cookie, dst);
  if (ret < 0)
    {
      verr("JPEG M2M capture completion failed: %d\n", ret);
    }

  priv->inflight = false;
  ret = bk7258_jpeg_m2m_kick_locked(priv);
  if (ret < 0)
    {
      verr("JPEG M2M worker requeue failed: %d\n", ret);
    }

  nxmutex_unlock(&priv->state_lock);
}

static int bk7258_jpeg_m2m_open(FAR void *cookie, FAR void **out)
{
  FAR struct bk7258_jpeg_m2m_file_s *priv;
  uint32_t default_height;
  uint32_t default_width;
  int ret;

  if (cookie == NULL || out == NULL)
    {
      return -EINVAL;
    }

  *out = NULL;
  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  ret = nxmutex_init(&priv->life_lock);
  if (ret < 0)
    {
      kmm_free(priv);
      return ret;
    }

  ret = nxmutex_init(&priv->state_lock);
  if (ret < 0)
    {
      nxmutex_destroy(&priv->life_lock);
      kmm_free(priv);
      return ret;
    }

  priv->wqueue = work_queue_create(
    BK7258_JPEG_M2M_WORKER_NAME,
    CONFIG_BK7258_JPEG_M2M_WORKER_PRIORITY,
    NULL, CONFIG_BK7258_JPEG_M2M_WORKER_STACKSIZE, 1);
  if (priv->wqueue == NULL)
    {
      ret = -ENOMEM;
      goto fail;
    }

  ret = bk7258_jpeg_m2m_claim_backend(priv);
  if (ret < 0)
    {
      work_queue_free(priv->wqueue);
      priv->wqueue = NULL;
      goto fail;
    }

  priv->cookie = cookie;
  default_width = bk7258_jpeg_m2m_width(
    CONFIG_BK7258_JPEG_M2M_DEFAULT_WIDTH);
  default_height = bk7258_jpeg_m2m_height(
    CONFIG_BK7258_JPEG_M2M_DEFAULT_HEIGHT);
  bk7258_jpeg_m2m_make_output_format(
    &priv->output_fmt, default_width, default_height);
  ret = bk7258_jpeg_m2m_make_capture_format(
    &priv->capture_fmt, default_width, default_height);
  if (ret < 0)
    {
      (void)bk7258_jpeg_m2m_release_backend(priv);
      work_queue_free(priv->wqueue);
      priv->wqueue = NULL;
      goto fail;
    }

  *out = priv;
  return 0;

fail:
  nxmutex_destroy(&priv->state_lock);
  nxmutex_destroy(&priv->life_lock);
  kmm_free(priv);
  return ret;
}

static int bk7258_jpeg_m2m_close(FAR void *arg)
{
  FAR struct bk7258_jpeg_m2m_file_s *priv = arg;
  int ret;

  if (priv == NULL)
    {
      return 0;
    }

  ret = nxmutex_lock(&priv->life_lock);
  if (ret < 0)
    {
      return 0;
    }

  ret = nxmutex_lock(&priv->state_lock);
  if (ret >= 0)
    {
      priv->closing = true;
      priv->output_on = false;
      priv->capture_on = false;
      priv->epoch++;
      nxmutex_unlock(&priv->state_lock);
    }

  ret = work_cancel_sync_wq(priv->wqueue, &priv->work);
  if (ret < 0 && ret != -ENOENT)
    {
      verr("JPEG M2M close cancel failed: %d\n", ret);
    }

  ret = nxmutex_lock(&priv->state_lock);
  if (ret >= 0)
    {
      bk7258_jpeg_m2m_drain_locked(
        priv, V4L2_BUF_TYPE_VIDEO_OUTPUT);
      bk7258_jpeg_m2m_drain_locked(
        priv, V4L2_BUF_TYPE_VIDEO_CAPTURE);
      nxmutex_unlock(&priv->state_lock);
    }

  ret = work_queue_free(priv->wqueue);
  if (ret < 0)
    {
      verr("JPEG M2M worker join failed: %d\n", ret);
    }

  priv->wqueue = NULL;
  ret = bk7258_jpeg_m2m_release_backend(priv);
  if (ret < 0)
    {
      verr("JPEG M2M backend close retained orphan: %d\n", ret);
    }

  nxmutex_unlock(&priv->life_lock);
  nxmutex_destroy(&priv->state_lock);
  nxmutex_destroy(&priv->life_lock);
  kmm_free(priv);

  /* The generic upper half ignores this return value and frees its queue
   * cookie immediately.  All work and backend callbacks are therefore
   * synchronously quiesced above, and close always reports completion. */

  return 0;
}

static int bk7258_jpeg_m2m_streamon(
  FAR struct bk7258_jpeg_m2m_file_s *priv, bool output)
{
  int ret;

  ret = nxmutex_lock(&priv->life_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_jpeg_m2m_ensure_backend(priv);
  if (ret < 0)
    {
      goto out_unlock_life;
    }

  ret = nxmutex_lock(&priv->state_lock);
  if (ret < 0)
    {
      goto out_unlock_life;
    }

  if (priv->closing)
    {
      ret = -ESHUTDOWN;
    }
  else
    {
      if (priv->output_fmt.fmt.pix.width !=
          priv->capture_fmt.fmt.pix.width ||
          priv->output_fmt.fmt.pix.height !=
          priv->capture_fmt.fmt.pix.height)
        {
          ret = -EINVAL;
        }
      else if (output && !priv->output_on)
        {
          priv->output_on = true;
          priv->epoch++;
        }
      else if (!output && !priv->capture_on)
        {
          priv->capture_on = true;
          priv->epoch++;
        }

      if (ret >= 0)
        {
          ret = bk7258_jpeg_m2m_kick_locked(priv);
        }
    }

  nxmutex_unlock(&priv->state_lock);

out_unlock_life:
  nxmutex_unlock(&priv->life_lock);
  return ret;
}

static int bk7258_jpeg_m2m_capture_streamon(FAR void *arg)
{
  return arg == NULL ? -EINVAL :
         bk7258_jpeg_m2m_streamon(arg, false);
}

static int bk7258_jpeg_m2m_output_streamon(FAR void *arg)
{
  return arg == NULL ? -EINVAL :
         bk7258_jpeg_m2m_streamon(arg, true);
}

static int bk7258_jpeg_m2m_streamoff(
  FAR struct bk7258_jpeg_m2m_file_s *priv, bool output)
{
  uint32_t type = output ? V4L2_BUF_TYPE_VIDEO_OUTPUT :
                           V4L2_BUF_TYPE_VIDEO_CAPTURE;
  int ret;

  ret = nxmutex_lock(&priv->life_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&priv->state_lock);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->life_lock);
      return ret;
    }

  if (output)
    {
      priv->output_on = false;
    }
  else
    {
      priv->capture_on = false;
    }

  priv->epoch++;
  nxmutex_unlock(&priv->state_lock);

  ret = work_cancel_sync_wq(priv->wqueue, &priv->work);
  if (ret < 0 && ret != -ENOENT)
    {
      verr("JPEG M2M streamoff cancel failed: %d\n", ret);
    }

  ret = nxmutex_lock(&priv->state_lock);
  if (ret >= 0)
    {
      bk7258_jpeg_m2m_drain_locked(priv, type);
      nxmutex_unlock(&priv->state_lock);
    }

  nxmutex_unlock(&priv->life_lock);
  return 0;
}

static int bk7258_jpeg_m2m_capture_streamoff(FAR void *arg)
{
  return arg == NULL ? -EINVAL :
         bk7258_jpeg_m2m_streamoff(arg, false);
}

static int bk7258_jpeg_m2m_output_streamoff(FAR void *arg)
{
  return arg == NULL ? -EINVAL :
         bk7258_jpeg_m2m_streamoff(arg, true);
}

static int bk7258_jpeg_m2m_available(FAR void *arg)
{
  FAR struct bk7258_jpeg_m2m_file_s *priv = arg;
  int ret;

  if (priv == NULL)
    {
      return 0;
    }

  ret = nxmutex_lock(&priv->state_lock);
  if (ret >= 0)
    {
      ret = bk7258_jpeg_m2m_kick_locked(priv);
      if (ret < 0)
        {
          verr("JPEG M2M queue kick failed: %d\n", ret);
        }

      nxmutex_unlock(&priv->state_lock);
    }

  /* codec_qbuf() has already put the container into the generic queue before
   * this callback runs.  Propagating a scheduling error would report QBUF as
   * failed while leaving that buffer owned by the driver. */

  return 0;
}

static int bk7258_jpeg_m2m_querycap(
  FAR void *arg, FAR struct v4l2_capability *cap)
{
  if (arg == NULL || cap == NULL)
    {
      return -EINVAL;
    }

  memset(cap, 0, sizeof(*cap));
  strlcpy((FAR char *)cap->driver, BK7258_JPEG_M2M_NAME,
          sizeof(cap->driver));
  strlcpy((FAR char *)cap->card, BK7258_JPEG_M2M_NAME,
          sizeof(cap->card));
  cap->capabilities = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
  cap->device_caps = cap->capabilities;
  return 0;
}

static int bk7258_jpeg_m2m_capture_enum_fmt(
  FAR void *arg, FAR struct v4l2_fmtdesc *fmt)
{
  if (arg == NULL || fmt == NULL ||
      !bk7258_jpeg_m2m_exact_capture_type(fmt->type) || fmt->index != 0)
    {
      return -EINVAL;
    }

  fmt->pixelformat = V4L2_PIX_FMT_YUYV;
  fmt->flags = 0;
  strlcpy((FAR char *)fmt->description, "YUYV 4:2:2",
          sizeof(fmt->description));
  return 0;
}

static int bk7258_jpeg_m2m_output_enum_fmt(
  FAR void *arg, FAR struct v4l2_fmtdesc *fmt)
{
  if (arg == NULL || fmt == NULL ||
      !bk7258_jpeg_m2m_exact_output_type(fmt->type) || fmt->index != 0)
    {
      return -EINVAL;
    }

  fmt->pixelformat = V4L2_PIX_FMT_JPEG;
  fmt->flags = V4L2_FMT_FLAG_COMPRESSED;
  strlcpy((FAR char *)fmt->description, "JPEG", sizeof(fmt->description));
  return 0;
}

static int bk7258_jpeg_m2m_capture_try_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt)
{
  uint32_t width;
  uint32_t height;

  if (arg == NULL || fmt == NULL ||
      !bk7258_jpeg_m2m_exact_capture_type(fmt->type) ||
      fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)
    {
      return -EINVAL;
    }

  width = bk7258_jpeg_m2m_width(fmt->fmt.pix.width);
  height = bk7258_jpeg_m2m_height(fmt->fmt.pix.height);
  return bk7258_jpeg_m2m_make_capture_format(fmt, width, height);
}

static int bk7258_jpeg_m2m_output_try_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt)
{
  uint32_t width;
  uint32_t height;

  if (arg == NULL || fmt == NULL ||
      !bk7258_jpeg_m2m_exact_output_type(fmt->type) ||
      fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG)
    {
      return -EINVAL;
    }

  width = bk7258_jpeg_m2m_width(fmt->fmt.pix.width);
  height = bk7258_jpeg_m2m_height(fmt->fmt.pix.height);
  bk7258_jpeg_m2m_make_output_format(fmt, width, height);
  return 0;
}

static int bk7258_jpeg_m2m_g_fmt(
  FAR struct bk7258_jpeg_m2m_file_s *priv,
  FAR struct v4l2_format *fmt, bool output)
{
  int ret;

  ret = nxmutex_lock(&priv->state_lock);
  if (ret < 0)
    {
      return ret;
    }

  *fmt = output ? priv->output_fmt : priv->capture_fmt;
  nxmutex_unlock(&priv->state_lock);
  return 0;
}

static int bk7258_jpeg_m2m_capture_g_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt)
{
  if (arg == NULL || fmt == NULL ||
      !bk7258_jpeg_m2m_exact_capture_type(fmt->type))
    {
      return -EINVAL;
    }

  return bk7258_jpeg_m2m_g_fmt(arg, fmt, false);
}

static int bk7258_jpeg_m2m_output_g_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt)
{
  if (arg == NULL || fmt == NULL ||
      !bk7258_jpeg_m2m_exact_output_type(fmt->type))
    {
      return -EINVAL;
    }

  return bk7258_jpeg_m2m_g_fmt(arg, fmt, true);
}

static int bk7258_jpeg_m2m_s_fmt(
  FAR struct bk7258_jpeg_m2m_file_s *priv,
  FAR struct v4l2_format *fmt, bool output)
{
  struct v4l2_format normalized = *fmt;
  uint32_t width;
  uint32_t height;
  int ret;

  ret = output ? bk7258_jpeg_m2m_output_try_fmt(priv, &normalized) :
                 bk7258_jpeg_m2m_capture_try_fmt(priv, &normalized);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&priv->life_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&priv->state_lock);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->life_lock);
      return ret;
    }

  if (priv->closing)
    {
      ret = -ESHUTDOWN;
    }
  else if (priv->formats_locked || priv->output_on || priv->capture_on ||
           priv->inflight)
    {
      ret = -EBUSY;
    }
  else
    {
      width = normalized.fmt.pix.width;
      height = normalized.fmt.pix.height;
      if (output)
        {
          bk7258_jpeg_m2m_make_output_format(&priv->output_fmt,
                                              width, height);
          *fmt = priv->output_fmt;
        }
      else
        {
          ret = bk7258_jpeg_m2m_make_capture_format(&priv->capture_fmt,
                                                     width, height);
          if (ret >= 0)
            {
              *fmt = priv->capture_fmt;
            }
        }

      if (output)
        {
          ret = 0;
        }
    }

  nxmutex_unlock(&priv->state_lock);
  nxmutex_unlock(&priv->life_lock);
  return ret;
}

static int bk7258_jpeg_m2m_capture_s_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt)
{
  return arg == NULL || fmt == NULL ? -EINVAL :
         bk7258_jpeg_m2m_s_fmt(arg, fmt, false);
}

static int bk7258_jpeg_m2m_output_s_fmt(
  FAR void *arg, FAR struct v4l2_format *fmt)
{
  return arg == NULL || fmt == NULL ? -EINVAL :
         bk7258_jpeg_m2m_s_fmt(arg, fmt, true);
}

static size_t bk7258_jpeg_m2m_g_bufsize(FAR void *arg, bool output)
{
  FAR struct bk7258_jpeg_m2m_file_s *priv = arg;
  size_t size = 0;

  if (priv == NULL || nxmutex_lock(&priv->state_lock) < 0)
    {
      return 0;
    }

  /* The generic M2M upper half does not expose REQBUFS state to the lower
   * half.  Its first size query is the only safe point to freeze the coupled
   * formats, preventing later S_FMT from invalidating allocated containers.
   * Returning zero while active also rejects REQBUFS reallocation of a
   * container currently referenced by the worker. */

  if (!priv->closing && !priv->output_on && !priv->capture_on &&
      !priv->inflight &&
      priv->output_fmt.fmt.pix.width ==
        priv->capture_fmt.fmt.pix.width &&
      priv->output_fmt.fmt.pix.height ==
        priv->capture_fmt.fmt.pix.height)
    {
      priv->formats_locked = true;
      size = output ? priv->output_fmt.fmt.pix.sizeimage :
                      priv->capture_fmt.fmt.pix.sizeimage;
    }

  nxmutex_unlock(&priv->state_lock);
  return size;
}

static size_t bk7258_jpeg_m2m_capture_g_bufsize(FAR void *arg)
{
  return bk7258_jpeg_m2m_g_bufsize(arg, false);
}

static size_t bk7258_jpeg_m2m_output_g_bufsize(FAR void *arg)
{
  return bk7258_jpeg_m2m_g_bufsize(arg, true);
}

static size_t bk7258_jpeg_m2m_g_bufcnt(FAR void *arg)
{
  return arg == NULL ? 0 : BK7258_JPEG_M2M_BUFFER_COUNT;
}

static int bk7258_jpeg_m2m_try_memory(
  FAR void *arg, enum v4l2_memory memory)
{
  if (arg == NULL)
    {
      return -EINVAL;
    }

  return memory == V4L2_MEMORY_USERPTR ? 0 : -ENOTSUP;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_jpeg_m2m_register(FAR const char *devpath)
{
  int ret;

  if (devpath == NULL || devpath[0] == '\0')
    {
      return -EINVAL;
    }

  ret = bk7258_jpeg_m2m_check_config();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_bk7258_jpeg_m2m_device_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_jpeg_m2m_registered)
    {
      nxmutex_unlock(&g_bk7258_jpeg_m2m_device_lock);
      return -EALREADY;
    }

  ret = codec_register(devpath, &g_bk7258_jpeg_m2m_codec);
  if (ret >= 0)
    {
      g_bk7258_jpeg_m2m_registered = true;
    }

  nxmutex_unlock(&g_bk7258_jpeg_m2m_device_lock);
  return ret;
}
