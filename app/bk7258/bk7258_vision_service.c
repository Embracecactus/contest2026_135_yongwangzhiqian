/****************************************************************************
 * app/bk7258/bk7258_vision_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-owned V4L2 snapshot and bounded local MJPEG recording service.
 * Only command results cross RPMsg; pixels remain on AP-owned storage.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_VISION_SERVICE

#include "bk7258_vision_core.h"
#include "bk7258_vision_protocol.h"
#include "bk7258_vision_service.h"
#include "bk7258_vision_record.h"
#include "bk7258_media_volume.h"

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
#include "bk7258_display_service.h"
#include "bk7258_vision_feedback.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/videoio.h>
#include <unistd.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>


#define BKVISION_MOUNTPOINT "/mnt/sdnand"
#define BKVISION_RECORD_ROOT BKVISION_MOUNTPOINT "/recordings"
#define BKVISION_RECORD_BUFFERS CONFIG_BK7258_VISION_RECORD_BUFFERS
#define BKVISION_RECORD_WRITE_BUFFER_SIZE (16u * 1024u)

/* Retain ownership across a failed unmount; retry before the next record. */
static bool g_record_mounted;
static bool g_record_leased;

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
static int bkvision_feedback_set_expression(void *arg,
                                            const char *expression)
{
  (void)arg;
  return bk7258_display_set_expression(expression);
}

static int bkvision_feedback_replace_expression(void *arg,
                                                const char *expected,
                                                const char *replacement)
{
  (void)arg;
  return bk7258_display_replace_expression(expected, replacement);
}

static void bkvision_feedback_wait(void *arg, unsigned int milliseconds)
{
  (void)arg;
  nxsig_usleep(milliseconds * 1000u);
}

static const struct bkvision_feedback_ops_s g_bkvision_feedback_ops =
{
  .set_expression = bkvision_feedback_set_expression,
  .replace_expression = bkvision_feedback_replace_expression,
  .wait_ms = bkvision_feedback_wait,
};
#endif

struct bkvision_server_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  spinlock_t request_lock;
  sem_t request_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  bool active;
  bool replay_valid;
  struct bkvision_rpc_request_s active_request;
  struct bkvision_rpc_request_s last_request;
  struct bkvision_rpc_response_s last_response;
};

static struct bkvision_server_s g_bkvision_server =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = SP_UNLOCKED,
};

/* The V4L2 node accepts one capture or recording operation at a time. */
static mutex_t g_bkvision_capture_lock = NXMUTEX_INITIALIZER;

static int bkvision_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static int bkvision_ioctl(int fd, int request, FAR void *argument)
{
  int ret = ioctl(fd, request, (unsigned long)(uintptr_t)argument);

  return ret < 0 ? bkvision_errno() : 0;
}

static void bkvision_operation_failed(
  FAR struct bkvision_rpc_response_s *response, int status)
{
  response->operation_status = status < 0 ? status : -EIO;
  response->width = 0;
  response->height = 0;
  response->pixel_format = 0;
  response->bytes_used = 0;
  response->capture_sequence = 0;
  response->flags = 0;
  response->reserved[0] = 0;
  response->reserved[1] = 0;
}

static int bkvision_wait_frame(int fd, FAR struct v4l2_buffer *buffer)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(CONFIG_BK7258_VISION_CAPTURE_TIMEOUT_MS);
  int ret;

  for (;;)
    {
      ret = bkvision_ioctl(fd, VIDIOC_DQBUF, buffer);
      if (ret >= 0)
        {
          return 0;
        }

      if (ret != -EAGAIN && ret != -EINTR)
        {
          return ret;
        }

      if ((clock_t)(clock_systime_ticks() - start) >= limit)
        {
          return -ETIMEDOUT;
        }

      nxsig_usleep(5000);
    }
}

static int bkvision_volume_close(void)
{
  if (g_record_mounted)
    {
      if (umount(BKVISION_MOUNTPOINT) < 0)
        {
          return bkvision_errno();
        }
      g_record_mounted = false;
    }
  if (g_record_leased)
    {
      int ret = bk7258_media_volume_release(BK7258_MEDIA_VOLUME_VISION);
      if (ret < 0)
        {
          return ret;
        }
      g_record_leased = false;
    }
  return 0;
}

static int bkvision_volume_open(bool readonly)
{
  int ret = bkvision_volume_close();
  if (ret < 0)
    {
      return ret;
    }
  ret = bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION);
  if (ret < 0)
    {
      return ret;
    }
  g_record_leased = true;
  if ((mkdir("/mnt", 0777) < 0 && errno != EEXIST) ||
      (mkdir(BKVISION_MOUNTPOINT, 0777) < 0 && errno != EEXIST))
    {
      ret = bkvision_errno();
      goto fail;
    }
  /* Use the same mountpoint as the display service. An existing mount is
   * busy, never adopted or unmounted by this service. Never format media.
   */
  if (mount("/dev/mmcsd0", BKVISION_MOUNTPOINT, "vfat",
            readonly ? MS_RDONLY : 0, NULL) < 0)
    {
      ret = bkvision_errno();
      goto fail;
    }
  g_record_mounted = true;
  if (!readonly && mkdir(BKVISION_RECORD_ROOT, 0777) < 0 && errno != EEXIST)
    {
      ret = bkvision_errno();
      goto fail;
    }
  return 0;
fail:
  (void)bkvision_volume_close();
  return ret;
}

static int bkvision_check_record(
  FAR const struct bkvision_rpc_request_s *request,
  FAR struct bkvision_rpc_response_s *response)
{
  struct bkvision_record_s record;
  struct stat info;
  char path[96];
  int fd = -1;
  int ret = bkvision_volume_open(true);

  if (ret >= 0)
    {
      snprintf(path, sizeof(path), BKVISION_RECORD_ROOT "/"
               BKVISION_RECORD_NAME_FORMAT,
               (unsigned long)request->duration_ms,
               (unsigned long)request->reserved);
      fd = open(path, O_RDONLY);
      ret = fd < 0 ? bkvision_errno() : fstat(fd, &info) < 0 ?
            bkvision_errno() : !S_ISREG(info.st_mode) ? -EINVAL : 0;
      if (ret >= 0)
        {
          ret = bkvision_record_inspect(&record, fd);
        }
      if (fd >= 0 && close(fd) < 0 && ret >= 0)
        {
          ret = bkvision_errno();
        }
      int cleanup = bkvision_volume_close();
      if (ret >= 0 && cleanup < 0)
        {
          ret = cleanup;
        }
    }
  if (ret < 0)
    {
      bkvision_operation_failed(response, ret);
    }
  else
    {
      response->operation_status = 0;
      response->flags = BKVISION_FLAG_CHECKED;
      response->width = record.width;
      response->height = record.height;
      response->pixel_format = BKVISION_PIXEL_FORMAT_JPEG;
      response->bytes_used = record.bytes + 224;
      response->reserved[0] = record.frames;
      response->reserved[1] = record.payload_hash;
    }
  return ret;
}

static int bkvision_set_frame_interval(int fd)
{
  struct v4l2_streamparm parm;
  int ret;

  memset(&parm, 0, sizeof(parm));
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = CONFIG_BK7258_VISION_FPS;
  ret = bkvision_ioctl(fd, VIDIOC_S_PARM, &parm);
  if (ret < 0)
    {
      return ret;
    }

  memset(&parm.parm, 0, sizeof(parm.parm));
  ret = bkvision_ioctl(fd, VIDIOC_G_PARM, &parm);
  if (ret >= 0 &&
      ((parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) == 0 ||
       parm.parm.capture.timeperframe.numerator == 0 ||
       (uint64_t)parm.parm.capture.timeperframe.numerator *
         CONFIG_BK7258_VISION_FPS !=
         parm.parm.capture.timeperframe.denominator))
    {
      ret = -EPROTO;
    }

  return ret;
}

static int bkvision_capture(
  FAR const struct bkvision_rpc_request_s *request,
  FAR struct bkvision_rpc_response_s *response,
  FAR uint8_t *destination, size_t destination_capacity,
  FAR size_t *destination_size)
{
  struct v4l2_requestbuffers request_buffers;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  struct v4l2_format format;
  struct v4l2_buffer buffer;
  FAR uint8_t *mapping[BKVISION_RECORD_BUFFERS];
  size_t mapping_length[BKVISION_RECORD_BUFFERS] = {0};
  struct bkvision_record_s record;
  FAR uint8_t *write_buffer = NULL;
  char path[96] = {0};
  bool recording = request->command == BKVISION_RPC_RECORD;
  bool volume = false;
  bool created = false;
  unsigned int count = recording ? BKVISION_RECORD_BUFFERS : 1;
  unsigned int i;
  clock_t started = 0;
  uint32_t elapsed_ms = 0;
  int output = -1;
  bool streaming = false;
  int fd = -1;
  int result;
  int cleanup;

  if (destination_size != NULL)
    {
      *destination_size = 0;
    }

  if ((destination == NULL) != (destination_size == NULL))
    {
      bkvision_operation_failed(response, -EINVAL);
      return -EINVAL;
    }

  result = nxmutex_trylock(&g_bkvision_capture_lock);
  if (result < 0)
    {
      bkvision_operation_failed(response, result == -EBUSY ? -EBUSY : result);
      return result == -EBUSY ? -EBUSY : result;
    }

  for (i = 0; i < count; i++)
    {
      mapping[i] = MAP_FAILED;
    }

  if (recording)
    {
      result = bkvision_volume_open(false);
      if (result < 0)
        {
          goto out;
        }
      volume = true;
      snprintf(path, sizeof(path), BKVISION_RECORD_ROOT "/"
               BKVISION_RECORD_NAME_FORMAT,
               (unsigned long)request->session_id,
               (unsigned long)request->sequence);
      output = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
      if (output < 0)
        {
          result = bkvision_errno();
          goto out;
        }
      created = true;
    }

  fd = open(CONFIG_BK7258_VISION_DEVPATH, O_RDWR | O_NONBLOCK);
  if (fd < 0)
    {
      result = bkvision_errno();
      goto out;
    }

  memset(&format, 0, sizeof(format));
  format.type = type;
  format.fmt.pix.width = CONFIG_BK7258_VISION_WIDTH;
  format.fmt.pix.height = CONFIG_BK7258_VISION_HEIGHT;
  format.fmt.pix.field = V4L2_FIELD_ANY;
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
  format.fmt.pix.sizeimage = CONFIG_BK7258_VISION_JPEG_BUFFER_BYTES;
  result = bkvision_ioctl(fd, VIDIOC_S_FMT, &format);
  if (result < 0)
    {
      goto out;
    }

  memset(&format, 0, sizeof(format));
  format.type = type;
  result = bkvision_ioctl(fd, VIDIOC_G_FMT, &format);
  if (result < 0)
    {
      goto out;
    }

  if (format.fmt.pix.width == 0 || format.fmt.pix.height == 0 ||
      format.fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG ||
      format.fmt.pix.sizeimage < 4 ||
      format.fmt.pix.sizeimage > CONFIG_BK7258_VISION_JPEG_BUFFER_BYTES)
    {
      result = -EPROTO;
      goto out;
    }

  result = bkvision_set_frame_interval(fd);
  if (result < 0)
    {
      goto out;
    }

  memset(&request_buffers, 0, sizeof(request_buffers));
  request_buffers.type = type;
  request_buffers.memory = V4L2_MEMORY_MMAP;
  request_buffers.mode = V4L2_BUF_MODE_FIFO;
  request_buffers.count = count;
  result = bkvision_ioctl(fd, VIDIOC_REQBUFS, &request_buffers);
  if (result < 0)
    {
      goto out;
    }

  if (request_buffers.count != count)
    {
      result = -ENOMEM;
      goto out;
    }

  for (i = 0; i < count; i++)
    {
      memset(&buffer, 0, sizeof(buffer));
      buffer.type = type;
      buffer.memory = V4L2_MEMORY_MMAP;
      buffer.index = i;
      result = bkvision_ioctl(fd, VIDIOC_QUERYBUF, &buffer);
      if (result < 0)
        {
          goto out;
        }
      mapping_length[i] = buffer.length;
      if (mapping_length[i] < format.fmt.pix.sizeimage)
        {
          result = -EOVERFLOW;
          goto out;
        }
      mapping[i] = mmap(NULL, mapping_length[i], PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, buffer.m.offset);
      if (mapping[i] == MAP_FAILED)
        {
          result = bkvision_errno();
          goto out;
        }
      result = bkvision_ioctl(fd, VIDIOC_QBUF, &buffer);
      if (result < 0)
        {
          goto out;
        }
    }

  if (recording)
    {
      result = bkvision_record_begin(&record, output, format.fmt.pix.width,
                                     format.fmt.pix.height);
      if (result >= 0)
        {
          write_buffer = malloc(BKVISION_RECORD_WRITE_BUFFER_SIZE);
          result = write_buffer == NULL ? -ENOMEM :
            bkvision_record_set_buffer(&record, write_buffer,
                                        BKVISION_RECORD_WRITE_BUFFER_SIZE);
        }
      if (result < 0)
        {
          goto out;
        }
    }
  result = bkvision_ioctl(fd, VIDIOC_STREAMON, &type);
  if (result < 0)
    {
      goto out;
    }

  streaming = true;
  started = clock_systime_ticks();
  do
    {
      memset(&buffer, 0, sizeof(buffer));
      buffer.type = type;
      buffer.memory = V4L2_MEMORY_MMAP;
      result = bkvision_wait_frame(fd, &buffer);
      if (result < 0)
        {
          goto out;
        }
      if (buffer.index >= count)
        {
          result = -EPROTO;
          goto out;
        }
      if (destination != NULL)
        {
          result = bkvision_copy_jpeg_frame(
            response, destination, destination_capacity, destination_size,
            mapping[buffer.index], mapping_length[buffer.index],
            buffer.bytesused, buffer.flags & V4L2_BUF_FLAG_ERROR,
            format.fmt.pix.width, format.fmt.pix.height,
            format.fmt.pix.pixelformat, buffer.sequence);
        }
      else
        {
          result = bkvision_rpc_validate_frame(
            response, mapping[buffer.index], mapping_length[buffer.index],
            buffer.bytesused, buffer.flags & V4L2_BUF_FLAG_ERROR,
            format.fmt.pix.width, format.fmt.pix.height,
            format.fmt.pix.pixelformat, buffer.sequence);
        }
      if (result < 0 || !recording)
        {
          break;
        }
      if (recording)
        {
          result = bkvision_record_frame(&record, mapping[buffer.index],
                                         buffer.bytesused);
          if (result < 0)
            {
              goto out;
            }
        }
      /* The writer has copied the JPEG before it returns. Its own staging
       * buffer remains valid across QBUF and is flushed before final fsync.
       */
      buffer.bytesused = 0;
      result = bkvision_ioctl(fd, VIDIOC_QBUF, &buffer);
      if (result < 0)
        {
          goto out;
        }
      elapsed_ms = TICK2MSEC(clock_systime_ticks() - started);
    }
  while (elapsed_ms < request->duration_ms);

out:
  if (streaming)
    {
      cleanup = bkvision_ioctl(fd, VIDIOC_STREAMOFF, &type);
      if (result >= 0 && cleanup < 0)
        {
          result = cleanup;
        }
    }

  for (i = 0; i < count; i++)
    {
      if (mapping[i] != MAP_FAILED)
        {
          /* Flat-address-space MMAP is freed by sole-owner final close.
           * NuttX legitimately returns EINVAL from munmap for this driver.
           */
          (void)munmap(mapping[i], mapping_length[i]);
        }
    }

  if (fd >= 0)
    {
      cleanup = close(fd);
      if (cleanup < 0)
        {
          cleanup = bkvision_errno();
        }

      if (result >= 0 && cleanup < 0)
        {
          result = cleanup;
        }
    }

  if (output >= 0)
    {
      if (result >= 0)
        {
          result = bkvision_record_finish(&record, elapsed_ms);
        }

      if (output >= 0 && close(output) < 0 && result >= 0)
        {
          result = bkvision_errno();
        }
      if (result >= 0)
        {
          response->flags |= BKVISION_FLAG_RECORDED;
          response->reserved[0] = record.frames;
          response->reserved[1] = elapsed_ms;
          response->bytes_used = record.bytes + 224;
        }
    }
  free(write_buffer);
  if (result < 0 && created)
    {
      (void)unlink(path);
    }
  if (volume)
    {
      cleanup = bkvision_volume_close();
      if (result >= 0 && cleanup < 0)
        {
          result = cleanup;
        }
    }

  if (result < 0)
    {
      if (destination != NULL && destination_size != NULL &&
          *destination_size != 0)
        {
          memset(destination, 0, *destination_size);
          *destination_size = 0;
        }
      bkvision_operation_failed(response, result);
    }

  nxmutex_unlock(&g_bkvision_capture_lock);
  return result;
}

int bk7258_vision_capture_jpeg(uint8_t *destination,
                                size_t destination_capacity,
                                size_t *destination_size)
{
  struct bkvision_rpc_request_s request;
  struct bkvision_rpc_response_s response;

  if (destination_size != NULL)
    {
      *destination_size = 0;
    }

  if (destination == NULL || destination_size == NULL ||
      destination_capacity == 0)
    {
      return -EINVAL;
    }

  memset(&request, 0, sizeof(request));
  memset(&response, 0, sizeof(response));
  request.command = BKVISION_RPC_SNAPSHOT;
  /* The voice session owns its thinking/speaking/error presentation.  A
   * nested snapshot must not render a second sequence or hold the cloud
   * worker while a happy expression is displayed.  Standalone snapshot
   * commands keep their feedback in bkvision_worker(). */
  return bkvision_capture(&request, &response, destination,
                           destination_capacity, destination_size);
}

static int bkvision_send(
  FAR struct bkvision_server_s *server,
  FAR const struct bkvision_rpc_response_s *response)
{
  int ret = nxmutex_lock(&server->endpoint_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (!__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE) ||
      !is_rpmsg_ept_ready(&server->endpoint))
    {
      ret = -ENOTCONN;
    }
  else
    {
      ret = rpmsg_trysend(&server->endpoint, response, sizeof(*response));
    }

  nxmutex_unlock(&server->endpoint_lock);
  return ret;
}

static int bkvision_worker(int argc, FAR char **argv)
{
  FAR struct bkvision_server_s *server = &g_bkvision_server;

  (void)argc;
  (void)argv;
  for (;;)
    {
      struct bkvision_rpc_request_s request;
      struct bkvision_rpc_response_s response;
      irqstate_t flags;

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
      struct bkvision_feedback_s feedback;
      bool snapshot;
#endif

      if (nxsem_wait_uninterruptible(&server->request_sem) < 0)
        {
          continue;
        }

      flags = spin_lock_irqsave(&server->request_lock);
      memcpy(&request, &server->active_request, sizeof(request));
      spin_unlock_irqrestore(&server->request_lock, flags);

      bkvision_rpc_make_response(&response, &request, 0);
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
      snapshot = request.command == BKVISION_RPC_SNAPSHOT;
      if (snapshot)
        {
          bkvision_feedback_initialize(&feedback,
                                       &g_bkvision_feedback_ops);
          bkvision_feedback_snapshot_begin(&feedback);
        }
#endif
      if (request.command == BKVISION_RPC_CHECK_RECORD)
        {
          (void)bkvision_check_record(&request, &response);
        }
      else
        {
          (void)bkvision_capture(&request, &response, NULL, 0, NULL);
        }

      flags = spin_lock_irqsave(&server->request_lock);
      memcpy(&server->last_request, &request, sizeof(request));
      memcpy(&server->last_response, &response, sizeof(response));
      server->replay_valid = true;
      server->active = false;
      spin_unlock_irqrestore(&server->request_lock, flags);

      (void)bkvision_send(server, &response);
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
      if (snapshot)
        {
          bkvision_feedback_snapshot_finish(
            &feedback, response.operation_status == 0);
        }
#endif
    }

  return 0;
}

static int bkvision_server_cb(FAR struct rpmsg_endpoint *endpoint,
                              FAR void *data, size_t len, uint32_t src,
                              FAR void *priv)
{
  FAR struct bkvision_server_s *server = priv;
  FAR const struct bkvision_rpc_request_s *request = data;
  struct bkvision_rpc_response_s response;
  irqstate_t flags;
  bool replay = false;
  bool duplicate;
  int ret;

  (void)endpoint;
  (void)src;
  if (request == NULL || len != sizeof(*request) ||
      request->magic != BKVISION_RPC_MAGIC ||
      request->version != BKVISION_RPC_VERSION)
    {
      return -EINVAL;
    }

  if (!bkvision_rpc_request_valid(request))
    {
      bkvision_rpc_make_response(&response, request, -EINVAL);
      return bkvision_send(server, &response);
    }

  flags = spin_lock_irqsave(&server->request_lock);
  if (server->replay_valid &&
      request->session_id == server->last_request.session_id &&
      request->sequence == server->last_request.sequence)
    {
      if (memcmp(request, &server->last_request, sizeof(*request)) != 0)
        {
          spin_unlock_irqrestore(&server->request_lock, flags);
          bkvision_rpc_make_response(&response, request, -EPROTO);
          return bkvision_send(server, &response);
        }

      memcpy(&response, &server->last_response, sizeof(response));
      replay = true;
    }
  else if (server->active)
    {
      duplicate = memcmp(request, &server->active_request,
                         sizeof(*request)) == 0;
      spin_unlock_irqrestore(&server->request_lock, flags);
      if (duplicate)
        {
          return 0;
        }

      bkvision_rpc_make_response(&response, request, -EBUSY);
      return bkvision_send(server, &response);
    }
  else
    {
      memcpy(&server->active_request, request, sizeof(*request));
      server->active = true;
    }

  spin_unlock_irqrestore(&server->request_lock, flags);
  if (replay)
    {
      return bkvision_send(server, &response);
    }

  ret = nxsem_post(&server->request_sem);
  if (ret >= 0)
    {
      return 0;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  server->active = false;
  spin_unlock_irqrestore(&server->request_lock, flags);
  bkvision_rpc_make_response(&response, request, ret);
  return bkvision_send(server, &response);
}

static bool bkvision_ns_match(FAR struct rpmsg_device *rdev, FAR void *priv,
                              FAR const char *name, uint32_t dest)
{
  FAR const char *cpu = rpmsg_get_cpuname(rdev);

  (void)priv;
  (void)dest;
  return cpu != NULL && strcmp(cpu, "cp") == 0 &&
         strcmp(name, BKVISION_RPC_ENDPOINT) == 0;
}

static void bkvision_ns_bind(FAR struct rpmsg_device *rdev, FAR void *priv,
                             FAR const char *name, uint32_t dest)
{
  FAR struct bkvision_server_s *server = priv;

  if (nxmutex_lock(&server->endpoint_lock) < 0)
    {
      return;
    }

  if (!__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
    {
      server->endpoint.priv = server;
      if (rpmsg_create_ept(&server->endpoint, rdev, name, RPMSG_ADDR_ANY,
                           dest, bkvision_server_cb, NULL) >= 0)
        {
          __atomic_store_n(&server->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&server->endpoint_lock);
}

static void bkvision_device_destroy(FAR struct rpmsg_device *rdev,
                                    FAR void *priv)
{
  FAR struct bkvision_server_s *server = priv;
  FAR const char *cpu = rpmsg_get_cpuname(rdev);

  if (cpu == NULL || strcmp(cpu, "cp") != 0)
    {
      return;
    }

  __atomic_store_n(&server->endpoint_created, false, __ATOMIC_RELEASE);
  if (nxmutex_lock(&server->endpoint_lock) >= 0)
    {
      if (server->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&server->endpoint);
        }

      memset(&server->endpoint, 0, sizeof(server->endpoint));
      nxmutex_unlock(&server->endpoint_lock);
    }
}

int bk7258_vision_service_prepare(void)
{
  return 0;
}

int bk7258_vision_service_start(void)
{
  FAR struct bkvision_server_s *server = &g_bkvision_server;
  bool semaphore_initialized = false;
  bool callback_registered = false;
  pid_t pid;
  int ret;

  ret = nxmutex_lock(&server->init_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&server->initialized, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&server->init_lock);
      return 0;
    }

  ret = nxsem_init(&server->request_sem, 0, 0);
  if (ret >= 0)
    {
      semaphore_initialized = true;
    }

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&server->request_sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(server, NULL, bkvision_device_destroy,
                                    bkvision_ns_match, bkvision_ns_bind);
      callback_registered = ret >= 0;
    }

  if (ret >= 0)
    {
      pid = task_create("bkvision-rpc", CONFIG_BK7258_VISION_RPC_PRIORITY,
                        CONFIG_BK7258_VISION_RPC_STACKSIZE, bkvision_worker,
                        NULL);
      if (pid < 0)
        {
          ret = bkvision_errno();
        }
    }

  if (ret >= 0)
    {
      __atomic_store_n(&server->initialized, true, __ATOMIC_RELEASE);
    }
  else
    {
      if (callback_registered)
        {
          rpmsg_unregister_callback(server, NULL, bkvision_device_destroy,
                                    bkvision_ns_match, bkvision_ns_bind);
        }

      if (nxmutex_lock(&server->endpoint_lock) >= 0)
        {
          __atomic_store_n(&server->endpoint_created, false,
                           __ATOMIC_RELEASE);
          if (server->endpoint.rdev != NULL)
            {
              rpmsg_destroy_ept(&server->endpoint);
            }

          memset(&server->endpoint, 0, sizeof(server->endpoint));
          nxmutex_unlock(&server->endpoint_lock);
        }

      if (semaphore_initialized)
        {
          (void)nxsem_destroy(&server->request_sem);
        }

      server->active = false;
      server->replay_valid = false;
    }

  nxmutex_unlock(&server->init_lock);
  return ret;
}

#endif /* CONFIG_BK7258_VISION_SERVICE */
