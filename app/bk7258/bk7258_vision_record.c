/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_vision_record.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

#define AVI_HEADER_SIZE 224u
#define AVI_MAX_BYTES BKVISION_RECORD_MAX_BYTES

static void put32(uint8_t *p, uint32_t value)
{
  p[0] = value;
  p[1] = value >> 8;
  p[2] = value >> 16;
  p[3] = value >> 24;
}

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Non-cryptographic integrity check for the serialized movi payload. */
static uint32_t hash_bytes(uint32_t hash, const uint8_t *p, size_t size)
{
  while (size-- != 0)
    {
      hash = (hash ^ *p++) * 16777619u;
    }
  return hash;
}

static int write_all(int fd, const void *data, size_t size)
{
  const uint8_t *p = data;
  while (size != 0)
    {
      ssize_t n = write(fd, p, size);
      if (n < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          return errno > 0 ? -errno : -EIO;
        }
      if (n == 0)
        {
          return -EIO;
        }
      p += n;
      size -= n;
    }
  return 0;
}

static int flush_buffer(struct bkvision_record_s *r)
{
  int ret;

  if (r->write_error < 0)
    {
      return r->write_error;
    }

  ret = write_all(r->fd, r->write_buffer, r->write_used);
  if (ret < 0)
    {
      /* A short write may have reached media. Do not replay it on finish. */

      r->write_error = ret;
      return ret;
    }

  r->write_used = 0;
  r->write_limit = r->write_capacity;
  return 0;
}

static int write_payload(struct bkvision_record_s *r,
                          const void *data, size_t size)
{
  const uint8_t *p = data;
  size_t count;
  int ret;

  if (r->write_buffer == NULL)
    {
      return write_all(r->fd, data, size);
    }

  if (r->write_error < 0)
    {
      return r->write_error;
    }

  while (size != 0)
    {
      count = r->write_limit - r->write_used;
      if (count > size)
        {
          count = size;
        }

      memcpy(r->write_buffer + r->write_used, p, count);
      r->write_used += count;
      p += count;
      size -= count;
      if (r->write_used == r->write_limit)
        {
          ret = flush_buffer(r);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  return 0;
}

static int write_header(struct bkvision_record_s *r, uint32_t elapsed)
{
  uint8_t h[AVI_HEADER_SIZE] = {0};

  /* RIFF AVI: hdrl(avih, strl(strh, strf)), movi(00dc...). The optional
   * idx1 is omitted; AVIF_HASINDEX remains clear. Serialize little endian
   * explicitly rather than writing compiler-dependent packed structs.
   */

  memcpy(h, "RIFF", 4);
  put32(h + 4, AVI_HEADER_SIZE + r->bytes - 8);
  memcpy(h + 8, "AVI LIST", 8);
  put32(h + 16, 192);
  memcpy(h + 20, "hdrlavih", 8);
  put32(h + 28, 56);
  put32(h + 32, r->frames ? (uint64_t)elapsed * 1000 / r->frames : 33333);
  put32(h + 48, r->frames);
  put32(h + 56, 1);
  put32(h + 60, r->largest);
  put32(h + 64, r->width);
  put32(h + 68, r->height);
  memcpy(h + 88, "LIST", 4);
  put32(h + 92, 116);
  memcpy(h + 96, "strlstrh", 8);
  put32(h + 104, 56);
  memcpy(h + 108, "vidsMJPG", 8);
  put32(h + 128, elapsed ? elapsed : 1);
  put32(h + 132, r->frames ? r->frames * 1000 : 30);
  put32(h + 140, r->frames);
  put32(h + 144, r->largest);
  put32(h + 148, UINT32_MAX);
  h[160] = r->width;
  h[161] = r->width >> 8;
  h[162] = r->height;
  h[163] = r->height >> 8;
  memcpy(h + 164, "strf", 4);
  put32(h + 168, 40);
  put32(h + 172, 40);
  put32(h + 176, r->width);
  put32(h + 180, r->height);
  h[184] = 1;
  h[186] = 24;
  memcpy(h + 188, "MJPG", 4);
  put32(h + 192, r->width * r->height * 3);
  memcpy(h + 212, "LIST", 4);
  put32(h + 216, r->bytes + 4);
  memcpy(h + 220, "movi", 4);
  if (lseek(r->fd, 0, SEEK_SET) < 0)
    {
      return -errno;
    }
  return write_all(r->fd, h, sizeof(h));
}

int bkvision_record_begin(struct bkvision_record_s *r, int fd,
                          uint32_t width, uint32_t height)
{
  if (r == NULL || fd < 0 || width == 0 || height == 0 ||
      width > 4096 || height > 4096)
    {
      return -EINVAL;
    }
  memset(r, 0, sizeof(*r));
  r->fd = fd;
  r->payload_hash = 2166136261u;
  r->width = width;
  r->height = height;
  return write_header(r, 0);
}

int bkvision_record_set_buffer(struct bkvision_record_s *r,
                               void *buffer, size_t capacity)
{
  off_t position;

  if (r == NULL || buffer == NULL || capacity < 512u ||
      capacity % 512u != 0 || r->write_buffer != NULL ||
      r->frames != 0 || r->bytes != 0)
    {
      return -EINVAL;
    }

  position = lseek(r->fd, 0, SEEK_CUR);
  if (position < 0)
    {
      return -errno;
    }

  if (position != AVI_HEADER_SIZE)
    {
      return -EINVAL;
    }

  r->write_buffer = buffer;
  r->write_capacity = capacity;

  /* The header is already written. End the first flush at a whole buffer
   * boundary; all subsequent full writes then remain sector aligned.
   */

  r->write_limit = capacity - AVI_HEADER_SIZE % capacity;
  return 0;
}

int bkvision_record_frame(struct bkvision_record_s *r,
                          const void *data, size_t size)
{
  uint8_t h[8];
  const uint8_t zero = 0;
  const uint8_t *jpeg = data;
  int ret;

  if (r == NULL || jpeg == NULL || size < 4 || size > AVI_MAX_BYTES ||
      jpeg[0] != 0xff || jpeg[1] != 0xd8 ||
      jpeg[size - 2] != 0xff || jpeg[size - 1] != 0xd9)
    {
      return -EINVAL;
    }
  if (r->bytes > AVI_MAX_BYTES - size ||
      AVI_HEADER_SIZE + r->bytes + size + 9 > AVI_MAX_BYTES)
    {
      return -EFBIG;
    }
  memcpy(h, "00dc", 4);
  put32(h + 4, size);


  ret = write_payload(r, h, sizeof(h));
  if (ret == 0)
    {
      ret = write_payload(r, data, size);
    }
  if (ret == 0 && (size & 1))
    {
      ret = write_payload(r, &zero, 1);
    }
  if (ret == 0)
    {
      r->bytes += 8 + size + (size & 1);
      r->frames++;
      if (r->largest < size)
        {
          r->largest = size;
        }
    }
  return ret;
}

int bkvision_record_finish(struct bkvision_record_s *r, uint32_t elapsed_ms)
{
  int ret;
  if (r != NULL && r->write_error < 0)
    {
      return r->write_error;
    }
  if (r == NULL || r->frames < 2 || elapsed_ms == 0 ||
      r->frames > UINT32_MAX / 1000)
    {
      return -ENODATA;
    }
  ret = flush_buffer(r);
  if (ret == 0)
    {
      ret = write_header(r, elapsed_ms);
    }
  if (ret == 0 && fsync(r->fd) < 0)
    {
      ret = -errno;
    }
  return ret;
}
int bkvision_record_inspect(struct bkvision_record_s *r, int fd)
{
  uint32_t words[128];
  uint8_t *buffer = (uint8_t *)words;
  uint32_t hash = 2166136261u;
  off_t length = lseek(fd, 0, SEEK_END);
  uint32_t position = 0;
  ssize_t n;

  if (r == NULL || length < AVI_HEADER_SIZE ||
      (uint64_t)length > AVI_HEADER_SIZE + AVI_MAX_BYTES)
    {
      return length < 0 ? -errno : -ENODATA;
    }
  if (lseek(fd, 0, SEEK_SET) < 0)
    {
      return -errno;
    }
  /* Accumulate the complete header even when the backing read is short. */
  while (position < AVI_HEADER_SIZE)
    {
      n = read(fd, buffer + position, AVI_HEADER_SIZE - position);
      if (n < 0 && errno == EINTR)
        {
          continue;
        }
      if (n <= 0)
        {
          return n < 0 ? -errno : -EIO;
        }
      position += n;
    }
  if (memcmp(buffer, "RIFF", 4) != 0 ||
      memcmp(buffer + 8, "AVI ", 4) != 0 ||
      memcmp(buffer + 108, "vidsMJPG", 8) != 0 ||
      memcmp(buffer + 220, "movi", 4) != 0 ||
      (uint64_t)get32(buffer + 4) + 8 != (uint64_t)length ||
      get32(buffer + 216) != (uint32_t)length - 220 ||
      get32(buffer + 48) < 2 || get32(buffer + 48) != get32(buffer + 140))
    {
      return -EBADMSG;
    }
  memset(r, 0, sizeof(*r));
  r->fd = fd;
  r->width = get32(buffer + 64);
  r->height = get32(buffer + 68);
  r->frames = get32(buffer + 48);
  r->bytes = (uint32_t)length - AVI_HEADER_SIZE;
  if (r->width == 0 || r->width > 4096 || r->height == 0 || r->height > 4096)
    {
      return -EBADMSG;
    }
  hash = hash_bytes(hash, buffer, AVI_HEADER_SIZE);
  while (position < (uint32_t)length)
    {
      size_t count = (uint32_t)length - position;
      n = read(fd, buffer, count < sizeof(words) ? count : sizeof(words));
      if (n < 0 && errno == EINTR)
        {
          continue;
        }
      if (n <= 0)
        {
          return n < 0 ? -errno : -EIO;
        }
      hash = hash_bytes(hash, buffer, n);
      position += n;
    }
  /* For inspection this is the whole-file hash, including the header. */
  r->payload_hash = hash;
  return 0;
}
