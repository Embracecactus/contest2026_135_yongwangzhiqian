/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_store.h"
#include "bk7258_provision_claim.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mbedtls/constant_time.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#ifdef __NuttX__
#include <sys/statfs.h>
#include <nuttx/fs/fs.h>
#endif

#define HEADER 64u

static void put64(uint8_t *p, uint64_t value)
{
  for (int i = 7; i >= 0; i--) { p[i] = value; value >>= 8; }
}

static uint64_t get64(const uint8_t *p)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; i++) value = (value << 8) | p[i];
  return value;
}

static int transfer(int fd, void *data, size_t size, int writing)
{
  uint8_t *p = data;
  while (size)
    {
      ssize_t ret = writing ? write(fd, p, size) : read(fd, p, size);
      if (ret < 0 && errno == EINTR) continue;
      if (ret < 0) return -errno;
      if (ret == 0) return -EIO;
      p += ret;
      size -= ret;
    }
  return 0;
}

static int digest(uint8_t header[HEADER], const void *data, size_t size,
                   uint8_t result[32])
{
  mbedtls_sha256_context sha;
  int ret;
  mbedtls_sha256_init(&sha);
  ret = mbedtls_sha256_starts(&sha, 0);
  if (ret == 0) ret = mbedtls_sha256_update(&sha, header, 32);
  if (ret == 0) ret = mbedtls_sha256_update(&sha, data, size);
  if (ret == 0) ret = mbedtls_sha256_finish(&sha, result);
  mbedtls_sha256_free(&sha);
  return ret == 0 ? 0 : -EIO;
}

int bkprov_store_check_filesystem(const char *root)
{
  if (root == NULL) return -EINVAL;
#ifdef __NuttX__
  struct statfs fs;
  if (statfs(root, &fs) < 0) return -errno;
  if (fs.f_type == LITTLEFS_SUPER_MAGIC) return 0;
#if defined(CONFIG_BK7258_RPMSGFS) && defined(CONFIG_BK7258_AP_CORE)
  /* The chip binding mounts cpu=cp,fs=/data at /cpdata. CP alone mounts
   * LittleFS there. Stock RPMsgFS replaces f_type, but retains the remote
   * geometry. An unmounted CP pseudo-directory has zero blocks. Do not
   * accept arbitrary RPMsgFS paths or an unready remote root.
   */
  if (fs.f_type == RPMSGFS_MAGIC &&
      (!strcmp(root, "/cpdata") || !strcmp(root, "/cpdata/shaniu") ||
       !strcmp(root, "/cpdata/shaniu/identity") ||
       !strcmp(root, "/cpdata/shaniu/voice-volume") ||
       !strcmp(root, "/cpdata/shaniu/voice-ota") ||
       !strcmp(root, "/cpdata/shaniu/memory-policy") ||
       !strcmp(root, "/cpdata/shaniu/memory-snapshot") ||
       !strcmp(root, "/cpdata/shaniu/cloud-models") ||
       !strcmp(root, "/cpdata/shaniu/wake-models")))
    return fs.f_blocks > 0 && fs.f_bsize > 0 ? 0 : -ENODEV;
#endif
  return -EXDEV;
#else
  return 0;
#endif
}

int bkprov_store_open(struct bkprov_store_s *store, const char *root)
{
  struct stat info;
  size_t length;
  if (store == NULL || root == NULL) return -EINVAL;
  memset(store, 0, sizeof(*store));
  length = strlen(root);
  if (length == 0 || length >= sizeof(store->directory) || root[0] != '/' ||
      root[length - 1] == '/') return -EINVAL;
  if (lstat(root, &info) < 0) return -errno;
  if (!S_ISDIR(info.st_mode)) return -ENOTDIR;
  int ret = bkprov_store_check_filesystem(root);
  if (ret < 0) return ret;
  memcpy(store->directory, root, length + 1);
  snprintf(store->active, sizeof(store->active), "%s/config.bin", root);
  snprintf(store->pending, sizeof(store->pending), "%s/config.pending", root);
  return 0;
}

int bkprov_store_load(struct bkprov_store_s *store, void *bundle,
                      size_t capacity, size_t *size, uint64_t *revision,
                      uint8_t transaction[16])
{
  uint8_t header[HEADER], hash[32];
  struct stat info;
  size_t length = 0;
  int ret, fd;
  if (store == NULL || store->active[0] == 0 || bundle == NULL ||
      size == NULL || revision == NULL) return -EINVAL;
  *size = 0; *revision = 0;
  if (transaction != NULL) memset(transaction, 0, 16);
  fd = open(store->active, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) return -errno;
  if (fstat(fd, &info) < 0) { ret = -errno; goto done; }
  if (!S_ISREG(info.st_mode) || info.st_size <= HEADER ||
      info.st_size > HEADER + BKPROV_STORE_RECORD_MAX)
    { ret = -EBADMSG; goto done; }
  ret = transfer(fd, header, HEADER, 0);
  if (ret < 0) goto done;
  length = ((size_t)header[12] << 24) | ((size_t)header[13] << 16) |
           ((size_t)header[14] << 8) | header[15];
  if (memcmp(header, "SCF1", 4) || get64(header + 4) == 0 ||
      length != (size_t)info.st_size - HEADER)
    { ret = -EBADMSG; goto done; }
  if (capacity < length) { ret = -ENOSPC; goto done; }
  ret = transfer(fd, bundle, length, 0);
  if (ret < 0) goto done;
  ret = digest(header, bundle, length, hash);
  if (ret == 0 && mbedtls_ct_memcmp(hash, header + 32, 32)) ret = -EBADMSG;
  if (ret == 0)
    {
      *size = length; *revision = get64(header + 4);
      if (transaction != NULL) memcpy(transaction, header + 16, 16);
    }
done:
  if (close(fd) < 0 && ret == 0) ret = -errno;
  if (ret < 0)
    {
      mbedtls_platform_zeroize(bundle, length < capacity ? length : capacity);
      *size = 0; *revision = 0;
      if (transaction != NULL) memset(transaction, 0, 16);
    }
  mbedtls_platform_zeroize(header, sizeof(header));
  mbedtls_platform_zeroize(hash, sizeof(hash));
  return ret;
}

int bkprov_store_commit(struct bkprov_store_s *store, uint64_t expected,
                        const uint8_t transaction[16], const void *bundle, size_t size)
{
  uint8_t header[HEADER] = {'S', 'C', 'F', '1'};
  uint8_t *previous;
  size_t previous_size;
  uint64_t revision;
  int ret, fd;
  if (store == NULL || store->active[0] == 0 || bundle == NULL ||
      transaction == NULL || size == 0 || size > BKPROV_STORE_RECORD_MAX ||
      expected == UINT64_MAX)
    return -EINVAL;
  previous = malloc(BKPROV_STORE_RECORD_MAX);
  if (previous == NULL) return -ENOMEM;
  ret = bkprov_store_load(store, previous, BKPROV_STORE_RECORD_MAX,
                          &previous_size, &revision, NULL);
  mbedtls_platform_zeroize(previous, BKPROV_STORE_RECORD_MAX);
  free(previous);
  if (ret == -ENOENT) revision = 0;
  else if (ret < 0) return ret;
  if (revision != expected) return -ESTALE;
  put64(header + 4, expected + 1);
  header[12] = size >> 24; header[13] = size >> 16;
  header[14] = size >> 8; header[15] = size;
  memcpy(header + 16, transaction, 16);
  ret = digest(header, bundle, size, header + 32);
  if (ret < 0) return ret;
  /* A leftover staging name is never the selected configuration. Unlink it
   * before exclusive creation so even a hard link cannot truncate active.
   */
  if (unlink(store->pending) < 0 && errno != ENOENT) return -errno;
  fd = open(store->pending, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
                             O_NONBLOCK, 0600);
  if (fd < 0) return -errno;
  ret = transfer(fd, header, HEADER, 1);
  if (ret == 0) ret = transfer(fd, (void *)bundle, size, 1);
  if (ret == 0 && fsync(fd) < 0) ret = -errno;
  if (close(fd) < 0 && ret == 0) ret = -errno;
  if (ret < 0) { unlink(store->pending); return ret; }
  if (rename(store->pending, store->active) < 0) return -EINPROGRESS;
  return bkprov_store_sync_directory(store->directory);
}

int bkprov_store_sync_directory(const char *directory)
{
#ifndef __NuttX__
  int fd;
  int ret;
  /* Host POSIX requires the containing directory to be synced. NuttX
   * LittleFS rename commits its metadata before returning; it does not
   * implement directory descriptors usable with fsync.
   */
  fd = open(directory, O_RDONLY | O_DIRECTORY);
  if (fd < 0) return -EINPROGRESS;
  ret = fsync(fd);
  if (close(fd) < 0) ret = -1;
  if (ret < 0) return -EINPROGRESS;
#else
  (void)directory;
#endif
  return 0;
}
