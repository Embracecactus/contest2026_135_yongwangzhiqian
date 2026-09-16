/****************************************************************************
 * app/bk7258/bk7258_display_store.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Strict staging -> packs -> active.json transaction for a mounted FAT
 * volume.  USB/block-device ownership is intentionally handled above this
 * portable layer.
 ****************************************************************************/

#include "bk7258_display_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __NuttX__
#  include <syslog.h>
#  define BKDISPLAY_STORE_DIAG(...) syslog(LOG_INFO, __VA_ARGS__)
#else
#  define BKDISPLAY_STORE_DIAG(...) do { } while (0)
#endif

#define BKDISPLAY_STORE_BASE       "shaniu/display"
#define BKDISPLAY_STORE_PACKS      BKDISPLAY_STORE_BASE "/packs"
#define BKDISPLAY_STORE_STAGING    BKDISPLAY_STORE_BASE "/staging"
#define BKDISPLAY_STORE_ACTIVE     BKDISPLAY_STORE_BASE "/active.json"
#define BKDISPLAY_STORE_ACTIVE_TMP BKDISPLAY_STORE_BASE "/.active.json.tmp"
#define BKDISPLAY_STORE_LEGACY_BASE   "SHANIU/DISPLAY"
#define BKDISPLAY_STORE_LEGACY_PACKS  BKDISPLAY_STORE_LEGACY_BASE "/PACKS"
#define BKDISPLAY_STORE_LEGACY_ACTIVE BKDISPLAY_STORE_LEGACY_BASE "/active.json"
#define BKDISPLAY_ACTIVE_PREFIX    \
  "{\"format\":\"shaniu-display-active/1\",\"pack\":\""
#define BKDISPLAY_ACTIVE_SUFFIX    "\"}\n"
#define BKDISPLAY_ACTIVE_MAX       128u

static int bkdisplay_store_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static int bkdisplay_store_path(char *path, size_t capacity,
                                const char *root, const char *relative)
{
  size_t length;
  int written;

  if (path == NULL || capacity == 0 || root == NULL || root[0] != '/' ||
      relative == NULL || *relative == '\0')
    {
      return -EINVAL;
    }

  length = strlen(root);
  while (length > 1 && root[length - 1] == '/')
    {
      length--;
    }

  written = snprintf(path, capacity, "%.*s/%s", (int)length, root,
                     relative);
  return written < 0 || (size_t)written >= capacity ? -ENAMETOOLONG : 0;
}

static int bkdisplay_store_directory(const char *path)
{
  struct stat statbuf;

  if (mkdir(path, 0775) == 0)
    {
      return 0;
    }

  if (errno != EEXIST)
    {
      return bkdisplay_store_errno();
    }

  if (stat(path, &statbuf) < 0)
    {
      return bkdisplay_store_errno();
    }

  return S_ISDIR(statbuf.st_mode) ? 0 : -ENOTDIR;
}

static bool bkdisplay_store_filename(const char *filename)
{
  static const char suffix[] = ".bkep";
  size_t length = 0;

  if (filename == NULL || filename[0] < 'a' || filename[0] > 'z')
    {
      return false;
    }

  while (filename[length] != '\0')
    {
      unsigned char byte = (unsigned char)filename[length];

      if (!((byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
            byte == '-'))
        {
          return false;
        }

      length++;
      if (length >= BKDISPLAY_STORE_FILENAME_SIZE)
        {
          return false;
        }
    }

  return length > sizeof(suffix) - 1u &&
         strcmp(filename + length - (sizeof(suffix) - 1u), suffix) == 0;
}

static int bkdisplay_store_validate(const char *path, const char *filename,
                                    struct bkdisplay_store_selection_s *result,
                                    bool fallback)
{
  struct bkdisplay_pack_info_s info;
  struct bkdisplay_pack_s *pack = NULL;
  char expected[BKDISPLAY_STORE_FILENAME_SIZE];
  int written;
  int ret;

  ret = bkdisplay_pack_open(path, &pack, &info);
  if (ret < 0)
    {
      return ret;
    }

  written = snprintf(expected, sizeof(expected), "%s.bkep", info.pack_id);
  if (written < 0 || (size_t)written >= sizeof(expected) ||
      strcmp(expected, filename) != 0)
    {
      ret = -EPROTO;
    }

  if (ret == 0 && result != NULL)
    {
      memset(result, 0, sizeof(*result));
      snprintf(result->filename, sizeof(result->filename), "%s", filename);
      snprintf(result->path, sizeof(result->path), "%s", path);
      result->fallback = fallback;
      result->info = info;
    }

  bkdisplay_pack_close(pack);
  return ret;
}

static int bkdisplay_store_write_all(int fd, const void *buffer, size_t size)
{
  const unsigned char *cursor = buffer;
  size_t done = 0;

  while (done < size)
    {
      ssize_t written = write(fd, cursor + done, size - done);

      if (written < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return bkdisplay_store_errno();
        }

      if (written == 0)
        {
          return -EIO;
        }

      done += (size_t)written;
    }

  return 0;
}

static int bkdisplay_store_read_active(const char *path, char *filename,
                                       size_t capacity)
{
  char data[BKDISPLAY_ACTIVE_MAX];
  size_t prefix = strlen(BKDISPLAY_ACTIVE_PREFIX);
  size_t suffix = strlen(BKDISPLAY_ACTIVE_SUFFIX);
  ssize_t nread;
  int read_errno = 0;
  size_t length;
  size_t name_length;
  int fd;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return bkdisplay_store_errno();
    }

  do
    {
      nread = read(fd, data, sizeof(data));
    }
  while (nread < 0 && errno == EINTR);

  if (nread < 0)
    {
      read_errno = errno;
    }

  close(fd);
  if (nread < 0)
    {
      return read_errno > 0 ? -read_errno : -EIO;
    }

  length = (size_t)nread;
  if (length >= sizeof(data) || length <= prefix + suffix ||
      memcmp(data, BKDISPLAY_ACTIVE_PREFIX, prefix) != 0 ||
      memcmp(data + length - suffix, BKDISPLAY_ACTIVE_SUFFIX, suffix) != 0)
    {
      return -EPROTO;
    }

  name_length = length - prefix - suffix;
  if (name_length + 1u > capacity)
    {
      return -ENAMETOOLONG;
    }

  memcpy(filename, data + prefix, name_length);
  filename[name_length] = '\0';
  return bkdisplay_store_filename(filename) ? 0 : -EPROTO;
}

int bkdisplay_store_ensure(const char *root)
{
  static const char *const directories[] =
  {
    "shaniu",
    BKDISPLAY_STORE_BASE,
    BKDISPLAY_STORE_PACKS,
    BKDISPLAY_STORE_STAGING,
  };
  char path[BKDISPLAY_PACK_PATH_SIZE];
  unsigned int index;
  int ret;

  for (index = 0; index < sizeof(directories) / sizeof(directories[0]);
       index++)
    {
      ret = bkdisplay_store_path(path, sizeof(path), root,
                                 directories[index]);
      if (ret < 0)
        {
          return ret;
        }

      ret = bkdisplay_store_directory(path);
      if (ret < 0)
        {
          return ret;
        }
    }

  return 0;
}

static int bkdisplay_store_resolve_layout(
  const char *root, const char *active_relative, const char *packs_relative,
  struct bkdisplay_store_selection_s *selection, bool *fallback_result)
{
  char active[BKDISPLAY_PACK_PATH_SIZE];
  char pack[BKDISPLAY_PACK_PATH_SIZE];
  char filename[BKDISPLAY_STORE_FILENAME_SIZE];
  bool fallback = false;
  int ret;

  if (selection == NULL || fallback_result == NULL)
    {
      return -EINVAL;
    }

  *fallback_result = false;

  ret = bkdisplay_store_path(active, sizeof(active), root,
                             active_relative);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_store_read_active(active, filename, sizeof(filename));
  if (ret < 0 && ret != -ENOENT)
    {
      BKDISPLAY_STORE_DIAG(
        "BKDISPLAY STORE stage=active-read path=%s ret=%d\n", active, ret);
    }

  if (ret == -ENOENT)
    {
      snprintf(filename, sizeof(filename), "%s",
               BKDISPLAY_STORE_DEFAULT_PACK);
      fallback = true;
      *fallback_result = true;
    }
  else if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_store_path(pack, sizeof(pack), root,
                             packs_relative);
  if (ret < 0)
    {
      return ret;
    }

  if (snprintf(active, sizeof(active), "%s/%s", pack, filename) >=
      (int)sizeof(active))
    {
      return -ENAMETOOLONG;
    }

  ret = bkdisplay_store_validate(active, filename, selection, fallback);
  if (ret < 0 && ret != -ENOENT)
    {
      BKDISPLAY_STORE_DIAG(
        "BKDISPLAY STORE stage=pack-validate path=%s fallback=%u ret=%d\n",
        active, fallback ? 1u : 0u, ret);
    }

  return ret;
}

int bkdisplay_store_resolve(const char *root,
                            struct bkdisplay_store_selection_s *selection)
{
  bool canonical_fallback;
  bool legacy_fallback;
  int ret;

  ret = bkdisplay_store_resolve_layout(root, BKDISPLAY_STORE_ACTIVE,
                                       BKDISPLAY_STORE_PACKS, selection,
                                       &canonical_fallback);
  if (ret != -ENOENT || !canonical_fallback)
    {
      return ret;
    }

  /* Early AIDK media and Windows provisioning may contain a second,
   * case-distinct SHANIU/DISPLAY tree.  NuttX's FAT layer can distinguish
   * that legacy spelling while Windows path lookup selects it
   * case-insensitively.  Keep the canonical lowercase store authoritative,
   * but accept a legacy tree only when the canonical active marker and
   * default pack are both absent.  This preserves fail-closed handling for
   * malformed or explicitly selected canonical content.
   */

  ret = bkdisplay_store_resolve_layout(root, BKDISPLAY_STORE_LEGACY_ACTIVE,
                                       BKDISPLAY_STORE_LEGACY_PACKS,
                                       selection, &legacy_fallback);
  if (ret == 0)
    {
      BKDISPLAY_STORE_DIAG(
        "BKDISPLAY STORE stage=legacy-resolve path=%s fallback=%u\n",
        selection->path, legacy_fallback ? 1u : 0u);
    }

  return ret;
}

int bkdisplay_store_activate(const char *root, const char *filename,
                             struct bkdisplay_store_selection_s *selection)
{
  struct bkdisplay_store_selection_s selected;
  char directory[BKDISPLAY_PACK_PATH_SIZE];
  char path[BKDISPLAY_PACK_PATH_SIZE];
  char marker[BKDISPLAY_ACTIVE_MAX];
  char active[BKDISPLAY_PACK_PATH_SIZE];
  char temporary[BKDISPLAY_PACK_PATH_SIZE];
  int marker_length;
  int fd = -1;
  int ret;

  if (!bkdisplay_store_filename(filename))
    {
      return -EINVAL;
    }

  ret = bkdisplay_store_ensure(root);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_store_path(directory, sizeof(directory), root,
                             BKDISPLAY_STORE_PACKS);
  if (ret < 0)
    {
      return ret;
    }

  if (snprintf(path, sizeof(path), "%s/%s", directory, filename) >=
      (int)sizeof(path))
    {
      return -ENAMETOOLONG;
    }

  ret = bkdisplay_store_validate(path, filename, &selected, false);
  if (ret < 0)
    {
      return ret;
    }

  marker_length = snprintf(marker, sizeof(marker), "%s%s%s",
                           BKDISPLAY_ACTIVE_PREFIX, filename,
                           BKDISPLAY_ACTIVE_SUFFIX);
  if (marker_length < 0 || (size_t)marker_length >= sizeof(marker))
    {
      return -E2BIG;
    }

  ret = bkdisplay_store_path(active, sizeof(active), root,
                             BKDISPLAY_STORE_ACTIVE);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_store_path(temporary, sizeof(temporary), root,
                             BKDISPLAY_STORE_ACTIVE_TMP);
  if (ret < 0)
    {
      return ret;
    }

  (void)unlink(temporary);
  fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0644);
  if (fd < 0)
    {
      return bkdisplay_store_errno();
    }

  ret = bkdisplay_store_write_all(fd, marker, (size_t)marker_length);
  if (ret == 0 && fsync(fd) < 0)
    {
      ret = bkdisplay_store_errno();
    }

  if (close(fd) < 0 && ret == 0)
    {
      ret = bkdisplay_store_errno();
    }

  if (ret == 0 && rename(temporary, active) < 0)
    {
      ret = bkdisplay_store_errno();
    }

  if (ret < 0)
    {
      (void)unlink(temporary);
      return ret;
    }

  if (selection != NULL)
    {
      *selection = selected;
    }

  return 0;
}

int bkdisplay_store_install(const char *root, const char *filename,
                            struct bkdisplay_store_selection_s *selection)
{
  char staging_dir[BKDISPLAY_PACK_PATH_SIZE];
  char packs_dir[BKDISPLAY_PACK_PATH_SIZE];
  char staging[BKDISPLAY_PACK_PATH_SIZE];
  char installed[BKDISPLAY_PACK_PATH_SIZE];
  struct stat statbuf;
  int ret;

  if (!bkdisplay_store_filename(filename))
    {
      return -EINVAL;
    }

  ret = bkdisplay_store_ensure(root);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_store_path(staging_dir, sizeof(staging_dir), root,
                             BKDISPLAY_STORE_STAGING);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_store_path(packs_dir, sizeof(packs_dir), root,
                             BKDISPLAY_STORE_PACKS);
  if (ret < 0)
    {
      return ret;
    }

  if (snprintf(staging, sizeof(staging), "%s/%s", staging_dir, filename) >=
      (int)sizeof(staging) ||
      snprintf(installed, sizeof(installed), "%s/%s", packs_dir, filename) >=
      (int)sizeof(installed))
    {
      return -ENAMETOOLONG;
    }

  ret = bkdisplay_store_validate(staging, filename, NULL, false);
  if (ret < 0)
    {
      return ret;
    }

  if (stat(installed, &statbuf) == 0)
    {
      return -EEXIST;
    }

  if (errno != ENOENT)
    {
      return bkdisplay_store_errno();
    }

  if (rename(staging, installed) < 0)
    {
      return bkdisplay_store_errno();
    }

  return bkdisplay_store_activate(root, filename, selection);
}
