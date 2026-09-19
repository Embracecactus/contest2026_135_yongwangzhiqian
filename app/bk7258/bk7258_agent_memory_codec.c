/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_agent_memory_codec.h"
#include "bk7258_provision_store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>

static bool nonzero(const uint8_t key[32])
{
  uint8_t bits = 0;
  for (size_t i = 0; i < 32; i++) bits |= key[i];
  return bits != 0;
}
static void put32(uint8_t *p, uint32_t n)
{ p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n; }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int parent_check(const char *root)
{
  char parent[160];
  if (!root || root[0] != '/' || strlen(root) >= sizeof(parent)) return -EINVAL;
  strcpy(parent, root);
  char *end = strrchr(parent, '/');
  if (!end || end == parent || end[1] == 0) return -EINVAL;
  *end = 0;
  struct stat info;
  if (lstat(parent, &info) < 0) return -errno;
  if (!S_ISDIR(info.st_mode)) return -ENOTDIR;
  return bkprov_store_check_filesystem(parent);
}
int bkmemory_policy_load(const char *root, const uint8_t owner[32],
                               struct bkmemory_policy_s *policy)
{
  struct bkprov_store_s store;
  uint8_t record[72] = {0}, hash[32];
  size_t size;
  if (!policy) return -EINVAL;
  memset(policy, 0, sizeof(*policy));
  if (!owner || !nonzero(owner)) return -EINVAL;
  int ret = parent_check(root);
  if (ret < 0) return ret;
  ret = bkprov_store_open(&store, root);
  if (ret == -ENOENT) return 0;
  if (ret < 0) return ret;
  uint64_t revision;
  ret = bkprov_store_load(&store, record, sizeof(record), &size, &revision, NULL);
  if (ret == -ENOENT) ret = 0;
  else if (ret == 0)
    {
      if (size != sizeof(record) || memcmp(record, "SMP1", 4) ||
          record[4] > 1 || record[5] || record[6] || record[7] ||
          (record[4] && !nonzero(record + 8))) ret = -EBADMSG;
      else if (mbedtls_sha256(owner, 32, hash, 0) != 0) ret = -EIO;
      else
        {
          policy->revision = revision;
          if (!memcmp(hash, record + 40, 32))
            { memcpy(policy->key, record + 8, 32); policy->enabled = record[4] != 0; }
        }
    }
  mbedtls_platform_zeroize(record, sizeof(record));
  mbedtls_platform_zeroize(hash, sizeof(hash));
  return ret;
}
int bkmemory_policy_set(const char *root, const uint8_t owner[32],
                              bool enabled, bool rotate,
                              bkmemory_random_t random, void *context)
{
  struct bkmemory_policy_s policy;
  struct bkprov_store_s store;
  uint8_t record[72] = {'S','M','P','1'}, transaction[16] = {'S','M','P','1'};
  int ret = bkmemory_policy_load(root, owner, &policy);
  if (ret < 0) goto done;
  if (policy.enabled == enabled && !rotate && (!enabled || nonzero(policy.key))) goto done;
  if (policy.revision == UINT64_MAX) { ret = -EOVERFLOW; goto done; }
  if (rotate || (enabled && !nonzero(policy.key)))
    {
      uint8_t previous[32];
      memcpy(previous, policy.key, sizeof(previous));
      bool failed = !random || random(context, policy.key, 32) != 0 ||
                    !nonzero(policy.key) || (rotate && !memcmp(previous, policy.key, 32));
      mbedtls_platform_zeroize(previous, sizeof(previous));
      if (failed) { ret = -EIO; goto done; }
    }
  record[4] = enabled ? 1 : 0;
  memcpy(record + 8, policy.key, 32);
  if (mbedtls_sha256(owner, 32, record + 40, 0) != 0) { ret = -EIO; goto done; }
  if (mkdir(root, 0700) < 0 && errno != EEXIST) { ret = -errno; goto done; }
  ret = bkprov_store_open(&store, root);
  if (ret < 0) goto done;
  uint64_t revision = policy.revision + 1;
  for (int i = 11; i >= 4; i--) { transaction[i] = revision; revision >>= 8; }
  transaction[12] = record[4]; transaction[13] = rotate ? 1 : 0;
  ret = bkprov_store_commit(&store, policy.revision, transaction, record, sizeof(record));
done:
  mbedtls_platform_zeroize(&policy, sizeof(policy));
  mbedtls_platform_zeroize(record, sizeof(record));
  return ret;
}

int bkmemory_seal(const struct bkmemory_policy_s *policy,
                        const void *plain, size_t size, void *sealed,
                        size_t capacity, size_t *used,
                        bkmemory_random_t random, void *context)
{
  uint8_t hash[32];
  uint8_t *out = sealed;
  mbedtls_gcm_context gcm;
  if (used) *used = 0;
  if (!policy || !policy->enabled || !nonzero(policy->key)) return -EACCES;
  if (!plain || !sealed || !used || !random || !size || size > BKMEMORY_MAX ||
      capacity < size + BKMEMORY_OVERHEAD) return -EINVAL;
  mbedtls_gcm_init(&gcm);
  memcpy(out, "SMM1", 4);
  int ret = mbedtls_sha256(policy->key, 32, hash, 0);
  if (!ret) memcpy(out + 4, hash, 16);
  if (!ret) ret = random(context, out + 20, 12);
  put32(out + 32, (uint32_t)size);
  if (!ret) ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, policy->key, 256);
  if (!ret) ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, size,
      out + 20, 12, out, 36, plain, out + 52, 16, out + 36);
  mbedtls_gcm_free(&gcm);
  mbedtls_platform_zeroize(hash, sizeof(hash));
  if (ret) { mbedtls_platform_zeroize(out, size + 52); return -EIO; }
  *used = size + 52;
  return 0;
}
int bkmemory_open(const struct bkmemory_policy_s *policy,
                        const void *sealed, size_t size, void *plain,
                        size_t capacity, size_t *used)
{
  const uint8_t *input = sealed;
  uint8_t hash[32];
  mbedtls_gcm_context gcm;
  int ret = -EBADMSG;
  if (used) *used = 0;
  if (!plain || !used || capacity > BKMEMORY_MAX) return -EINVAL;
  if (!policy || !policy->enabled || !nonzero(policy->key)) { ret = -EACCES; goto fail; }
  if (!input || size <= 52 || size > BKMEMORY_MAX + 52 ||
      size - 52 > capacity || memcmp(input, "SMM1", 4) || get32(input + 32) != size - 52) goto fail;
  if (mbedtls_sha256(policy->key, 32, hash, 0) != 0) goto fail;
  if (memcmp(hash, input + 4, 16)) { ret = -ENOKEY; goto fail; }
  mbedtls_gcm_init(&gcm);
  ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, policy->key, 256);
  if (!ret) ret = mbedtls_gcm_auth_decrypt(&gcm, size - 52, input + 20, 12,
      input, 36, input + 36, 16, input + 52, plain);
  mbedtls_gcm_free(&gcm);
  if (ret) { ret = -EBADMSG; goto fail; }
  mbedtls_platform_zeroize(hash, sizeof(hash));
  *used = size - 52;
  return 0;
fail:
  mbedtls_platform_zeroize(hash, sizeof(hash));
  mbedtls_platform_zeroize(plain, capacity);
  return ret;
}

/* Keep removable-media I/O separate from the private policy store: SD is
 * allowed to hold ciphertext, but must never receive an owner or data key.
 */
static int snapshot_paths(const char *root, char path[192], char temp[192],
                          bool create)
{
  struct stat info;
  if (!root || root[0] != '/' || strlen(root) > 160) return -EINVAL;
  if (create && mkdir(root, 0700) < 0 && errno != EEXIST) return -errno;
  if (lstat(root, &info) < 0) return -errno;
  if (!S_ISDIR(info.st_mode)) return -ENOTDIR;
  snprintf(path, 192, "%s/history.enc", root);
  snprintf(temp, 192, "%s/history.tmp", root);
  return 0;
}

int bkmemory_restore(const char *root,
                           const struct bkmemory_policy_s *policy,
                           void *plain, size_t capacity, size_t *used)
{
  char path[192], temp[192];
  struct stat info;
  uint8_t *sealed = NULL;
  size_t size = 0, offset = 0;
  int fd = -1, ret;
  if (used) *used = 0;
  if (!plain || !used || !capacity || capacity > BKMEMORY_MAX) return -EINVAL;
  if (!policy || !policy->enabled || !nonzero(policy->key))
    { ret = -EACCES; goto done; }
  ret = snapshot_paths(root, path, temp, false);
  if (ret < 0) goto done;
  fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) { ret = -errno; goto done; }
  if (fstat(fd, &info) < 0) { ret = -errno; goto done; }
  if (!S_ISREG(info.st_mode) || info.st_size <= BKMEMORY_OVERHEAD ||
      info.st_size > (off_t)(capacity + BKMEMORY_OVERHEAD))
    { ret = -EBADMSG; goto done; }
  size = (size_t)info.st_size;
  sealed = malloc(size);
  if (!sealed) { ret = -ENOMEM; goto done; }
  while (offset < size)
    {
      ssize_t n = read(fd, sealed + offset, size - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { ret = n < 0 ? -errno : -EBADMSG; goto done; }
      offset += (size_t)n;
    }
  uint8_t extra;
  ssize_t n;
  do { n = read(fd, &extra, 1); } while (n < 0 && errno == EINTR);
  if (n != 0) { ret = n < 0 ? -errno : -EBADMSG; goto done; }
  ret = close(fd) < 0 ? -errno : 0;
  fd = -1;
  if (ret == 0) ret = bkmemory_open(policy, sealed, size, plain, capacity, used);
done:
  if (fd >= 0) close(fd);
  if (sealed) { mbedtls_platform_zeroize(sealed, size); free(sealed); }
  if (ret < 0) { mbedtls_platform_zeroize(plain, capacity); *used = 0; }
  return ret;
}
