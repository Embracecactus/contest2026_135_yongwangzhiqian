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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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
#define BKDISPLAY_STORE_LEGACY_ROOT   "SHANIU"
#define BKDISPLAY_STORE_LEGACY_BASE   BKDISPLAY_STORE_LEGACY_ROOT "/DISPLAY"
#define BKDISPLAY_STORE_LEGACY_PACKS  BKDISPLAY_STORE_LEGACY_BASE "/PACKS"
#define BKDISPLAY_STORE_LEGACY_ACTIVE BKDISPLAY_STORE_LEGACY_BASE "/active.json"
#define BKDISPLAY_ACTIVE_PREFIX    \
  "{\"format\":\"shaniu-display-active/1\",\"pack\":\""
#define BKDISPLAY_ACTIVE_PREFIX_V2 \
  "{\"format\":\"shaniu-display-active/2\",\"pack\":\""
#define BKDISPLAY_ACTIVE_REVISION "\",\"revision\":\""
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
                                    bool fallback,
                                    struct bkdisplay_pack_s **result_pack)
{
  struct bkdisplay_pack_info_s info;
  struct bkdisplay_pack_s *pack = NULL;
  char expected[BKDISPLAY_STORE_FILENAME_SIZE];
  int written;
  int close_ret;
  int ret;

  if (result_pack != NULL)
    {
      *result_pack = NULL;
    }

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

  if (ret == 0 && result_pack != NULL)
    {
      *result_pack = pack;
      pack = NULL;
    }

  close_ret = bkdisplay_pack_close(pack);
  if (close_ret < 0)
    {
      ret = close_ret;
    }

  if (ret < 0 && result != NULL)
    {
      memset(result, 0, sizeof(*result));
    }

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

/* Require successful directory synchronization and propagate syscall errors.
 * Native NuttX FAT already writes directory changes during rename/unlink;
 * its directory fsync path can return success without a device flush.
 * Even a successful call is not evidence of power-loss durability and must
 * not be used to explain an earlier EIO without storage-level evidence.
 */

static int bkdisplay_store_sync_directory_strict(const char *path)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0) return bkdisplay_store_errno();
  int ret = fsync(fd) < 0 ? bkdisplay_store_errno() : 0;
  if (close(fd) < 0 && ret == 0) ret = bkdisplay_store_errno();
  return ret;
}

static int bkdisplay_store_sync_if_directory(const char *path)
{
  struct stat info;
  if (lstat(path, &info) < 0) return errno == ENOENT ? 0 : bkdisplay_store_errno();
  if (!S_ISDIR(info.st_mode)) return -ENOTDIR;
  return bkdisplay_store_sync_directory_strict(path);
}

int bkdisplay_store_reset_selection(const char *root)
{
  static const char *const names[] =
    {BKDISPLAY_STORE_ACTIVE, BKDISPLAY_STORE_ACTIVE_TMP,
     BKDISPLAY_STORE_LEGACY_ACTIVE};
  char path[BKDISPLAY_PACK_PATH_SIZE];
  int ret;
  if (root == NULL) return -EINVAL;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
      ret = bkdisplay_store_path(path, sizeof(path), root, names[i]);
      if (ret < 0) return ret;
      if (unlink(path) < 0 && errno != ENOENT) return bkdisplay_store_errno();
    }
  /* A first-use volume has neither tree. That is already a positive absence;
   * do not create directories merely to reset them. Each existing layout's
   * own parent is fsynced after its active entry is removed. */
  ret = bkdisplay_store_path(path, sizeof(path), root, BKDISPLAY_STORE_BASE);
  if (ret < 0) return ret;
  ret = bkdisplay_store_sync_if_directory(path);
  if (ret < 0) return ret;
  ret = bkdisplay_store_path(path, sizeof(path), root, BKDISPLAY_STORE_LEGACY_BASE);
  if (ret < 0) return ret;
  return bkdisplay_store_sync_if_directory(path);
}

static int bkdisplay_store_read_active(const char *path, char *filename,
                                       size_t capacity, uint64_t *revision)
{
  char data[BKDISPLAY_ACTIVE_MAX];
  size_t prefix = strlen(BKDISPLAY_ACTIVE_PREFIX);
  size_t suffix = strlen(BKDISPLAY_ACTIVE_SUFFIX);
  ssize_t nread;
  int read_errno = 0;
  size_t length;
  size_t name_length;
  uint64_t parsed_revision = 0;
  bool versioned;
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
      memcmp(data + length - suffix, BKDISPLAY_ACTIVE_SUFFIX, suffix) != 0)
    {
      return -EPROTO;
    }

  versioned = memcmp(data, BKDISPLAY_ACTIVE_PREFIX_V2, prefix) == 0;
  if (!versioned && memcmp(data, BKDISPLAY_ACTIVE_PREFIX, prefix) != 0)
    {
      return -EPROTO;
    }

  name_length = length - prefix - suffix;
  if (versioned)
    {
      size_t separator = strlen(BKDISPLAY_ACTIVE_REVISION);
      size_t position;

      if (name_length <= separator + 16)
        {
          return -EPROTO;
        }

      name_length -= separator + 16;
      position = prefix + name_length;
      if (memcmp(data + position, BKDISPLAY_ACTIVE_REVISION, separator))
        {
          return -EPROTO;
        }

      position += separator;
      for (unsigned int i = 0; i < 16; i++)
        {
          unsigned char c = data[position + i];
          unsigned int digit;

          if (c >= '0' && c <= '9')
            {
              digit = c - '0';
            }
          else if (c >= 'a' && c <= 'f')
            {
              digit = c - 'a' + 10;
            }
          else
            {
              return -EPROTO;
            }

          parsed_revision = (parsed_revision << 4) | digit;
        }

      if (parsed_revision == 0)
        {
          return -EPROTO;
        }
    }

  if (memchr(data + prefix, '\0', name_length) != NULL)
    {
      return -EPROTO;
    }

  if (name_length + 1u > capacity)
    {
      return -ENAMETOOLONG;
    }

  memcpy(filename, data + prefix, name_length);
  filename[name_length] = '\0';
  if (!bkdisplay_store_filename(filename))
    {
      return -EPROTO;
    }

  if (revision != NULL)
    {
      *revision = parsed_revision;
    }

  return 0;
}

int bkdisplay_store_selection_version(
  const char *root, struct bkdisplay_selection_version_s *version)
{
  struct bkdisplay_selection_version_s current =
  {
    {0}, 0
  };
  char path[BKDISPLAY_PACK_PATH_SIZE];
  int ret;

  if (version == NULL)
    {
      return -EINVAL;
    }

  ret = bkdisplay_store_path(path, sizeof(path), root, BKDISPLAY_STORE_ACTIVE);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_store_read_active(path, current.filename,
                                    sizeof(current.filename),
                                    &current.revision);
  if (ret == -ENOENT)
    {
      ret = bkdisplay_store_path(path, sizeof(path), root,
                                 BKDISPLAY_STORE_LEGACY_ACTIVE);
      if (ret < 0)
        {
          return ret;
        }

      ret = bkdisplay_store_read_active(path, current.filename,
                                        sizeof(current.filename),
                                        &current.revision);
    }

  if (ret == -ENOENT)
    {
      snprintf(current.filename, sizeof(current.filename), "%s",
               BKDISPLAY_STORE_DEFAULT_PACK);
      current.revision = 0;
      ret = 0;
    }

  if (ret == 0)
    {
      *version = current;
    }

  return ret;
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

static int bkdisplay_store_candidate(
  const char *packs_dir, const char *filename,
  struct bkdisplay_store_selection_s *selection)
{
  char path[BKDISPLAY_PACK_PATH_SIZE];

  if (snprintf(path, sizeof(path), "%s/%s", packs_dir, filename) >=
      (int)sizeof(path))
    {
      return -ENAMETOOLONG;
    }

  return bkdisplay_store_validate(path, filename, selection, true, NULL);
}

static int bkdisplay_store_scan(
  const char *packs_dir, const char *rejected,
  struct bkdisplay_store_selection_s *selection)
{
  struct dirent *entry;
  DIR *directory = opendir(packs_dir);
  int ret = -ENOENT;

  if (directory == NULL)
    {
      return bkdisplay_store_errno();
    }

  while ((entry = readdir(directory)) != NULL)
    {
      if (!bkdisplay_store_filename(entry->d_name) ||
          strcmp(entry->d_name, rejected) == 0)
        {
          continue;
        }

      ret = bkdisplay_store_candidate(packs_dir, entry->d_name, selection);
      if (ret == 0)
        {
          break;
        }
    }

  (void)closedir(directory);
  return ret;
}

static int bkdisplay_store_resolve_layout(
  const char *root, const char *active_relative, const char *packs_relative,
  struct bkdisplay_store_selection_s *selection, bool *fallback_result,
  struct bkdisplay_pack_s **result_pack)
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
  if (result_pack != NULL)
    {
      *result_pack = NULL;
    }

  ret = bkdisplay_store_path(active, sizeof(active), root,
                             active_relative);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_store_read_active(active, filename, sizeof(filename), NULL);
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

  ret = bkdisplay_store_validate(active, filename, selection, fallback,
                                 result_pack);
  if (ret == 0 || ret == -ENOENT)
    {
      return ret;
    }

  BKDISPLAY_STORE_DIAG(
    "BKDISPLAY STORE stage=pack-validate path=%s fallback=%u ret=%d\n",
    active, fallback ? 1u : 0u, ret);

  /* The active marker names a pack that cannot be read or parsed.  Leaving
   * both panels dark is worse than using another pack that is already
   * installed, so try the board default and then any other valid pack.  The
   * marker file is left untouched: this is a read-only recovery for the
   * current boot, not a silent re-selection.
   */

  {
    int first_error = ret;

    if (strcmp(filename, BKDISPLAY_STORE_DEFAULT_PACK) != 0 &&
        bkdisplay_store_candidate(pack, BKDISPLAY_STORE_DEFAULT_PACK,
                                  selection) == 0)
      {
        if (result_pack != NULL)
          {
            ret = bkdisplay_pack_open(selection->path, result_pack, NULL);
            if (ret < 0) return ret;
          }
        *fallback_result = true;
        BKDISPLAY_STORE_DIAG(
          "BKDISPLAY STORE stage=fallback path=%s reason=default\n",
          selection->path);
        return 0;
      }

    if (bkdisplay_store_scan(pack, filename, selection) == 0)
      {
        if (result_pack != NULL)
          {
            ret = bkdisplay_pack_open(selection->path, result_pack, NULL);
            if (ret < 0) return ret;
          }
        *fallback_result = true;
        BKDISPLAY_STORE_DIAG(
          "BKDISPLAY STORE stage=fallback path=%s reason=scan\n",
          selection->path);
        return 0;
      }

    return first_error;
  }
}

int bkdisplay_store_open_installed(
  const char *root, const char *filename,
  struct bkdisplay_store_selection_s *selection,
  struct bkdisplay_pack_s **pack)
{
  char directory[BKDISPLAY_PACK_PATH_SIZE];
  char path[BKDISPLAY_PACK_PATH_SIZE];
  int ret;

  if (pack == NULL)
    {
      return -EINVAL;
    }

  *pack = NULL;
  if (root == NULL || selection == NULL ||
      !bkdisplay_store_filename(filename))
    {
      return -EINVAL;
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

  return bkdisplay_store_validate(path, filename, selection, false, pack);
}

int bkdisplay_store_resolve_open(const char *root,
                                 struct bkdisplay_store_selection_s *selection,
                                 struct bkdisplay_pack_s **pack)
{
  static const char *const legacy_dirs[] =
    {
      BKDISPLAY_STORE_LEGACY_ROOT,
      BKDISPLAY_STORE_LEGACY_BASE
    };
  char legacy[BKDISPLAY_PACK_PATH_SIZE];
  struct stat statbuf;
  bool canonical_fallback;
  bool legacy_fallback;
  size_t i;
  int ret;

  if (pack == NULL)
    {
      return -EINVAL;
    }

  *pack = NULL;

  ret = bkdisplay_store_resolve_layout(root, BKDISPLAY_STORE_ACTIVE,
                                       BKDISPLAY_STORE_PACKS, selection,
                                       &canonical_fallback, pack);
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

  /* Check each optional ancestor before opening the legacy marker. FAT can
   * report ENOTDIR for an absent intermediate directory. A missing legacy
   * tree means no installed resource; a file in its place remains an error.
   */

  for (i = 0; i < sizeof(legacy_dirs) / sizeof(legacy_dirs[0]); i++)
    {
      ret = bkdisplay_store_path(legacy, sizeof(legacy), root, legacy_dirs[i]);
      if (ret < 0)
        {
          return ret;
        }

      if (stat(legacy, &statbuf) < 0)
        {
          return bkdisplay_store_errno();
        }

      if (!S_ISDIR(statbuf.st_mode))
        {
          return -ENOTDIR;
        }
    }

  ret = bkdisplay_store_resolve_layout(root, BKDISPLAY_STORE_LEGACY_ACTIVE,
                                       BKDISPLAY_STORE_LEGACY_PACKS,
                                       selection, &legacy_fallback, pack);
  if (ret == 0)
    {
      BKDISPLAY_STORE_DIAG(
        "BKDISPLAY STORE stage=legacy-resolve path=%s fallback=%u\n",
        selection->path, legacy_fallback ? 1u : 0u);
    }

  return ret;
}

int bkdisplay_store_resolve(const char *root,
                            struct bkdisplay_store_selection_s *selection)
{
  struct bkdisplay_pack_s *pack = NULL;
  int ret = bkdisplay_store_resolve_open(root, selection, &pack);

  bkdisplay_pack_close(pack);
  return ret;
}

static int bkdisplay_store_activate_versioned(
  const char *root, const char *filename, bool check_revision,
  uint64_t expected_revision, struct bkdisplay_store_selection_s *selection,
  struct bkdisplay_selection_version_s *version)
{
  struct bkdisplay_store_selection_s selected;
  struct bkdisplay_selection_version_s current;
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

  ret = bkdisplay_store_selection_version(root, &current);
  if (ret < 0)
    {
      return ret;
    }

  if (check_revision && current.revision != expected_revision)
    {
      return -ESTALE;
    }

  if (current.revision == UINT64_MAX)
    {
      return -EOVERFLOW;
    }

  current.revision++;

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

  ret = bkdisplay_store_validate(path, filename, &selected, false, NULL);
  if (ret < 0)
    {
      return ret;
    }

  marker_length = snprintf(marker, sizeof(marker), "%s%s%s%016" PRIx64 "%s",
                           BKDISPLAY_ACTIVE_PREFIX_V2, filename,
                           BKDISPLAY_ACTIVE_REVISION, current.revision,
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

  /* A remnant is not owned by this request. Preserve it for explicit
   * recovery instead of silently discarding an interrupted selection.
   */

  fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0644);
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

  if (ret == 0)
    {
      char base[BKDISPLAY_PACK_PATH_SIZE];

      ret = bkdisplay_store_path(base, sizeof(base), root,
                                 BKDISPLAY_STORE_BASE);
      if (ret == 0)
        {
          /* Rename may already be visible when sync fails. Report unknown
           * durability; never manufacture a rollback or a success receipt.
           */

          ret = bkdisplay_store_sync_directory_strict(base);
        }
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

  if (version != NULL)
    {
      snprintf(current.filename, sizeof(current.filename), "%s", filename);
      *version = current;
    }

  return 0;
}

int bkdisplay_store_activate(const char *root, const char *filename,
                             struct bkdisplay_store_selection_s *selection)
{
  return bkdisplay_store_activate_versioned(root, filename, false, 0,
                                            selection, NULL);
}

int bkdisplay_store_activate_checked(
  const char *root, const char *filename, uint64_t expected_revision,
  struct bkdisplay_selection_version_s *version)
{
  return bkdisplay_store_activate_versioned(root, filename, true,
                                            expected_revision, NULL, version);
}

static int bkdisplay_store_publish(
  const char *root, const char *filename,
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

  ret = bkdisplay_store_validate(staging, filename, NULL, false, NULL);
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

  ret = bkdisplay_store_sync_directory_strict(packs_dir);
  if (ret == 0)
    {
      ret = bkdisplay_store_sync_directory_strict(staging_dir);
    }

  /* A sync error after rename leaves a possibly installed file, but never
   * changes active.json. Report the error; do not claim durable completion.
   */

  return ret < 0 ? ret :
    bkdisplay_store_validate(installed, filename, selection, false, NULL);
}

int bkdisplay_store_install(const char *root, const char *filename,
                            struct bkdisplay_store_selection_s *selection)
{
  int ret = bkdisplay_store_publish(root, filename, NULL);

  return ret < 0 ? ret : bkdisplay_store_activate(root, filename, selection);
}

/* Upload state is private to this synchronous storage owner. A protocol job
 * supplies its own ID/generation and cancels between operations.
 */

enum bkdisplay_upload_state_e
{
  BKUPLOAD_EMPTY = 0,
  BKUPLOAD_RECEIVING,
  BKUPLOAD_INSTALLED,
  BKUPLOAD_CANCELED,
  BKUPLOAD_FAILED,
  BKUPLOAD_CLOSE_UNKNOWN
};

static int bkdisplay_upload_close(struct bkdisplay_upload_s *upload)
{
  int fd = upload->fd;

  upload->fd = -1;
  if (close(fd) < 0)
    {
      upload->error = bkdisplay_store_errno();
      upload->state = BKUPLOAD_CLOSE_UNKNOWN;
      return upload->error;
    }

  return 0;
}

static int bkdisplay_upload_fail(struct bkdisplay_upload_s *upload,
                                 int error)
{
  if (upload->fd >= 0 && bkdisplay_upload_close(upload) < 0)
    {
      return upload->error;
    }

  if (unlink(upload->temporary) < 0 && errno != ENOENT)
    {
      error = bkdisplay_store_errno();
    }

  upload->state = BKUPLOAD_FAILED;
  upload->error = error;
  return error;
}

int bkdisplay_upload_quiesced(const struct bkdisplay_upload_s *upload)
{
  if (upload == NULL) return -EINVAL;
  if (upload->state == BKUPLOAD_RECEIVING) return -EBUSY;
  if (upload->state == BKUPLOAD_CLOSE_UNKNOWN) return upload->error;
  return 0;
}

int bkdisplay_upload_begin(struct bkdisplay_upload_s *upload,
                           const char *root, size_t size)
{
  int ret;

  if (upload == NULL || root == NULL || size < 128 ||
      size > BKDISPLAY_MAX_PACK_BYTES)
    {
      return -EINVAL;
    }

  if (upload->state != BKUPLOAD_EMPTY)
    {
      return upload->state == BKUPLOAD_CLOSE_UNKNOWN ? upload->error :
             -EALREADY;
    }

  ret = bkdisplay_store_path(upload->temporary, sizeof(upload->temporary),
                             root, BKDISPLAY_STORE_STAGING "/.upload.bkep");
  if (ret < 0)
    {
      return ret;
    }

  if (snprintf(upload->root, sizeof(upload->root), "%s", root) >=
      (int)sizeof(upload->root))
    {
      return -ENAMETOOLONG;
    }

  ret = bkdisplay_store_ensure(root);
  if (ret < 0)
    {
      return ret;
    }

  /* Do not truncate or clean another job's staging file, including an
   * unreviewed remnant from a previous boot.
   */

  upload->fd = open(upload->temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (upload->fd < 0)
    {
      return bkdisplay_store_errno();
    }

  upload->state = BKUPLOAD_RECEIVING;
  upload->expected = size;
  upload->received = 0;
  return 0;
}

int bkdisplay_upload_append(struct bkdisplay_upload_s *upload, size_t offset,
                            const void *data, size_t size)
{
  int ret;

  if (upload == NULL)
    {
      return -EINVAL;
    }

  if (upload->state != BKUPLOAD_RECEIVING)
    {
      return -EALREADY;
    }

  if (data == NULL || size == 0 || size > BKDISPLAY_UPLOAD_CHUNK_MAX ||
      offset != upload->received || size > upload->expected - offset)
    {
      return -EINVAL;
    }

  ret = bkdisplay_store_write_all(upload->fd, data, size);
  if (ret < 0)
    {
      return bkdisplay_upload_fail(upload, ret);
    }

  upload->received += size;
  return 0;
}

int bkdisplay_upload_cancel(struct bkdisplay_upload_s *upload)
{
  int ret;

  if (upload == NULL)
    {
      return -EINVAL;
    }

  if (upload->state == BKUPLOAD_CANCELED)
    {
      return 0;
    }

  if (upload->state != BKUPLOAD_RECEIVING)
    {
      return upload->state == BKUPLOAD_CLOSE_UNKNOWN ? upload->error :
             -EALREADY;
    }

  ret = bkdisplay_upload_fail(upload, 0);
  if (ret == 0)
    {
      upload->state = BKUPLOAD_CANCELED;
    }

  return ret;
}

int bkdisplay_upload_finish(struct bkdisplay_upload_s *upload,
                            struct bkdisplay_store_selection_s *selection)
{
  char staged[BKDISPLAY_PACK_PATH_SIZE];
  char filename[BKDISPLAY_STORE_FILENAME_SIZE];
  struct bkdisplay_pack_info_s info;
  struct bkdisplay_pack_s *pack = NULL;
  struct stat statbuf;
  uint16_t *pixels;
  int ret;

  if (upload == NULL)
    {
      return -EINVAL;
    }

  if (upload->state != BKUPLOAD_RECEIVING)
    {
      return upload->state == BKUPLOAD_CLOSE_UNKNOWN ? upload->error :
             -EALREADY;
    }

  if (upload->received != upload->expected)
    {
      return -EAGAIN;
    }

  if (fsync(upload->fd) < 0)
    {
      return bkdisplay_upload_fail(upload, bkdisplay_store_errno());
    }

  ret = bkdisplay_upload_close(upload);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_pack_open(upload->temporary, &pack, &info);
  if (ret == 0)
    {
      pixels = malloc(BKDISPLAY_CANVAS_PIXELS * sizeof(*pixels));
      ret = pixels == NULL ? -ENOMEM :
        bkdisplay_pack_render(pack, "neutral", BKDISPLAY_SIDE_UNMAPPED,
                              pixels, BKDISPLAY_CANVAS_PIXELS);
      free(pixels);
    }

  bkdisplay_pack_close(pack);
  if (ret < 0)
    {
      return bkdisplay_upload_fail(upload, ret);
    }

  snprintf(filename, sizeof(filename), "%s.bkep", info.pack_id);
  if (snprintf(staged, sizeof(staged), "%s/" BKDISPLAY_STORE_STAGING
               "/%s", upload->root, filename) >= (int)sizeof(staged))
    {
      return bkdisplay_upload_fail(upload, -ENAMETOOLONG);
    }

  if (stat(staged, &statbuf) == 0)
    {
      return bkdisplay_upload_fail(upload, -EEXIST);
    }

  if (errno != ENOENT || rename(upload->temporary, staged) < 0)
    {
      return bkdisplay_upload_fail(upload, bkdisplay_store_errno());
    }

  ret = bkdisplay_store_publish(upload->root, filename, selection);

  /* Only this upload's renamed file is eligible for cleanup. */

  if (unlink(staged) < 0 && errno != ENOENT && ret == 0)
    {
      ret = bkdisplay_store_errno();
    }

  upload->state = ret == 0 ? BKUPLOAD_INSTALLED : BKUPLOAD_FAILED;
  upload->error = ret;
  return ret;
}

int bkdisplay_store_import(const char *root, const void *data, size_t size,
                           struct bkdisplay_store_selection_s *selection)
{
  struct bkdisplay_store_selection_s installed;
  struct bkdisplay_upload_s upload;
  const uint8_t *bytes = data;
  size_t offset = 0;
  int ret;

  if (data == NULL)
    {
      return -EINVAL;
    }

  memset(&upload, 0, sizeof(upload));
  ret = bkdisplay_upload_begin(&upload, root, size);
  while (ret == 0 && offset < size)
    {
      size_t count = size - offset;

      if (count > BKDISPLAY_UPLOAD_CHUNK_MAX)
        {
          count = BKDISPLAY_UPLOAD_CHUNK_MAX;
        }

      ret = bkdisplay_upload_append(&upload, offset, bytes + offset, count);
      offset += count;
    }

  if (ret == 0)
    {
      ret = bkdisplay_upload_finish(&upload, &installed);
    }

  /* Preserve the legacy explicit import-and-activate command. New job users
   * finish installation separately and activate only on a distinct request.
   */

  return ret < 0 ? ret :
    bkdisplay_store_activate(root, installed.filename, selection);
}


int bkdisplay_store_catalog_page(
  const char *root, const char *after,
  bool (*canceled)(void *context), void *context,
  struct bkdisplay_catalog_page_s *page)
{
  struct bkdisplay_catalog_page_s result = {0};
  char names[BKDISPLAY_CATALOG_PAGE_MAX + 1][BKDISPLAY_STORE_FILENAME_SIZE] = {{0}};
  char directory[BKDISPLAY_PACK_PATH_SIZE];
  char path[BKDISPLAY_PACK_PATH_SIZE];
  struct bkdisplay_store_selection_s selection;
  struct bkdisplay_pack_s *pack;
  struct stat st;
  struct dirent *entry;
  DIR *dir;
  unsigned int scanned = 0;
  int ret;

  if (page == NULL)
    {
      return -EINVAL;
    }

  memset(page, 0, sizeof(*page));
  if (after != NULL && after[0] != '\0' &&
      !bkdisplay_store_filename(after))
    {
      return -EINVAL;
    }

  ret = bkdisplay_store_path(directory, sizeof(directory), root,
                             BKDISPLAY_STORE_PACKS);
  if (ret < 0)
    {
      return ret;
    }

  if (canceled != NULL && canceled(context))
    {
      return -ECANCELED;
    }

  if (lstat(directory, &st) < 0)
    {
      return bkdisplay_store_errno();
    }

  if (!S_ISDIR(st.st_mode))
    {
      return S_ISLNK(st.st_mode) ? -ELOOP : -ENOTDIR;
    }

  dir = opendir(directory);
  if (dir == NULL)
    {
      return bkdisplay_store_errno();
    }

  for (;;)
    {
      if (canceled != NULL && canceled(context))
        {
          ret = -ECANCELED;
          break;
        }

      errno = 0;
      entry = readdir(dir);
      if (entry == NULL)
        {
          ret = errno ? bkdisplay_store_errno() : 0;
          break;
        }

      if (++scanned > BKDISPLAY_CATALOG_SCAN_MAX)
        {
          ret = -E2BIG;
          break;
        }

      if (!bkdisplay_store_filename(entry->d_name) ||
          (after != NULL && strcmp(entry->d_name, after) <= 0))
        {
          continue;
        }

      for (unsigned int i = 0; i <= BKDISPLAY_CATALOG_PAGE_MAX; i++)
        {
          if (!names[i][0] || strcmp(entry->d_name, names[i]) < 0)
            {
              for (unsigned int j = BKDISPLAY_CATALOG_PAGE_MAX; j > i; j--)
                {
                  memcpy(names[j], names[j - 1], sizeof(names[j]));
                }

              /* Canonical validation above guarantees the bounded length. */

              memcpy(names[i], entry->d_name, strlen(entry->d_name) + 1);
              break;
            }
        }
    }

  if (closedir(dir) < 0)
    {
      ret = bkdisplay_store_errno();
    }

  if (ret < 0)
    {
      return ret;
    }

  result.more = names[BKDISPLAY_CATALOG_PAGE_MAX][0] != '\0';
  for (unsigned int i = 0; i < BKDISPLAY_CATALOG_PAGE_MAX && names[i][0]; i++)
    {
      if (canceled != NULL && canceled(context))
        {
          return -ECANCELED;
        }

      if (snprintf(path, sizeof(path), "%s/%s", directory, names[i]) >=
          (int)sizeof(path))
        {
          return -ENAMETOOLONG;
        }

      if (lstat(path, &st) < 0)
        {
          return bkdisplay_store_errno();
        }

      if (!S_ISREG(st.st_mode))
        {
          return S_ISLNK(st.st_mode) ? -ELOOP : -EINVAL;
        }

      ret = bkdisplay_store_open_installed(root, names[i], &selection, &pack);
      if (ret < 0)
        {
          return ret;
        }

      ret = bkdisplay_pack_close(pack);
      if (ret < 0)
        {
          return ret;
        }

      memcpy(result.entries[i].filename, names[i], sizeof(names[i]));
      result.entries[i].info = selection.info;
      result.count++;
    }

  if (canceled != NULL && canceled(context))
    {
      return -ECANCELED;
    }

  *page = result;
  return 0;
}
