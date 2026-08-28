/****************************************************************************
 * chips/bk7258/ap/bk7258_jpeg_m2m_validation.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded chip-level validation for the standard BK7258 JPEG V4L2 M2M ABI.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_JPEG_M2M_VALIDATION

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/videoio.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/cache.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>

#include <arch/chip/bk7258_jpeg_m2m_validation.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKJMVAL_WIDTH                   32u
#define BKJMVAL_HEIGHT                  16u
#define BKJMVAL_DECODED_SIZE            \
  (BKJMVAL_WIDTH * BKJMVAL_HEIGHT * 2u)
#define BKJMVAL_BUFFER_COUNT            2u
#define BKJMVAL_STACKSIZE               6144
#define BKJMVAL_POLL_SLICE_MS           100
#define BKJMVAL_POLL_ATTEMPTS           30u
#define BKJMVAL_REQUIRED_CAPS           \
  (V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING)
#define BKJMVAL_FIXTURE_BYTES           \
  ((uint32_t)sizeof(g_bkjmval_fixture))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bkjmval_buffer_stats_s
{
  uint32_t crc32;
  uint32_t nonzero;
  uint8_t minimum;
  uint8_t maximum;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Deterministic 32x16, 8-bit baseline JPEG with 4:2:2 sampling.  It is a
 * generated chip fixture and has no sensor, panel, GPIO, or board binding.
 */

static const uint8_t g_bkjmval_fixture[] =
{
  0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
  0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43,
  0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09,
  0x09, 0x08, 0x0a, 0x0c, 0x14, 0x0d, 0x0c, 0x0b, 0x0b, 0x0c, 0x19, 0x12,
  0x13, 0x0f, 0x14, 0x1d, 0x1a, 0x1f, 0x1e, 0x1d, 0x1a, 0x1c, 0x1c, 0x20,
  0x24, 0x2e, 0x27, 0x20, 0x22, 0x2c, 0x23, 0x1c, 0x1c, 0x28, 0x37, 0x29,
  0x2c, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1f, 0x27, 0x39, 0x3d, 0x38, 0x32,
  0x3c, 0x2e, 0x33, 0x34, 0x32, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x09, 0x09,
  0x09, 0x0c, 0x0b, 0x0c, 0x18, 0x0d, 0x0d, 0x18, 0x32, 0x21, 0x1c, 0x21,
  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
  0x32, 0x32, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x20, 0x03,
  0x01, 0x21, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xff, 0xc4, 0x00,
  0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
  0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00,
  0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00,
  0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21,
  0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81,
  0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24,
  0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25,
  0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
  0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
  0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
  0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86,
  0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
  0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3,
  0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6,
  0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
  0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1,
  0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4, 0x00,
  0x1f, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
  0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00,
  0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00,
  0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31,
  0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08,
  0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15,
  0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18,
  0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55,
  0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84,
  0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
  0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
  0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4,
  0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
  0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
  0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xda, 0x00,
  0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0xe3,
  0x34, 0x3f, 0x0c, 0x7d, 0xdf, 0xdd, 0xfe, 0x95, 0xe8, 0x16, 0xde, 0x1e,
  0x16, 0xfa, 0x5c, 0xf2, 0x60, 0xa1, 0x11, 0x90, 0xac, 0x33, 0x90, 0x4f,
  0x03, 0xa7, 0xb9, 0x14, 0xb0, 0xf5, 0xaf, 0x34, 0x8d, 0xf0, 0x99, 0x97,
  0xb3, 0xc3, 0xce, 0x6d, 0xda, 0xc9, 0xbf, 0xc0, 0x4d, 0x3f, 0xc3, 0xfe,
  0x54, 0x4a, 0xaa, 0xb8, 0x76, 0xf4, 0xea, 0x07, 0xad, 0x5c, 0xd4, 0xe7,
  0xd0, 0xb4, 0x8d, 0x16, 0x71, 0x73, 0xaa, 0x5b, 0xc7, 0x26, 0xf1, 0x09,
  0x48, 0xdf, 0x7b, 0x86, 0xcf, 0x20, 0xaa, 0xe4, 0x8e, 0x01, 0xcf, 0x15,
  0xc3, 0x9d, 0x62, 0x2a, 0xe3, 0x73, 0x48, 0x61, 0xe8, 0xae, 0x65, 0x0b,
  0x2f, 0x2b, 0xbd, 0x5f, 0xa7, 0x45, 0xf2, 0x3c, 0x9c, 0xb6, 0xad, 0x6a,
  0xb8, 0x2a, 0xaa, 0x8a, 0x6d, 0xf2, 0xcb, 0xf2, 0x76, 0xd7, 0xa6, 0xa6,
  0x1d, 0x9f, 0x8a, 0xb4, 0x7b, 0x6d, 0xc9, 0x65, 0x69, 0x71, 0x78, 0xeb,
  0x8d, 0x8d, 0xb7, 0xcb, 0x8d, 0xba, 0x67, 0x93, 0xf3, 0x0c, 0x73, 0xfc,
  0x3d, 0x47, 0xe3, 0x56, 0xf5, 0xdf, 0x19, 0x6a, 0xff, 0x00, 0xd8, 0x93,
  0x25, 0xa5, 0x9d, 0xbd, 0xa4, 0x72, 0xba, 0x2a, 0xb6, 0x37, 0xc8, 0x98,
  0xf9, 0x8f, 0x27, 0xe5, 0x39, 0xda, 0x47, 0xdd, 0xef, 0xf8, 0xd7, 0xb9,
  0x96, 0xe5, 0x51, 0xa7, 0x25, 0x5b, 0x17, 0x2d, 0x16, 0xb6, 0x5e, 0x5a,
  0xea, 0xfe, 0xf5, 0xa7, 0xde, 0x75, 0x60, 0xb2, 0x9e, 0x5c, 0x05, 0x49,
  0xe2, 0xa5, 0xf6, 0x5f, 0xba, 0xbc, 0xd5, 0xb5, 0x7f, 0x7d, 0xd2, 0xf9,
  0x33, 0x92, 0xb7, 0xb6, 0xd5, 0x75, 0x99, 0x0b, 0x5f, 0xde, 0x5c, 0xdc,
  0x2b, 0x3f, 0x98, 0x23, 0x92, 0x42, 0x51, 0x4f, 0x3d, 0x17, 0xa0, 0xea,
  0x7a, 0x0a, 0xd7, 0xd4, 0x3c, 0x3d, 0xb6, 0xd6, 0xd2, 0x0f, 0x2b, 0xef,
  0xb9, 0x7d, 0xde, 0x98, 0x18, 0xff, 0x00, 0xd9, 0xbf, 0x4a, 0xe0, 0xc2,
  0x62, 0x15, 0x4c, 0x57, 0x3a, 0x56, 0xbb, 0x6c, 0xf4, 0xa7, 0x8a, 0xa7,
  0x85, 0xca, 0x67, 0x0a, 0x49, 0x25, 0x64, 0xac, 0xb4, 0xdd, 0xa4, 0x7f,
  0xff, 0xd9
};

static volatile bool g_bkjmval_started;

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Intentionally non-static so a debugger can inspect the bounded result
 * without depending on a console or an RPMsg syslog transport.
 */

volatile struct bk7258_jpeg_m2m_validation_diag_s
  g_bk7258_jpeg_m2m_validation_diag;

_Static_assert(sizeof(struct bk7258_jpeg_m2m_validation_diag_s) == 0x9cu,
               "unexpected JPEG M2M validation diagnostic ABI");

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bkjmval_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static void bkjmval_set_stage(uint32_t stage)
{
  g_bk7258_jpeg_m2m_validation_diag.stage = stage;
}

static void bkjmval_check_passed(void)
{
  g_bk7258_jpeg_m2m_validation_diag.checks++;
}

static uint32_t bkjmval_crc32(FAR const uint8_t *data, size_t length)
{
  uint32_t crc = UINT32_MAX;
  size_t index;
  unsigned int bit;

  for (index = 0; index < length; index++)
    {
      crc ^= data[index];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^ (0xedb88320u &
                              (uint32_t)-(int32_t)(crc & 1u));
        }
    }

  return ~crc;
}

static void bkjmval_buffer_stats(FAR const uint8_t *data, size_t length,
                                 FAR struct bkjmval_buffer_stats_s *stats)
{
  size_t index;

  memset(stats, 0, sizeof(*stats));
  stats->minimum = UINT8_MAX;
  for (index = 0; index < length; index++)
    {
      if (data[index] != 0)
        {
          stats->nonzero++;
        }

      if (data[index] < stats->minimum)
        {
          stats->minimum = data[index];
        }

      if (data[index] > stats->maximum)
        {
          stats->maximum = data[index];
        }
    }

  stats->crc32 = bkjmval_crc32(data, length);
}

static int bkjmval_capture_storage(FAR size_t *cache_line,
                                   FAR uint32_t *storage)
{
  uint64_t aligned;
  size_t line;

  if (storage == NULL)
    {
      return -EINVAL;
    }

  line = up_get_dcache_linesize();
  if (line == 0)
    {
      line = sizeof(uintptr_t);
    }

  if ((line & (line - 1u)) != 0)
    {
      return -EIO;
    }

  aligned = (BKJMVAL_DECODED_SIZE + (uint64_t)line - 1u) &
            ~((uint64_t)line - 1u);
  if (aligned > UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  *storage = (uint32_t)aligned;
  if (cache_line != NULL)
    {
      *cache_line = line;
    }

  return 0;
}

static int bkjmval_expect_errno(int fd, int request, unsigned long argument,
                                int expected)
{
  int ret;

  errno = 0;
  ret = ioctl(fd, request, argument);
  if (ret >= 0)
    {
      return -EPROTO;
    }

  if (errno != expected)
    {
      return bkjmval_errno();
    }

  bkjmval_check_passed();
  return 0;
}

static int bkjmval_enum_format(int fd, uint32_t type, uint32_t fourcc,
                               bool compressed)
{
  struct v4l2_fmtdesc desc;
  int ret;

  memset(&desc, 0, sizeof(desc));
  desc.type = type;
  if (ioctl(fd, VIDIOC_ENUM_FMT, (unsigned long)(uintptr_t)&desc) < 0)
    {
      return bkjmval_errno();
    }

  if (desc.index != 0 || desc.type != type || desc.pixelformat != fourcc ||
      ((desc.flags & V4L2_FMT_FLAG_COMPRESSED) != 0) != compressed ||
      desc.description[0] == '\0')
    {
      return -EPROTO;
    }

  bkjmval_check_passed();
  desc.index = 1;
  ret = bkjmval_expect_errno(fd, VIDIOC_ENUM_FMT,
                             (unsigned long)(uintptr_t)&desc, EINVAL);
  if (ret < 0)
    {
      return ret;
    }

  return 0;
}

static void bkjmval_make_format(FAR struct v4l2_format *format,
                                uint32_t type, uint32_t fourcc)
{
  memset(format, 0, sizeof(*format));
  format->type = type;
  format->fmt.pix.width = BKJMVAL_WIDTH;
  format->fmt.pix.height = BKJMVAL_HEIGHT;
  format->fmt.pix.pixelformat = fourcc;
  format->fmt.pix.field = V4L2_FIELD_ANY;
}

static int bkjmval_check_format(FAR const struct v4l2_format *format,
                                uint32_t type, uint32_t fourcc)
{
  uint32_t storage;
  int ret;

  if (format->type != type || format->fmt.pix.width != BKJMVAL_WIDTH ||
      format->fmt.pix.height != BKJMVAL_HEIGHT ||
      format->fmt.pix.pixelformat != fourcc ||
      format->fmt.pix.field != V4L2_FIELD_NONE ||
      format->fmt.pix.colorspace != V4L2_COLORSPACE_JPEG)
    {
      return -EPROTO;
    }

  if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
    {
      if (format->fmt.pix.bytesperline != 0 ||
          format->fmt.pix.sizeimage !=
            (uint32_t)CONFIG_BK7258_JPEG_M2M_MAX_INPUT_SIZE)
        {
          return -EPROTO;
        }
    }
  else
    {
      ret = bkjmval_capture_storage(NULL, &storage);
      if (ret < 0)
        {
          return ret;
        }

      if (format->fmt.pix.bytesperline != BKJMVAL_WIDTH * 2u ||
          format->fmt.pix.sizeimage != storage)
        {
          return -EPROTO;
        }
    }

  bkjmval_check_passed();
  return 0;
}

static int bkjmval_ioctl_format(int fd, int request, uint32_t type,
                                uint32_t fourcc,
                                FAR struct v4l2_format *result)
{
  struct v4l2_format format;
  int ret;

  bkjmval_make_format(&format, type, fourcc);
  if (ioctl(fd, request, (unsigned long)(uintptr_t)&format) < 0)
    {
      return bkjmval_errno();
    }

  ret = bkjmval_check_format(&format, type, fourcc);
  if (ret < 0)
    {
      return ret;
    }

  if (result != NULL)
    {
      *result = format;
    }

  return 0;
}

static int bkjmval_get_format(int fd, uint32_t type, uint32_t fourcc,
                              FAR struct v4l2_format *result)
{
  struct v4l2_format format;
  int ret;

  memset(&format, 0, sizeof(format));
  format.type = type;
  if (ioctl(fd, VIDIOC_G_FMT, (unsigned long)(uintptr_t)&format) < 0)
    {
      return bkjmval_errno();
    }

  ret = bkjmval_check_format(&format, type, fourcc);
  if (ret >= 0 && result != NULL)
    {
      *result = format;
    }

  return ret;
}

static int bkjmval_request_buffers(int fd, uint32_t type,
                                   uint32_t memory, uint32_t count)
{
  struct v4l2_requestbuffers request;

  memset(&request, 0, sizeof(request));
  request.type = type;
  request.memory = memory;
  request.count = count;
  if (ioctl(fd, VIDIOC_REQBUFS,
            (unsigned long)(uintptr_t)&request) < 0)
    {
      return bkjmval_errno();
    }

  if (request.type != type || request.memory != memory ||
      request.count != count || request.mode != V4L2_BUF_MODE_FIFO)
    {
      return -EPROTO;
    }

  bkjmval_check_passed();
  return 0;
}

static int bkjmval_queue_capture(int fd, FAR uint8_t *capture,
                                 uint32_t length)
{
  struct v4l2_buffer buffer;

  memset(&buffer, 0, sizeof(buffer));
  buffer.index = 0;
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_USERPTR;
  buffer.m.userptr = (unsigned long)(uintptr_t)capture;
  buffer.length = length;
  if (ioctl(fd, VIDIOC_QBUF, (unsigned long)(uintptr_t)&buffer) < 0)
    {
      return bkjmval_errno();
    }

  return 0;
}

static int bkjmval_queue_output(int fd, uint32_t bytesused,
                                uint32_t timestamp_tag)
{
  struct v4l2_buffer buffer;

  memset(&buffer, 0, sizeof(buffer));
  buffer.index = 0;
  buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  buffer.bytesused = bytesused;
  buffer.memory = V4L2_MEMORY_USERPTR;
  buffer.m.userptr = (unsigned long)(uintptr_t)g_bkjmval_fixture;
  buffer.length = BKJMVAL_FIXTURE_BYTES;
  buffer.timestamp.tv_sec = (time_t)timestamp_tag;
  buffer.timestamp.tv_usec = (long)(timestamp_tag * 1000u);
  if (ioctl(fd, VIDIOC_QBUF, (unsigned long)(uintptr_t)&buffer) < 0)
    {
      return bkjmval_errno();
    }

  return 0;
}

static int bkjmval_queue_pair(int fd, FAR uint8_t *capture,
                              uint32_t capture_length, uint32_t bytesused,
                              uint32_t timestamp_tag)
{
  int ret;

  ret = bkjmval_queue_capture(fd, capture, capture_length);
  if (ret >= 0)
    {
      ret = bkjmval_queue_output(fd, bytesused, timestamp_tag);
    }

  return ret;
}

static int bkjmval_try_dequeue(int fd, uint32_t type,
                               FAR struct v4l2_buffer *buffer,
                               FAR bool *done)
{
  struct v4l2_buffer candidate;

  if (*done)
    {
      return 0;
    }

  memset(&candidate, 0, sizeof(candidate));
  candidate.type = type;
  candidate.memory = V4L2_MEMORY_USERPTR;
  errno = 0;
  if (ioctl(fd, VIDIOC_DQBUF,
            (unsigned long)(uintptr_t)&candidate) < 0)
    {
      if (errno == EAGAIN)
        {
          return 0;
        }

      return bkjmval_errno();
    }

  *buffer = candidate;
  *done = true;
  g_bk7258_jpeg_m2m_validation_diag.dqbuf_count++;
  return 0;
}

static int bkjmval_wait_buffers(int fd, bool need_output,
                                bool need_capture,
                                FAR struct v4l2_buffer *output,
                                FAR struct v4l2_buffer *capture)
{
  bool output_done = !need_output;
  bool capture_done = !need_capture;
  unsigned int attempt;
  int ret;

  memset(output, 0, sizeof(*output));
  memset(capture, 0, sizeof(*capture));
  for (attempt = 0; attempt < BKJMVAL_POLL_ATTEMPTS; attempt++)
    {
      struct pollfd pfd;

      ret = bkjmval_try_dequeue(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                                output, &output_done);
      if (ret < 0)
        {
          return ret;
        }

      ret = bkjmval_try_dequeue(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                capture, &capture_done);
      if (ret < 0)
        {
          return ret;
        }

      if (output_done && capture_done)
        {
          return 0;
        }

      memset(&pfd, 0, sizeof(pfd));
      pfd.fd = fd;
      if (!output_done)
        {
          pfd.events |= POLLOUT;
        }

      if (!capture_done)
        {
          pfd.events |= POLLIN;
        }

      g_bk7258_jpeg_m2m_validation_diag.poll_calls++;
      errno = 0;
      ret = poll(&pfd, 1, BKJMVAL_POLL_SLICE_MS);
      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return bkjmval_errno();
        }

      if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        {
          return -EIO;
        }
    }

  return -ETIMEDOUT;
}

static bool bkjmval_same_timestamp(FAR const struct v4l2_buffer *first,
                                   FAR const struct v4l2_buffer *second,
                                   uint32_t tag)
{
  return first->timestamp.tv_sec == (time_t)tag &&
         first->timestamp.tv_usec == (long)(tag * 1000u) &&
         first->timestamp.tv_sec == second->timestamp.tv_sec &&
         first->timestamp.tv_usec == second->timestamp.tv_usec;
}

static int bkjmval_validate_success(
  FAR const struct v4l2_buffer *output,
  FAR const struct v4l2_buffer *capture,
  FAR const uint8_t *capture_data, uint32_t capture_storage,
  uint32_t sequence, uint32_t tag,
  FAR struct bkjmval_buffer_stats_s *stats)
{
  if (output->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
      output->memory != V4L2_MEMORY_USERPTR || output->index != 0 ||
      output->m.userptr != (unsigned long)(uintptr_t)g_bkjmval_fixture ||
      output->length != BKJMVAL_FIXTURE_BYTES ||
      output->bytesused != BKJMVAL_FIXTURE_BYTES ||
      (output->flags & V4L2_BUF_FLAG_ERROR) != 0 ||
      capture->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
      capture->memory != V4L2_MEMORY_USERPTR || capture->index != 0 ||
      capture->m.userptr != (unsigned long)(uintptr_t)capture_data ||
      capture->length != capture_storage ||
      capture->bytesused != BKJMVAL_DECODED_SIZE ||
      (capture->flags & V4L2_BUF_FLAG_ERROR) != 0 ||
      capture->field != V4L2_FIELD_NONE || capture->sequence != sequence ||
      !bkjmval_same_timestamp(output, capture, tag))
    {
      return -EPROTO;
    }

  bkjmval_buffer_stats(capture_data, capture->bytesused, stats);
  if (stats->nonzero == 0 || stats->minimum == stats->maximum)
    {
      return -ENODATA;
    }

  bkjmval_check_passed();
  return 0;
}

static int bkjmval_validate_negative(
  FAR const struct v4l2_buffer *output,
  FAR const struct v4l2_buffer *capture,
  FAR const uint8_t *capture_data, uint32_t capture_storage)
{
  if (output->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
      output->memory != V4L2_MEMORY_USERPTR || output->index != 0 ||
      output->m.userptr != (unsigned long)(uintptr_t)g_bkjmval_fixture ||
      output->bytesused != BKJMVAL_FIXTURE_BYTES - 2u ||
      (output->flags & V4L2_BUF_FLAG_ERROR) == 0 ||
      capture->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
      capture->memory != V4L2_MEMORY_USERPTR || capture->index != 0 ||
      capture->m.userptr != (unsigned long)(uintptr_t)capture_data ||
      capture->length != capture_storage || capture->bytesused != 0 ||
      (capture->flags & V4L2_BUF_FLAG_ERROR) == 0)
    {
      return -EPROTO;
    }

  bkjmval_check_passed();
  return 0;
}

static int bkjmval_validate_drain(FAR const struct v4l2_buffer *output)
{
  if (output->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
      output->memory != V4L2_MEMORY_USERPTR || output->index != 0 ||
      output->m.userptr != (unsigned long)(uintptr_t)g_bkjmval_fixture ||
      output->bytesused != BKJMVAL_FIXTURE_BYTES ||
      (output->flags & V4L2_BUF_FLAG_ERROR) == 0)
    {
      return -EPROTO;
    }

  bkjmval_check_passed();
  return 0;
}

static int bkjmval_run(void)
{
  struct bkjmval_buffer_stats_s valid_stats = {0};
  struct bkjmval_buffer_stats_s recovery_stats = {0};
  struct v4l2_capability capability;
  struct v4l2_format capture_format;
  struct v4l2_format late_format;
  struct v4l2_buffer output;
  struct v4l2_buffer capture;
  struct v4l2_requestbuffers request;
  FAR uint8_t *capture_data = NULL;
  enum v4l2_buf_type type;
  uint32_t capture_storage;
  size_t alignment;
  bool output_on = false;
  bool capture_on = false;
  int second_fd;
  int fd = -1;
  int ret;

  if ((size_t)BKJMVAL_FIXTURE_BYTES >
        (size_t)CONFIG_BK7258_JPEG_M2M_MAX_INPUT_SIZE ||
      (uint32_t)CONFIG_BK7258_JPEG_M2M_MAX_WIDTH < BKJMVAL_WIDTH ||
      (uint32_t)CONFIG_BK7258_JPEG_M2M_MAX_HEIGHT < BKJMVAL_HEIGHT)
    {
      return -E2BIG;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_OPEN);
  fd = open(CONFIG_BK7258_JPEG_M2M_DEVPATH, O_RDWR | O_NONBLOCK);
  if (fd < 0)
    {
      return bkjmval_errno();
    }

  bkjmval_check_passed();
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_SECOND_OPEN);
  errno = 0;
  second_fd = open(CONFIG_BK7258_JPEG_M2M_DEVPATH,
                   O_RDWR | O_NONBLOCK);
  if (second_fd >= 0)
    {
      close(second_fd);
      ret = -EPROTO;
      goto out;
    }

  if (errno != EBUSY)
    {
      ret = bkjmval_errno();
      goto out;
    }

  bkjmval_check_passed();
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_QUERYCAP);
  memset(&capability, 0, sizeof(capability));
  if (ioctl(fd, VIDIOC_QUERYCAP,
            (unsigned long)(uintptr_t)&capability) < 0)
    {
      ret = bkjmval_errno();
      goto out;
    }

  g_bk7258_jpeg_m2m_validation_diag.capabilities =
    capability.capabilities;
  g_bk7258_jpeg_m2m_validation_diag.device_caps =
    capability.device_caps;
  if ((capability.capabilities & BKJMVAL_REQUIRED_CAPS) !=
        BKJMVAL_REQUIRED_CAPS ||
      (capability.device_caps & BKJMVAL_REQUIRED_CAPS) !=
        BKJMVAL_REQUIRED_CAPS || capability.driver[0] == '\0' ||
      capability.card[0] == '\0')
    {
      ret = -EPROTO;
      goto out;
    }

  bkjmval_check_passed();
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_ENUM_FMT);
  ret = bkjmval_enum_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                            V4L2_PIX_FMT_JPEG, true);
  if (ret < 0)
    {
      goto out;
    }

  ret = bkjmval_enum_format(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                            V4L2_PIX_FMT_YUYV, false);
  if (ret < 0)
    {
      goto out;
    }

  g_bk7258_jpeg_m2m_validation_diag.output_fourcc = V4L2_PIX_FMT_JPEG;
  g_bk7258_jpeg_m2m_validation_diag.capture_fourcc = V4L2_PIX_FMT_YUYV;

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_TRY_FMT);
  ret = bkjmval_ioctl_format(fd, VIDIOC_TRY_FMT,
                             V4L2_BUF_TYPE_VIDEO_OUTPUT,
                             V4L2_PIX_FMT_JPEG, NULL);
  if (ret < 0)
    {
      goto out;
    }

  ret = bkjmval_ioctl_format(fd, VIDIOC_TRY_FMT,
                             V4L2_BUF_TYPE_VIDEO_CAPTURE,
                             V4L2_PIX_FMT_YUYV, NULL);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_S_FMT);
  ret = bkjmval_ioctl_format(fd, VIDIOC_S_FMT,
                             V4L2_BUF_TYPE_VIDEO_OUTPUT,
                             V4L2_PIX_FMT_JPEG, NULL);
  if (ret < 0)
    {
      goto out;
    }

  ret = bkjmval_ioctl_format(fd, VIDIOC_S_FMT,
                             V4L2_BUF_TYPE_VIDEO_CAPTURE,
                             V4L2_PIX_FMT_YUYV, &capture_format);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_G_FMT);
  ret = bkjmval_get_format(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                           V4L2_PIX_FMT_JPEG, NULL);
  if (ret < 0)
    {
      goto out;
    }

  ret = bkjmval_get_format(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                           V4L2_PIX_FMT_YUYV, &capture_format);
  if (ret < 0)
    {
      goto out;
    }

  g_bk7258_jpeg_m2m_validation_diag.width = capture_format.fmt.pix.width;
  g_bk7258_jpeg_m2m_validation_diag.height = capture_format.fmt.pix.height;
  g_bk7258_jpeg_m2m_validation_diag.capture_size =
    capture_format.fmt.pix.sizeimage;

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_MMAP_REJECT);
  memset(&request, 0, sizeof(request));
  request.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  request.memory = V4L2_MEMORY_MMAP;
  request.count = BKJMVAL_BUFFER_COUNT;
  ret = bkjmval_expect_errno(fd, VIDIOC_REQBUFS,
                             (unsigned long)(uintptr_t)&request, ENOTSUP);
  if (ret < 0)
    {
      goto out;
    }

  memset(&request, 0, sizeof(request));
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  request.count = BKJMVAL_BUFFER_COUNT;
  ret = bkjmval_expect_errno(fd, VIDIOC_REQBUFS,
                             (unsigned long)(uintptr_t)&request, ENOTSUP);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_USERPTR_REQBUFS);
  ret = bkjmval_request_buffers(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                                V4L2_MEMORY_USERPTR,
                                BKJMVAL_BUFFER_COUNT);
  if (ret < 0)
    {
      goto out;
    }

  ret = bkjmval_request_buffers(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                V4L2_MEMORY_USERPTR,
                                BKJMVAL_BUFFER_COUNT);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_LATE_S_FMT);
  bkjmval_make_format(&late_format, V4L2_BUF_TYPE_VIDEO_OUTPUT,
                      V4L2_PIX_FMT_JPEG);
  ret = bkjmval_expect_errno(fd, VIDIOC_S_FMT,
                             (unsigned long)(uintptr_t)&late_format, EBUSY);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_make_format(&late_format, V4L2_BUF_TYPE_VIDEO_CAPTURE,
                      V4L2_PIX_FMT_YUYV);
  ret = bkjmval_expect_errno(fd, VIDIOC_S_FMT,
                             (unsigned long)(uintptr_t)&late_format, EBUSY);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_ALLOCATE);
  ret = bkjmval_capture_storage(&alignment, &capture_storage);
  if (ret < 0)
    {
      goto out;
    }

  if (capture_format.fmt.pix.sizeimage != capture_storage)
    {
      ret = -EPROTO;
      goto out;
    }

  capture_data = kmm_memalign(alignment, capture_storage);
  if (capture_data == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  if (((uintptr_t)capture_data & (alignment - 1u)) != 0)
    {
      ret = -EFAULT;
      goto out;
    }

  bkjmval_check_passed();
  memset(capture_data, 0xa5, capture_storage);
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_VALID_QBUF);
  ret = bkjmval_queue_pair(fd, capture_data, capture_storage,
                           BKJMVAL_FIXTURE_BYTES, 1u);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_STREAMON);
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_STREAMON, (unsigned long)(uintptr_t)&type) < 0)
    {
      ret = bkjmval_errno();
      goto out;
    }

  capture_on = true;
  type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  if (ioctl(fd, VIDIOC_STREAMON, (unsigned long)(uintptr_t)&type) < 0)
    {
      ret = bkjmval_errno();
      goto out;
    }

  output_on = true;
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_VALID_DQBUF);
  ret = bkjmval_wait_buffers(fd, true, true, &output, &capture);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_VALID_RESULT);
  ret = bkjmval_validate_success(&output, &capture, capture_data,
                                 capture_storage, 0u, 1u, &valid_stats);
  g_bk7258_jpeg_m2m_validation_diag.valid_output_flags = output.flags;
  g_bk7258_jpeg_m2m_validation_diag.valid_capture_flags = capture.flags;
  g_bk7258_jpeg_m2m_validation_diag.valid_capture_bytesused =
    capture.bytesused;
  g_bk7258_jpeg_m2m_validation_diag.valid_sequence = capture.sequence;
  g_bk7258_jpeg_m2m_validation_diag.valid_crc32 = valid_stats.crc32;
  g_bk7258_jpeg_m2m_validation_diag.valid_nonzero_bytes = valid_stats.nonzero;
  g_bk7258_jpeg_m2m_validation_diag.valid_min_byte = valid_stats.minimum;
  g_bk7258_jpeg_m2m_validation_diag.valid_max_byte = valid_stats.maximum;
  if (ret < 0)
    {
      goto out;
    }

  g_bk7258_jpeg_m2m_validation_diag.completed_pairs++;
  memset(capture_data, 0xa5, capture_storage);
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_NEGATIVE_QBUF);
  ret = bkjmval_queue_pair(fd, capture_data, capture_storage,
                           BKJMVAL_FIXTURE_BYTES - 2u, 2u);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_NEGATIVE_DQBUF);
  ret = bkjmval_wait_buffers(fd, true, true, &output, &capture);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_NEGATIVE_RESULT);
  ret = bkjmval_validate_negative(&output, &capture, capture_data,
                                   capture_storage);
  g_bk7258_jpeg_m2m_validation_diag.negative_output_flags = output.flags;
  g_bk7258_jpeg_m2m_validation_diag.negative_capture_flags = capture.flags;
  g_bk7258_jpeg_m2m_validation_diag.negative_capture_bytesused =
    capture.bytesused;
  if (ret < 0)
    {
      goto out;
    }

  g_bk7258_jpeg_m2m_validation_diag.completed_pairs++;
  memset(capture_data, 0xa5, capture_storage);
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_RECOVERY_QBUF);
  ret = bkjmval_queue_pair(fd, capture_data, capture_storage,
                           BKJMVAL_FIXTURE_BYTES, 3u);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_RECOVERY_DQBUF);
  ret = bkjmval_wait_buffers(fd, true, true, &output, &capture);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_RECOVERY_RESULT);
  ret = bkjmval_validate_success(&output, &capture, capture_data,
                                 capture_storage, 1u, 3u, &recovery_stats);
  g_bk7258_jpeg_m2m_validation_diag.recovery_capture_flags = capture.flags;
  g_bk7258_jpeg_m2m_validation_diag.recovery_capture_bytesused =
    capture.bytesused;
  g_bk7258_jpeg_m2m_validation_diag.recovery_sequence = capture.sequence;
  g_bk7258_jpeg_m2m_validation_diag.recovery_crc32 = recovery_stats.crc32;
  g_bk7258_jpeg_m2m_validation_diag.recovery_nonzero_bytes =
    recovery_stats.nonzero;
  g_bk7258_jpeg_m2m_validation_diag.recovery_min_byte =
    recovery_stats.minimum;
  g_bk7258_jpeg_m2m_validation_diag.recovery_max_byte =
    recovery_stats.maximum;
  if (ret < 0)
    {
      goto out;
    }

  if (valid_stats.crc32 != recovery_stats.crc32 ||
      valid_stats.nonzero != recovery_stats.nonzero ||
      valid_stats.minimum != recovery_stats.minimum ||
      valid_stats.maximum != recovery_stats.maximum)
    {
      ret = -EILSEQ;
      goto out;
    }

  bkjmval_check_passed();
  g_bk7258_jpeg_m2m_validation_diag.completed_pairs++;
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_DRAIN_QBUF);
  ret = bkjmval_queue_output(fd, BKJMVAL_FIXTURE_BYTES, 4u);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_DRAIN_STREAMOFF);
  type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  if (ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)(uintptr_t)&type) < 0)
    {
      ret = bkjmval_errno();
      goto out;
    }

  output_on = false;
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_DRAIN_DQBUF);
  ret = bkjmval_wait_buffers(fd, true, false, &output, &capture);
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_DRAIN_RESULT);
  ret = bkjmval_validate_drain(&output);
  g_bk7258_jpeg_m2m_validation_diag.drain_output_flags = output.flags;
  g_bk7258_jpeg_m2m_validation_diag.drain_output_bytesused =
    output.bytesused;
  if (ret < 0)
    {
      goto out;
    }

  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_CAPTURE_STREAMOFF);
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)(uintptr_t)&type) < 0)
    {
      ret = bkjmval_errno();
      goto out;
    }

  capture_on = false;
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_CLOSE);
  if (close(fd) < 0)
    {
      ret = bkjmval_errno();
      fd = -1;
      goto out;
    }

  fd = -1;
  kmm_free(capture_data);
  bkjmval_check_passed();
  bkjmval_set_stage(BK7258_JPEG_M2M_VALIDATION_STAGE_COMPLETE);
  return 0;

out:
  if (output_on)
    {
      type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
      (void)ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)(uintptr_t)&type);
    }

  if (capture_on)
    {
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      (void)ioctl(fd, VIDIOC_STREAMOFF, (unsigned long)(uintptr_t)&type);
    }

  if (fd >= 0)
    {
      (void)close(fd);
    }

  kmm_free(capture_data);
  return ret;
}

static void bkjmval_publish_terminal(int result)
{
  g_bk7258_jpeg_m2m_validation_diag.result = result;

  /* State is the terminal publication field.  Every diagnostic word must be
   * globally visible before a debugger can observe PASSED or FAILED.
   */

  __asm volatile ("dmb sy" ::: "memory");
  g_bk7258_jpeg_m2m_validation_diag.state =
    result == 0 ? BK7258_JPEG_M2M_VALIDATION_PASSED :
                  BK7258_JPEG_M2M_VALIDATION_FAILED;
}

static int bkjmval_thread(int argc, FAR char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;

  ret = bkjmval_run();
  bkjmval_publish_terminal(ret);
  if (ret == 0)
    {
      syslog(LOG_INFO,
             "BJMV PASS checks=%" PRIu32 " pairs=%" PRIu32
             " size=%" PRIu32 " crc=%08" PRIx32
             " polls=%" PRIu32 "\n",
             g_bk7258_jpeg_m2m_validation_diag.checks,
             g_bk7258_jpeg_m2m_validation_diag.completed_pairs,
             g_bk7258_jpeg_m2m_validation_diag.valid_capture_bytesused,
             g_bk7258_jpeg_m2m_validation_diag.valid_crc32,
             g_bk7258_jpeg_m2m_validation_diag.poll_calls);
    }
  else
    {
      syslog(LOG_ERR,
             "BJMV FAIL stage=%" PRIu32 " ret=%d checks=%" PRIu32
             " polls=%" PRIu32 "\n",
             g_bk7258_jpeg_m2m_validation_diag.stage, ret,
             g_bk7258_jpeg_m2m_validation_diag.checks,
             g_bk7258_jpeg_m2m_validation_diag.poll_calls);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_jpeg_m2m_validation_start(void)
{
  uint32_t capture_storage = 0;
  bool expected = false;
  int ret;

  if (!__atomic_compare_exchange_n(&g_bkjmval_started, &expected, true,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  memset((FAR void *)&g_bk7258_jpeg_m2m_validation_diag, 0,
         sizeof(g_bk7258_jpeg_m2m_validation_diag));
  g_bk7258_jpeg_m2m_validation_diag.magic =
    BK7258_JPEG_M2M_VALIDATION_MAGIC;
  g_bk7258_jpeg_m2m_validation_diag.version =
    BK7258_JPEG_M2M_VALIDATION_VERSION;
  g_bk7258_jpeg_m2m_validation_diag.size =
    sizeof(g_bk7258_jpeg_m2m_validation_diag);
  g_bk7258_jpeg_m2m_validation_diag.result = -EINPROGRESS;
  g_bk7258_jpeg_m2m_validation_diag.stage =
    BK7258_JPEG_M2M_VALIDATION_STAGE_INIT;
  g_bk7258_jpeg_m2m_validation_diag.width = BKJMVAL_WIDTH;
  g_bk7258_jpeg_m2m_validation_diag.height = BKJMVAL_HEIGHT;
  g_bk7258_jpeg_m2m_validation_diag.fixture_bytes =
    BKJMVAL_FIXTURE_BYTES;
  g_bk7258_jpeg_m2m_validation_diag.fixture_crc32 =
    bkjmval_crc32(g_bkjmval_fixture, BKJMVAL_FIXTURE_BYTES);
  (void)bkjmval_capture_storage(NULL, &capture_storage);
  g_bk7258_jpeg_m2m_validation_diag.capture_size =
    capture_storage;
  __asm volatile ("dmb sy" ::: "memory");
  g_bk7258_jpeg_m2m_validation_diag.state =
    BK7258_JPEG_M2M_VALIDATION_RUNNING;

  ret = kthread_create("bjpeg-m2m-val", SCHED_PRIORITY_DEFAULT,
                       BKJMVAL_STACKSIZE, bkjmval_thread, NULL);
  if (ret < 0)
    {
      bkjmval_publish_terminal(ret);
      return ret;
    }

  return 0;
}

#endif /* CONFIG_BK7258_JPEG_M2M_VALIDATION */
