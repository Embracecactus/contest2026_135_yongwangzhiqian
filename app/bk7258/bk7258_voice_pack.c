/****************************************************************************
 * app/bk7258/bk7258_voice_pack.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Strict, host-testable parser for local authorized voice packs and their
 * 16 kHz mono PCM WAV assets.
 ****************************************************************************/

#include "bk7258_voice_pack.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BKVOICE_LINE_SIZE           320u
#define BKVOICE_MAX_CHUNKS          64u

#define BKVOICE_FIELD_FORMAT        (1u << 0)
#define BKVOICE_FIELD_SPEAKER       (1u << 1)
#define BKVOICE_FIELD_AUTHORIZATION (1u << 2)
#define BKVOICE_FIELD_DISCLOSURE    (1u << 3)
#define BKVOICE_FIELD_RATE          (1u << 4)
#define BKVOICE_FIELD_CHANNELS      (1u << 5)
#define BKVOICE_FIELD_ENCODING      (1u << 6)
#define BKVOICE_REQUIRED_FIELDS     ((1u << 7) - 1u)

static int bkvoice_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static uint16_t bkvoice_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t bkvoice_le32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int bkvoice_read_exact(int fd, void *buffer, size_t size)
{
  uint8_t *cursor = buffer;
  size_t done = 0;

  while (done < size)
    {
      ssize_t nread = read(fd, cursor + done, size - done);

      if (nread < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return bkvoice_errno();
        }

      if (nread == 0)
        {
          return -ENODATA;
        }

      done += (size_t)nread;
    }

  return 0;
}

static int bkvoice_read_line(int fd, char *line, size_t capacity,
                             bool *eof)
{
  size_t length = 0;

  *eof = false;
  for (;;)
    {
      char byte;
      ssize_t nread = read(fd, &byte, 1);

      if (nread < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return bkvoice_errno();
        }

      if (nread == 0)
        {
          *eof = true;
          break;
        }

      if (byte == '\0')
        {
          return -EPROTO;
        }

      if (byte == '\n')
        {
          break;
        }

      if (length + 1 >= capacity)
        {
          return -E2BIG;
        }

      line[length++] = byte;
    }

  line[length] = '\0';
  return length == 0 && *eof ? 1 : 0;
}

static char *bkvoice_trim(char *text)
{
  char *end;

  while (*text != '\0' && isspace((unsigned char)*text))
    {
      text++;
    }

  end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1]))
    {
      *--end = '\0';
    }

  return text;
}

static bool bkvoice_valid_id(const char *id)
{
  size_t length = 0;

  while (*id != '\0')
    {
      unsigned char byte = (unsigned char)*id++;

      if (!isalnum(byte) && byte != '_' && byte != '-' && byte != '.')
        {
          return false;
        }

      length++;
      if (length >= BKVOICE_ID_SIZE)
        {
          return false;
        }
    }

  return length > 0;
}

static bool bkvoice_valid_relative_path(const char *path)
{
  const char *segment = path;
  const char *cursor = path;

  if (*path == '\0' || *path == '/' || *path == '\\')
    {
      return false;
    }

  for (;; cursor++)
    {
      unsigned char byte = (unsigned char)*cursor;

      if (byte == '/' || byte == '\0')
        {
          size_t length = (size_t)(cursor - segment);

          if (length == 0 || (length == 1 && segment[0] == '.') ||
              (length == 2 && segment[0] == '.' && segment[1] == '.'))
            {
              return false;
            }

          if (byte == '\0')
            {
              return true;
            }

          segment = cursor + 1;
          continue;
        }

      if (!isalnum(byte) && byte != '_' && byte != '-' && byte != '.')
        {
          return false;
        }
    }
}

static int bkvoice_copy(char *destination, size_t capacity,
                        const char *source)
{
  size_t length = strlen(source);

  if (length + 1 > capacity)
    {
      return -ENAMETOOLONG;
    }

  memcpy(destination, source, length + 1);
  return 0;
}

static int bkvoice_set_field(unsigned int *fields, unsigned int field)
{
  if ((*fields & field) != 0)
    {
      return -EEXIST;
    }

  *fields |= field;
  return 0;
}

static int bkvoice_resolve_path(const char *manifest_path,
                                const char *relative_path,
                                char *resolved, size_t capacity)
{
  const char *slash = strrchr(manifest_path, '/');
  size_t prefix;
  size_t relative = strlen(relative_path);

  if (slash == NULL)
    {
      return bkvoice_copy(resolved, capacity, relative_path);
    }

  prefix = (size_t)(slash - manifest_path);
  if (prefix == 0)
    {
      if (relative + 2 > capacity)
        {
          return -ENAMETOOLONG;
        }

      resolved[0] = '/';
      memcpy(resolved + 1, relative_path, relative + 1);
      return 0;
    }

  if (prefix + 1 + relative + 1 > capacity)
    {
      return -ENAMETOOLONG;
    }

  memcpy(resolved, manifest_path, prefix);
  resolved[prefix] = '/';
  memcpy(resolved + prefix + 1, relative_path, relative + 1);
  return 0;
}

int bkvoice_pack_load(const char *manifest_path, const char *selected_clip,
                      struct bkvoice_pack_info_s *info)
{
  char clip_names[BKVOICE_MAX_CLIPS][BKVOICE_ID_SIZE];
  char selected_relative[BKVOICE_PATH_SIZE];
  char line[BKVOICE_LINE_SIZE];
  unsigned int fields = 0;
  unsigned int line_number = 0;
  unsigned int clip_count = 0;
  bool selected_found = false;
  bool eof;
  int fd;
  int ret = 0;

  if (manifest_path == NULL || *manifest_path == '\0' || info == NULL ||
      (selected_clip != NULL && !bkvoice_valid_id(selected_clip)))
    {
      return -EINVAL;
    }

  memset(info, 0, sizeof(*info));
  memset(selected_relative, 0, sizeof(selected_relative));
  fd = open(manifest_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      return bkvoice_errno();
    }

  for (;;)
    {
      char *key;
      char *value;
      char *separator;

      ret = bkvoice_read_line(fd, line, sizeof(line), &eof);
      if (ret == 1)
        {
          ret = 0;
          break;
        }

      line_number++;
      if (ret < 0)
        {
          break;
        }

      key = bkvoice_trim(line);
      if (*key == '\0' || *key == '#')
        {
          if (eof)
            {
              break;
            }

          continue;
        }

      separator = strchr(key, '=');
      if (separator == NULL)
        {
          ret = -EPROTO;
          break;
        }

      *separator = '\0';
      value = bkvoice_trim(separator + 1);
      key = bkvoice_trim(key);
      if (*key == '\0' || *value == '\0')
        {
          ret = -EPROTO;
          break;
        }

      if (strcmp(key, "format") == 0)
        {
          ret = bkvoice_set_field(&fields, BKVOICE_FIELD_FORMAT);
          if (ret == 0 && strcmp(value, "bkvoice-pack-v1") != 0)
            {
              ret = -EPROTONOSUPPORT;
            }

          info->version = BKVOICE_PACK_VERSION;
        }
      else if (strcmp(key, "speaker_id") == 0)
        {
          ret = bkvoice_set_field(&fields, BKVOICE_FIELD_SPEAKER);
          if (ret == 0 && !bkvoice_valid_id(value))
            {
              ret = -EINVAL;
            }

          if (ret == 0)
            {
              ret = bkvoice_copy(info->speaker_id,
                                 sizeof(info->speaker_id), value);
            }
        }
      else if (strcmp(key, "authorization") == 0)
        {
          ret = bkvoice_set_field(&fields, BKVOICE_FIELD_AUTHORIZATION);
          if (ret == 0 && strcmp(value, "explicit-consent") != 0)
            {
              ret = -EACCES;
            }
        }
      else if (strcmp(key, "disclosure") == 0)
        {
          ret = bkvoice_set_field(&fields, BKVOICE_FIELD_DISCLOSURE);
          if (ret == 0 && strcmp(value, "synthetic-voice") != 0)
            {
              ret = -EACCES;
            }
        }
      else if (strcmp(key, "sample_rate") == 0)
        {
          ret = bkvoice_set_field(&fields, BKVOICE_FIELD_RATE);
          if (ret == 0 && strcmp(value, "16000") != 0)
            {
              ret = -ENOTSUP;
            }
        }
      else if (strcmp(key, "channels") == 0)
        {
          ret = bkvoice_set_field(&fields, BKVOICE_FIELD_CHANNELS);
          if (ret == 0 && strcmp(value, "1") != 0)
            {
              ret = -ENOTSUP;
            }
        }
      else if (strcmp(key, "encoding") == 0)
        {
          ret = bkvoice_set_field(&fields, BKVOICE_FIELD_ENCODING);
          if (ret == 0 && strcmp(value, "pcm-s16le") != 0)
            {
              ret = -ENOTSUP;
            }
        }
      else if (strncmp(key, "clip.", 5) == 0)
        {
          const char *clip_id = key + 5;
          unsigned int index;

          if (!bkvoice_valid_id(clip_id) ||
              !bkvoice_valid_relative_path(value))
            {
              ret = -EINVAL;
            }
          else if (clip_count >= BKVOICE_MAX_CLIPS)
            {
              ret = -E2BIG;
            }

          for (index = 0; ret == 0 && index < clip_count; index++)
            {
              if (strcmp(clip_names[index], clip_id) == 0)
                {
                  ret = -EEXIST;
                }
            }

          if (ret == 0)
            {
              ret = bkvoice_copy(clip_names[clip_count],
                                 sizeof(clip_names[clip_count]), clip_id);
            }

          if (ret == 0 && selected_clip != NULL &&
              strcmp(selected_clip, clip_id) == 0)
            {
              ret = bkvoice_copy(selected_relative,
                                 sizeof(selected_relative), value);
              selected_found = ret == 0;
            }

          if (ret == 0)
            {
              clip_count++;
            }
        }
      else
        {
          ret = -EPROTO;
        }

      if (ret < 0 || eof)
        {
          break;
        }
    }

  if (ret == 0 && fields != BKVOICE_REQUIRED_FIELDS)
    {
      ret = -ENODATA;
    }

  if (ret == 0 && clip_count == 0)
    {
      ret = -ENODATA;
    }

  if (ret == 0 && selected_clip != NULL && !selected_found)
    {
      ret = -ENOENT;
    }

  if (ret == 0 && selected_clip != NULL)
    {
      ret = bkvoice_resolve_path(manifest_path, selected_relative,
                                 info->clip_path,
                                 sizeof(info->clip_path));
    }

  info->clip_count = clip_count;
  info->error_line = ret < 0 ? line_number : 0;
  if (close(fd) < 0 && ret == 0)
    {
      ret = bkvoice_errno();
    }

  return ret;
}

int bkvoice_wav_parse(int fd, struct bkvoice_wav_info_s *info)
{
  struct stat status;
  uint8_t riff[12];
  uint64_t riff_end;
  bool format_found = false;
  unsigned int chunk;
  int ret;

  if (fd < 0 || info == NULL)
    {
      return -EINVAL;
    }

  memset(info, 0, sizeof(*info));
  if (fstat(fd, &status) < 0)
    {
      return bkvoice_errno();
    }

  if (!S_ISREG(status.st_mode) || status.st_size < (off_t)sizeof(riff))
    {
      return -EINVAL;
    }

  if (lseek(fd, 0, SEEK_SET) < 0)
    {
      return bkvoice_errno();
    }

  ret = bkvoice_read_exact(fd, riff, sizeof(riff));
  if (ret < 0)
    {
      return ret;
    }

  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0 ||
      bkvoice_le32(riff + 4) < 4u)
    {
      return -EPROTO;
    }

  riff_end = (uint64_t)bkvoice_le32(riff + 4) + 8u;
  if (riff_end > (uint64_t)status.st_size)
    {
      return -ENODATA;
    }

  for (chunk = 0; chunk < BKVOICE_MAX_CHUNKS; chunk++)
    {
      uint8_t header[8];
      uint32_t chunk_size;
      off_t chunk_offset;
      uint64_t chunk_end;

      ret = bkvoice_read_exact(fd, header, sizeof(header));
      if (ret < 0)
        {
          return ret;
        }

      chunk_size = bkvoice_le32(header + 4);
      chunk_offset = lseek(fd, 0, SEEK_CUR);
      if (chunk_offset < 0)
        {
          return bkvoice_errno();
        }

      chunk_end = (uint64_t)chunk_offset + chunk_size + (chunk_size & 1u);
      if (chunk_end > riff_end)
        {
          return -ENODATA;
        }

      if (memcmp(header, "fmt ", 4) == 0)
        {
          uint8_t format[16];

          if (format_found || chunk_size < sizeof(format))
            {
              return -EPROTO;
            }

          ret = bkvoice_read_exact(fd, format, sizeof(format));
          if (ret < 0)
            {
              return ret;
            }

          if (bkvoice_le16(format) != 1u ||
              bkvoice_le16(format + 2) != BKVOICE_CHANNELS ||
              bkvoice_le32(format + 4) != BKVOICE_SAMPLE_RATE ||
              bkvoice_le32(format + 8) !=
                BKVOICE_SAMPLE_RATE * (BKVOICE_BITS_PER_SAMPLE / 8u) ||
              bkvoice_le16(format + 12) !=
                BKVOICE_CHANNELS * (BKVOICE_BITS_PER_SAMPLE / 8u) ||
              bkvoice_le16(format + 14) != BKVOICE_BITS_PER_SAMPLE)
            {
              return -ENOTSUP;
            }

          format_found = true;
        }
      else if (memcmp(header, "data", 4) == 0)
        {
          if (!format_found || chunk_size == 0 || (chunk_size & 1u) != 0)
            {
              return -EPROTO;
            }

          if (chunk_size > BKVOICE_MAX_DATA_BYTES)
            {
              return -EFBIG;
            }

          info->data_offset = chunk_offset;
          info->data_bytes = chunk_size;
          info->sample_rate = BKVOICE_SAMPLE_RATE;
          info->channels = BKVOICE_CHANNELS;
          info->bits_per_sample = BKVOICE_BITS_PER_SAMPLE;
          if (lseek(fd, chunk_offset, SEEK_SET) < 0)
            {
              return bkvoice_errno();
            }

          return 0;
        }

      if (lseek(fd, (off_t)chunk_end, SEEK_SET) < 0)
        {
          return bkvoice_errno();
        }
    }

  return -E2BIG;
}
