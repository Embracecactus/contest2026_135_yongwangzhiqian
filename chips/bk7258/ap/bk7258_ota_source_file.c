/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/
 * bk7258_ota_source_file.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verified extracted-package source shared by TF and fixed NAND caches.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_OTA_SOURCE_FILE

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arch/chip/bk7258_ota_catalog.h>
#include <arch/chip/bk7258_ota_source_file.h>

#define BK7258_OTA_FILE_CATALOG "catalog.json"
#define BK7258_OTA_FILE_SIGNATURE "catalog.sig"

static void bk7258_ota_file_close_fds(
  struct bk7258_ota_file_source_s *source)
{
  int image;

  for (image = BK7258_OTA_IMAGE_CP; image <= BK7258_OTA_IMAGE_AP; image++)
    {
      if (source->fd[image] >= 0)
        {
          close(source->fd[image]);
          source->fd[image] = -1;
        }
    }
}

static int bk7258_ota_file_path(char *output, size_t output_size,
                                const char *root, const char *relative)
{
  int count = snprintf(output, output_size, "%s/%s", root, relative);

  return count > 0 && (size_t)count < output_size ? 0 : -ENAMETOOLONG;
}

static int bk7258_ota_file_read_small(const char *path, uint8_t *buffer,
                                      size_t capacity, size_t *size)
{
  struct stat status;
  size_t done = 0u;
  int fd;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode) ||
      status.st_size <= 0 || (uint64_t)status.st_size > capacity)
    {
      close(fd);
      return -EINVAL;
    }

  while (done < (size_t)status.st_size)
    {
      ssize_t count = read(fd, buffer + done,
                           (size_t)status.st_size - done);
      if (count <= 0)
        {
          close(fd);
          return count < 0 ? -errno : -EIO;
        }
      done += (size_t)count;
    }

  close(fd);
  *size = done;
  return 0;
}

static int bk7258_ota_file_open(
  void *context, struct bk7258_ota_manifest_s *manifest)
{
  struct bk7258_ota_file_source_s *source = context;
  uint8_t catalog[BK7258_OTA_CATALOG_MAX_SIZE];
  uint8_t signature[BK7258_OTA_CATALOG_MAX_SIGNATURE];
  char path[BK7258_OTA_FILE_PATH_SIZE];
  size_t catalog_size;
  size_t signature_size;
  struct stat status;
  int image;
  int ret;

  if (source == NULL || manifest == NULL ||
      __atomic_load_n(&source->canceled, __ATOMIC_ACQUIRE))
    {
      return -ECANCELED;
    }

  bk7258_ota_file_close_fds(source);
  ret = bk7258_ota_file_path(path, sizeof(path), source->root,
                             BK7258_OTA_FILE_CATALOG);
  if (ret == 0)
    {
      ret = bk7258_ota_file_read_small(path, catalog, sizeof(catalog),
                                       &catalog_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_file_path(path, sizeof(path), source->root,
                                 BK7258_OTA_FILE_SIGNATURE);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_file_read_small(path, signature, sizeof(signature),
                                       &signature_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_catalog_verify(catalog, catalog_size,
                                      signature, signature_size,
                                      &source->catalog);
    }

  for (image = BK7258_OTA_IMAGE_CP;
       ret == 0 && image <= BK7258_OTA_IMAGE_AP; image++)
    {
      ret = bk7258_ota_file_path(source->path[image],
                                 sizeof(source->path[image]), source->root,
                                 source->catalog.uri[image]);
      if (ret < 0)
        {
          break;
        }

      source->fd[image] = open(source->path[image], O_RDONLY);
      if (source->fd[image] < 0)
        {
          ret = -errno;
          break;
        }
      if (fstat(source->fd[image], &status) < 0 ||
          !S_ISREG(status.st_mode) ||
          status.st_size !=
            (off_t)source->catalog.manifest.image[image].physical_size)
        {
          ret = -EINVAL;
          break;
        }
    }

  if (ret < 0)
    {
      bk7258_ota_file_close_fds(source);
      return ret;
    }

  memcpy(manifest, &source->catalog.manifest, sizeof(*manifest));
  return 0;
}

static int bk7258_ota_file_read_at(
  void *context, enum bk7258_ota_image_e image, uint32_t offset,
  uint8_t *buffer, size_t nbytes)
{
  struct bk7258_ota_file_source_s *source = context;
  size_t done = 0u;

  if (source == NULL || buffer == NULL || nbytes == 0u ||
      image < BK7258_OTA_IMAGE_CP || image > BK7258_OTA_IMAGE_AP ||
      source->fd[image] < 0 ||
      (uint64_t)offset + nbytes >
        source->catalog.manifest.image[image].physical_size)
    {
      return -EINVAL;
    }
  if (__atomic_load_n(&source->canceled, __ATOMIC_ACQUIRE))
    {
      return -ECANCELED;
    }

  while (done < nbytes)
    {
      ssize_t count;

      if (__atomic_load_n(&source->canceled, __ATOMIC_ACQUIRE))
        {
          return -ECANCELED;
        }
      count = pread(source->fd[image], buffer + done, nbytes - done,
                    (off_t)offset + (off_t)done);
      if (count <= 0)
        {
          return count < 0 ? -errno : -EIO;
        }
      done += (size_t)count;
    }

  return 0;
}

static int bk7258_ota_file_checkpoint(
  void *context, const struct bk7258_ota_progress_s *progress)
{
  struct bk7258_ota_file_source_s *source = context;

  (void)progress;
  return __atomic_load_n(&source->canceled, __ATOMIC_ACQUIRE) ?
         -ECANCELED : 0;
}

static int bk7258_ota_file_cancel(void *context)
{
  struct bk7258_ota_file_source_s *source = context;

  __atomic_store_n(&source->canceled, true, __ATOMIC_RELEASE);
  return 0;
}

static void bk7258_ota_file_close(void *context)
{
  bk7258_ota_file_close_fds(context);
}

static const struct bk7258_ota_source_ops_s g_bk7258_ota_file_ops =
{
  .open = bk7258_ota_file_open,
  .read_at = bk7258_ota_file_read_at,
  .checkpoint = bk7258_ota_file_checkpoint,
  .cancel = bk7258_ota_file_cancel,
  .close = bk7258_ota_file_close,
};

int bk7258_ota_file_source_initialize(
  struct bk7258_ota_file_source_s *source, const char *root)
{
  size_t length;

  if (source == NULL || root == NULL)
    {
      return -EINVAL;
    }

  length = strlen(root);
  if (length == 0u || length >= sizeof(source->root) || root[0] != '/')
    {
      return -EINVAL;
    }

  memset(source, 0, sizeof(*source));
  memcpy(source->root, root, length + 1u);
  source->fd[BK7258_OTA_IMAGE_CP] = -1;
  source->fd[BK7258_OTA_IMAGE_AP] = -1;
  return 0;
}

const struct bk7258_ota_source_ops_s *bk7258_ota_file_source_ops(void)
{
  return &g_bk7258_ota_file_ops;
}

#endif /* CONFIG_BK7258_OTA_SOURCE_FILE */
