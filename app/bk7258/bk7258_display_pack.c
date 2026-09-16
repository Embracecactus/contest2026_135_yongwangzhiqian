/****************************************************************************
 * app/bk7258/bk7258_display_pack.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Strict, bounded and host-testable shaniu-eye-pack-v1 reader.
 ****************************************************************************/

#include "bk7258_display_pack.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __NuttX__
#  include <syslog.h>
#  define BKDISPLAY_PACK_DIAG(...) syslog(LOG_INFO, __VA_ARGS__)
#else
#  define BKDISPLAY_PACK_DIAG(...) do { } while (0)
#endif

#define BKDISPLAY_HEADER_SIZE          128u
#define BKDISPLAY_ENTRY_SIZE            64u
#define BKDISPLAY_MAX_ENTRIES           64u
#define BKDISPLAY_IO_BYTES            1024u
#define BKDISPLAY_PACK_VERSION           1u
#define BKDISPLAY_RENDERER_API           1u
#define BKDISPLAY_KIND_PALETTE           1u
#define BKDISPLAY_KIND_INDEXED_FRAME     2u
#define BKDISPLAY_CODEC_RAW              0u
#define BKDISPLAY_CODEC_RLE8             1u
#define BKDISPLAY_PIXEL_RGB565LE         1u
#define BKDISPLAY_PIXEL_INDEX8           2u
#define BKDISPLAY_ENTRY_MIRROR_RIGHT     1u

struct bkdisplay_entry_s
{
  char name[32];
  uint8_t kind;
  uint8_t codec;
  uint8_t pixel_format;
  uint8_t side;
  uint16_t flags;
  uint16_t width;
  uint16_t height;
  uint16_t palette_count;
  uint32_t offset;
  uint32_t stored_size;
  uint32_t decoded_size;
  uint32_t crc32;
};

struct bkdisplay_pack_s
{
  int fd;
  struct bkdisplay_pack_info_s info;
  uint16_t palette[256];
  struct bkdisplay_entry_s entries[BKDISPLAY_MAX_ENTRIES];
};

struct bkdisplay_expression_s
{
  char id[BKDISPLAY_EXPRESSION_SIZE];
  uint8_t sides;
};

static int bkdisplay_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static uint16_t bkdisplay_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t bkdisplay_le32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint32_t bkdisplay_crc32_update(uint32_t state,
                                       const uint8_t *data, size_t size)
{
  size_t index;

  for (index = 0; index < size; index++)
    {
      unsigned int bit;

      state ^= data[index];
      for (bit = 0; bit < 8; bit++)
        {
          state = (state >> 1) ^
                  (0xedb88320u & (uint32_t)-(int32_t)(state & 1u));
        }
    }

  return state;
}

static int bkdisplay_read_exact(int fd, void *buffer, size_t size)
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

          return bkdisplay_errno();
        }

      if (nread == 0)
        {
          return -ENODATA;
        }

      done += (size_t)nread;
    }

  return 0;
}

static int bkdisplay_seek(int fd, uint32_t offset)
{
  return lseek(fd, (off_t)offset, SEEK_SET) == (off_t)offset ? 0 :
         bkdisplay_errno();
}

static uint32_t bkdisplay_align4(uint32_t value)
{
  return (value + 3u) & ~3u;
}

static bool bkdisplay_pack_id_char(unsigned char byte, bool first)
{
  if (byte >= 'a' && byte <= 'z')
    {
      return true;
    }

  if (byte >= '0' && byte <= '9')
    {
      return true;
    }

  return !first && (byte == '.' || byte == '_' || byte == '-');
}

static bool bkdisplay_entry_char(unsigned char byte, bool first)
{
  if (byte >= 'a' && byte <= 'z')
    {
      return true;
    }

  if (!first && byte >= '0' && byte <= '9')
    {
      return true;
    }

  return !first && (byte == '_' || byte == '.' || byte == '/' ||
                    byte == '-');
}

static int bkdisplay_padded_ascii(const uint8_t *source, size_t size,
                                  char *target, size_t capacity,
                                  bool pack_id)
{
  size_t length = 0;
  size_t index;

  while (length < size && source[length] != 0)
    {
      bool valid = pack_id ?
        bkdisplay_pack_id_char(source[length], length == 0) :
        bkdisplay_entry_char(source[length], length == 0);

      if (!valid)
        {
          return -EPROTO;
        }

      length++;
    }

  if (length == 0 || length == size || length + 1 > capacity)
    {
      return -EPROTO;
    }

  for (index = length + 1; index < size; index++)
    {
      if (source[index] != 0)
        {
          return -EPROTO;
        }
    }

  memcpy(target, source, length);
  target[length] = '\0';
  return 0;
}

static bool bkdisplay_expression_id(const char *id)
{
  size_t length = 0;

  if (id == NULL || id[0] < 'a' || id[0] > 'z')
    {
      return false;
    }

  while (id[length] != '\0')
    {
      unsigned char byte = (unsigned char)id[length];

      if (!((byte >= 'a' && byte <= 'z') ||
            (length > 0 && byte >= '0' && byte <= '9') ||
            (length > 0 && (byte == '_' || byte == '-'))))
        {
          return false;
        }

      length++;
      if (length >= BKDISPLAY_EXPRESSION_SIZE)
        {
          return false;
        }
    }

  return length > 0;
}

static int bkdisplay_crc_region(int fd, uint32_t offset, uint32_t size,
                                uint32_t *result)
{
  uint8_t buffer[BKDISPLAY_IO_BYTES];
  uint32_t remaining = size;
  uint32_t state = 0xffffffffu;
  int ret;

  ret = bkdisplay_seek(fd, offset);
  if (ret < 0)
    {
      return ret;
    }

  while (remaining > 0)
    {
      size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

      ret = bkdisplay_read_exact(fd, buffer, chunk);
      if (ret < 0)
        {
          return ret;
        }

      state = bkdisplay_crc32_update(state, buffer, chunk);
      remaining -= (uint32_t)chunk;
    }

  *result = state ^ 0xffffffffu;
  return 0;
}

static int bkdisplay_zero_region(int fd, uint32_t offset, uint32_t size)
{
  uint8_t buffer[64];
  uint32_t remaining = size;
  int ret;

  ret = bkdisplay_seek(fd, offset);
  if (ret < 0)
    {
      return ret;
    }

  while (remaining > 0)
    {
      size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
      size_t index;

      ret = bkdisplay_read_exact(fd, buffer, chunk);
      if (ret < 0)
        {
          return ret;
        }

      for (index = 0; index < chunk; index++)
        {
          if (buffer[index] != 0)
            {
              return -EPROTO;
            }
        }

      remaining -= (uint32_t)chunk;
    }

  return 0;
}

static int bkdisplay_decode(struct bkdisplay_pack_s *pack,
                            const struct bkdisplay_entry_s *entry,
                            bool mirror, uint16_t *pixels)
{
  uint8_t buffer[BKDISPLAY_IO_BYTES];
  uint32_t decoded = 0;
  uint32_t remaining = entry->stored_size;
  uint32_t state = 0xffffffffu;
  int ret;

  ret = bkdisplay_seek(pack->fd, entry->offset);
  if (ret < 0)
    {
      return ret;
    }

  while (remaining > 0)
    {
      size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
      size_t index;

      ret = bkdisplay_read_exact(pack->fd, buffer, chunk);
      if (ret < 0)
        {
          return ret;
        }

      if (entry->codec == BKDISPLAY_CODEC_RAW)
        {
          state = bkdisplay_crc32_update(state, buffer, chunk);
          for (index = 0; index < chunk; index++)
            {
              uint8_t palette_index = buffer[index];

              if (palette_index >= pack->info.palette_count ||
                  decoded >= entry->decoded_size)
                {
                  return -EPROTO;
                }

              if (pixels != NULL)
                {
                  uint32_t target = decoded;

                  if (mirror)
                    {
                      uint32_t row = decoded / entry->width;
                      uint32_t column = decoded % entry->width;

                      target = row * entry->width + entry->width - 1u - column;
                    }

                  pixels[target] = pack->palette[palette_index];
                }

              decoded++;
            }
        }
      else
        {
          if ((chunk & 1u) != 0)
            {
              return -EPROTO;
            }

          for (index = 0; index < chunk; index += 2)
            {
              uint8_t count = buffer[index];
              uint8_t palette_index = buffer[index + 1];
              unsigned int run;

              if (count == 0 || palette_index >= pack->info.palette_count ||
                  decoded + count > entry->decoded_size)
                {
                  return -EPROTO;
                }

              for (run = 0; run < count; run++)
                {
                  uint32_t target = decoded;

                  state = bkdisplay_crc32_update(state, &palette_index, 1);
                  if (pixels != NULL)
                    {
                      if (mirror)
                        {
                          uint32_t row = decoded / entry->width;
                          uint32_t column = decoded % entry->width;

                          target = row * entry->width + entry->width - 1u -
                                   column;
                        }

                      pixels[target] = pack->palette[palette_index];
                    }

                  decoded++;
                }
            }
        }

      remaining -= (uint32_t)chunk;
    }

  if (decoded != entry->decoded_size ||
      (state ^ 0xffffffffu) != entry->crc32)
    {
      return -EBADMSG;
    }

  return 0;
}

static int bkdisplay_frame_identity(const struct bkdisplay_entry_s *entry,
                                    char *id, uint8_t *side)
{
  static const char prefix[] = "expression/";
  const char *suffix;
  const char *end;
  size_t length;

  if (strncmp(entry->name, prefix, sizeof(prefix) - 1u) != 0)
    {
      return -EPROTO;
    }

  suffix = entry->name + sizeof(prefix) - 1u;
  end = strchr(suffix, '/');
  length = end == NULL ? strlen(suffix) : (size_t)(end - suffix);
  if (length == 0 || length >= BKDISPLAY_EXPRESSION_SIZE)
    {
      return -EPROTO;
    }

  memcpy(id, suffix, length);
  id[length] = '\0';
  if (!bkdisplay_expression_id(id))
    {
      return -EPROTO;
    }

  if (entry->side == BKDISPLAY_SIDE_UNMAPPED)
    {
      if (end != NULL)
        {
          return -EPROTO;
        }
    }
  else
    {
      const char *expected = entry->side == BKDISPLAY_SIDE_LEFT ?
                             "left" : "right";

      if (end == NULL || strcmp(end + 1, expected) != 0 ||
          strchr(end + 1, '/') != NULL)
        {
          return -EPROTO;
        }
    }

  *side = entry->side;
  return 0;
}

static int bkdisplay_parse_entry(const uint8_t *raw,
                                 struct bkdisplay_entry_s *entry)
{
  int ret;

  memset(entry, 0, sizeof(*entry));
  ret = bkdisplay_padded_ascii(raw, 32, entry->name, sizeof(entry->name),
                               false);
  if (ret < 0)
    {
      return ret;
    }

  entry->kind = raw[32];
  entry->codec = raw[33];
  entry->pixel_format = raw[34];
  entry->side = raw[35];
  entry->flags = bkdisplay_le16(raw + 36);
  entry->width = bkdisplay_le16(raw + 38);
  entry->height = bkdisplay_le16(raw + 40);
  entry->palette_count = bkdisplay_le16(raw + 42);
  entry->offset = bkdisplay_le32(raw + 44);
  entry->stored_size = bkdisplay_le32(raw + 48);
  entry->decoded_size = bkdisplay_le32(raw + 52);
  entry->crc32 = bkdisplay_le32(raw + 56);

  if (bkdisplay_le32(raw + 60) != 0 || entry->stored_size == 0 ||
      entry->decoded_size == 0 || entry->side > BKDISPLAY_SIDE_RIGHT ||
      (entry->flags & ~BKDISPLAY_ENTRY_MIRROR_RIGHT) != 0 ||
      (entry->flags != 0 && entry->side != BKDISPLAY_SIDE_UNMAPPED))
    {
      return -EPROTO;
    }

  return 0;
}

static int bkdisplay_validate_entries(struct bkdisplay_pack_s *pack,
                                      const uint8_t *toc,
                                      uint32_t payload_offset)
{
  struct bkdisplay_expression_s expressions[BKDISPLAY_MAX_ENTRIES - 1u];
  char last_frame[32] = "";
  uint32_t cursor = payload_offset;
  unsigned int expression_count = 0;
  bool neutral = false;
  unsigned int index;
  int ret;

  memset(expressions, 0, sizeof(expressions));
  for (index = 0; index < pack->info.entry_count; index++)
    {
      struct bkdisplay_entry_s *entry = &pack->entries[index];
      uint32_t aligned;
      unsigned int previous;

      ret = bkdisplay_parse_entry(toc + index * BKDISPLAY_ENTRY_SIZE, entry);
      if (ret < 0)
        {
          return ret;
        }

      for (previous = 0; previous < index; previous++)
        {
          if (strcmp(pack->entries[previous].name, entry->name) == 0)
            {
              return -EEXIST;
            }
        }

      aligned = bkdisplay_align4(cursor);
      if (entry->offset != aligned ||
          (uint64_t)entry->offset + entry->stored_size >
          pack->info.total_size)
        {
          return -EPROTO;
        }

      ret = bkdisplay_zero_region(pack->fd, cursor, aligned - cursor);
      if (ret < 0)
        {
          return ret;
        }

      if (index == 0)
        {
          uint8_t palette[512];
          unsigned int color;

          if (strcmp(entry->name, "palette/default") != 0 ||
              entry->kind != BKDISPLAY_KIND_PALETTE ||
              entry->codec != BKDISPLAY_CODEC_RAW ||
              entry->pixel_format != BKDISPLAY_PIXEL_RGB565LE ||
              entry->side != BKDISPLAY_SIDE_UNMAPPED || entry->flags != 0 ||
              entry->width != 0 || entry->height != 0 ||
              entry->palette_count < 2 || entry->palette_count > 256 ||
              entry->stored_size != entry->decoded_size ||
              entry->decoded_size != (uint32_t)entry->palette_count * 2u)
            {
              return -EPROTO;
            }

          ret = bkdisplay_seek(pack->fd, entry->offset);
          if (ret < 0)
            {
              return ret;
            }

          ret = bkdisplay_read_exact(pack->fd, palette,
                                     entry->decoded_size);
          if (ret < 0)
            {
              return ret;
            }

          if ((bkdisplay_crc32_update(0xffffffffu, palette,
                                      entry->decoded_size) ^ 0xffffffffu) !=
              entry->crc32)
            {
              return -EBADMSG;
            }

          pack->info.palette_count = entry->palette_count;
          for (color = 0; color < entry->palette_count; color++)
            {
              pack->palette[color] = bkdisplay_le16(palette + color * 2u);
            }
        }
      else
        {
          char expression[BKDISPLAY_EXPRESSION_SIZE];
          uint8_t side;
          unsigned int found;

          if (entry->kind != BKDISPLAY_KIND_INDEXED_FRAME ||
              (entry->codec != BKDISPLAY_CODEC_RAW &&
               entry->codec != BKDISPLAY_CODEC_RLE8) ||
              entry->pixel_format != BKDISPLAY_PIXEL_INDEX8 ||
              entry->width != pack->info.width ||
              entry->height != pack->info.height ||
              entry->palette_count != 0 ||
              entry->decoded_size != BKDISPLAY_CANVAS_PIXELS ||
              (entry->codec == BKDISPLAY_CODEC_RAW &&
               entry->stored_size != entry->decoded_size) ||
              (entry->codec == BKDISPLAY_CODEC_RLE8 &&
               ((entry->stored_size & 1u) != 0 ||
                entry->stored_size > entry->decoded_size * 2u)) ||
              strcmp(entry->name, last_frame) <= 0)
            {
              return -EPROTO;
            }

          ret = bkdisplay_frame_identity(entry, expression, &side);
          if (ret < 0)
            {
              return ret;
            }

          snprintf(last_frame, sizeof(last_frame), "%s", entry->name);
          neutral = neutral || strcmp(expression, "neutral") == 0;
          for (found = 0; found < expression_count; found++)
            {
              if (strcmp(expressions[found].id, expression) == 0)
                {
                  break;
                }
            }

          if (found == expression_count)
            {
              if (expression_count >= BKDISPLAY_MAX_ENTRIES - 1u)
                {
                  return -E2BIG;
                }

              snprintf(expressions[found].id, sizeof(expressions[found].id),
                       "%s", expression);
              expression_count++;
            }

          if ((expressions[found].sides & (1u << side)) != 0)
            {
              return -EEXIST;
            }

          expressions[found].sides |= (uint8_t)(1u << side);
          ret = bkdisplay_decode(pack, entry, false, NULL);
          if (ret < 0)
            {
              return ret;
            }
        }

      cursor = entry->offset + entry->stored_size;
    }

  if (cursor != pack->info.total_size || !neutral)
    {
      return -EPROTO;
    }

  for (index = 0; index < expression_count; index++)
    {
      uint8_t shared = (uint8_t)(1u << BKDISPLAY_SIDE_UNMAPPED);
      uint8_t pair = (uint8_t)((1u << BKDISPLAY_SIDE_LEFT) |
                               (1u << BKDISPLAY_SIDE_RIGHT));

      if (expressions[index].sides != shared &&
          expressions[index].sides != pair)
        {
          return -EPROTO;
        }
    }

  return 0;
}

int bkdisplay_pack_open(const char *path, struct bkdisplay_pack_s **result,
                        struct bkdisplay_pack_info_s *info)
{
  static const uint8_t magic[8] = {'S', 'H', 'N', 'E', 'Y', 'E', '1', 0};
  struct bkdisplay_pack_s *pack = NULL;
  struct stat statbuf;
  uint8_t header[BKDISPLAY_HEADER_SIZE];
  uint8_t *toc = NULL;
  uint32_t toc_size;
  uint32_t payload_offset;
  uint32_t crc;
  unsigned int index;
  int fd = -1;
  int ret;

  if (path == NULL || *path == '\0' || result == NULL)
    {
      return -EINVAL;
    }

  *result = NULL;
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      ret = bkdisplay_errno();
      if (ret != -ENOENT)
        {
          BKDISPLAY_PACK_DIAG(
            "BKDISPLAY PACK stage=open path=%s ret=%d\n", path, ret);
        }

      return ret;
    }

  if (fstat(fd, &statbuf) < 0)
    {
      ret = bkdisplay_errno();
      BKDISPLAY_PACK_DIAG(
        "BKDISPLAY PACK stage=fstat path=%s ret=%d\n", path, ret);
      goto out;
    }

  if (!S_ISREG(statbuf.st_mode) || statbuf.st_size < BKDISPLAY_HEADER_SIZE ||
      statbuf.st_size > BKDISPLAY_MAX_PACK_BYTES)
    {
      BKDISPLAY_PACK_DIAG(
        "BKDISPLAY PACK stage=stat-check path=%s mode=%lo size=%lld ret=%d\n",
        path, (unsigned long)statbuf.st_mode,
        (long long)statbuf.st_size, -EINVAL);
      ret = -EINVAL;
      goto out;
    }

  ret = bkdisplay_read_exact(fd, header, sizeof(header));
  if (ret < 0)
    {
      goto out;
    }

  pack = calloc(1, sizeof(*pack));
  if (pack == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  pack->fd = fd;
  if (memcmp(header, magic, sizeof(magic)) != 0 ||
      bkdisplay_le16(header + 8) != BKDISPLAY_PACK_VERSION ||
      bkdisplay_le16(header + 10) != BKDISPLAY_HEADER_SIZE ||
      bkdisplay_le16(header + 12) != BKDISPLAY_ENTRY_SIZE)
    {
      ret = -EPROTONOSUPPORT;
      goto out;
    }

  pack->info.entry_count = bkdisplay_le16(header + 14);
  pack->info.width = bkdisplay_le16(header + 16);
  pack->info.height = bkdisplay_le16(header + 18);
  pack->info.renderer_api = bkdisplay_le16(header + 20);
  pack->info.revision = bkdisplay_le32(header + 24);
  pack->info.total_size = bkdisplay_le32(header + 36);
  memcpy(pack->info.source_sha256, header + 80,
         sizeof(pack->info.source_sha256));

  if (pack->info.entry_count < 2 ||
      pack->info.entry_count > BKDISPLAY_MAX_ENTRIES ||
      pack->info.width != BKDISPLAY_CANVAS_WIDTH ||
      pack->info.height != BKDISPLAY_CANVAS_HEIGHT ||
      pack->info.renderer_api != BKDISPLAY_RENDERER_API ||
      bkdisplay_le16(header + 22) != 0 || pack->info.revision == 0 ||
      bkdisplay_le32(header + 28) != BKDISPLAY_HEADER_SIZE ||
      pack->info.total_size != (uint32_t)statbuf.st_size)
    {
      ret = -EPROTO;
      goto out;
    }

  for (index = 0; index < 16; index++)
    {
      if (header[112 + index] != 0)
        {
          ret = -EPROTO;
          goto out;
        }
    }

  for (index = 0; index < sizeof(pack->info.source_sha256); index++)
    {
      if (pack->info.source_sha256[index] != 0)
        {
          break;
        }
    }

  if (index == sizeof(pack->info.source_sha256))
    {
      ret = -EPROTO;
      goto out;
    }

  ret = bkdisplay_padded_ascii(header + 48, 32, pack->info.pack_id,
                               sizeof(pack->info.pack_id), true);
  if (ret < 0)
    {
      goto out;
    }

  toc_size = (uint32_t)pack->info.entry_count * BKDISPLAY_ENTRY_SIZE;
  payload_offset = bkdisplay_align4(BKDISPLAY_HEADER_SIZE + toc_size);
  if (bkdisplay_le32(header + 32) != payload_offset ||
      payload_offset >= pack->info.total_size)
    {
      ret = -EPROTO;
      goto out;
    }

  toc = malloc(toc_size);
  if (toc == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  ret = bkdisplay_seek(fd, BKDISPLAY_HEADER_SIZE);
  if (ret < 0 || (ret = bkdisplay_read_exact(fd, toc, toc_size)) < 0)
    {
      goto out;
    }

  crc = bkdisplay_crc32_update(0xffffffffu, toc, toc_size) ^ 0xffffffffu;
  if (crc != bkdisplay_le32(header + 40))
    {
      ret = -EBADMSG;
      goto out;
    }

  ret = bkdisplay_zero_region(fd, BKDISPLAY_HEADER_SIZE + toc_size,
                              payload_offset -
                              (BKDISPLAY_HEADER_SIZE + toc_size));
  if (ret < 0)
    {
      goto out;
    }

  ret = bkdisplay_crc_region(fd, payload_offset,
                             pack->info.total_size - payload_offset, &crc);
  if (ret < 0 || crc != bkdisplay_le32(header + 44))
    {
      ret = ret < 0 ? ret : -EBADMSG;
      goto out;
    }

  ret = bkdisplay_validate_entries(pack, toc, payload_offset);
  if (ret < 0)
    {
      goto out;
    }

  if (info != NULL)
    {
      *info = pack->info;
    }

  free(toc);
  *result = pack;
  return 0;

out:
  free(toc);
  free(pack);
  close(fd);
  return ret;
}

void bkdisplay_pack_close(struct bkdisplay_pack_s *pack)
{
  if (pack != NULL)
    {
      if (pack->fd >= 0)
        {
          close(pack->fd);
        }

      free(pack);
    }
}

int bkdisplay_pack_render(struct bkdisplay_pack_s *pack,
                          const char *expression,
                          enum bkdisplay_side_e side,
                          uint16_t *pixels, size_t pixel_count)
{
  char shared_name[32];
  char side_name[32];
  const struct bkdisplay_entry_s *shared = NULL;
  const struct bkdisplay_entry_s *specific = NULL;
  const struct bkdisplay_entry_s *selected;
  unsigned int index;
  bool mirror;

  if (pack == NULL || !bkdisplay_expression_id(expression) ||
      side > BKDISPLAY_SIDE_RIGHT || pixels == NULL ||
      pixel_count < BKDISPLAY_CANVAS_PIXELS)
    {
      return -EINVAL;
    }

  if (snprintf(shared_name, sizeof(shared_name), "expression/%s",
               expression) >= (int)sizeof(shared_name))
    {
      return -ENAMETOOLONG;
    }

  side_name[0] = '\0';
  if (side != BKDISPLAY_SIDE_UNMAPPED &&
      snprintf(side_name, sizeof(side_name), "expression/%s/%s",
               expression, side == BKDISPLAY_SIDE_LEFT ? "left" : "right") >=
      (int)sizeof(side_name))
    {
      return -ENAMETOOLONG;
    }

  for (index = 1; index < pack->info.entry_count; index++)
    {
      if (strcmp(pack->entries[index].name, shared_name) == 0)
        {
          shared = &pack->entries[index];
        }

      if (side_name[0] != '\0' &&
          strcmp(pack->entries[index].name, side_name) == 0)
        {
          specific = &pack->entries[index];
        }
    }

  if (side == BKDISPLAY_SIDE_UNMAPPED)
    {
      selected = shared;
    }
  else
    {
      selected = specific != NULL ? specific : shared;
    }

  if (selected == NULL)
    {
      return side == BKDISPLAY_SIDE_UNMAPPED ? -EADDRNOTAVAIL : -ENOENT;
    }

  mirror = side == BKDISPLAY_SIDE_RIGHT && selected == shared &&
           (selected->flags & BKDISPLAY_ENTRY_MIRROR_RIGHT) != 0;
  return bkdisplay_decode(pack, selected, mirror, pixels);
}
