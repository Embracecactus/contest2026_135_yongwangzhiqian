/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_storage.h"
#include "bk7258_provision_store.h"
#include "bk7258_provision_claim.h"
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mbedtls/sha256.h>
#ifdef __NuttX__
#include <sys/statfs.h>
#include <nuttx/fs/fs.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/platform_util.h>

enum job_e { JOB_IDLE, JOB_LOAD, JOB_COMMIT, JOB_IDENTITY, JOB_RESET, JOB_STOP };
struct storage_s
{
  pthread_t thread;
  bool stopped;
  struct bkprov_store_s store;
  struct bkprov_store_s identity_store;
  int identity_status;
  int identity_result;
  bool identity_completed;
  size_t identity_size;
  size_t identity_candidate_size;
  uint8_t identity[8192];
  uint8_t identity_candidate[8192];
  char root[150];
  enum job_e job;
  int status;
  int result;
  bool completed;
  bool reset_completed;
  int reset_result;
  int (*reset_cleanup)(void);
  int reset_receipt_status;
  uint8_t reset_receipt_transaction[16];
  uint64_t revision;
  uint64_t expected;
  size_t size;
  size_t candidate_size;
  uint8_t transaction[16];
  uint8_t candidate_transaction[16];
  uint8_t bundle[BKPROV_BUNDLE_MAX];
  uint8_t candidate[BKPROV_BUNDLE_MAX];
};
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static struct storage_s *g_storage;
static void (*g_notify)(void);

static bool reset_marker(const void *record, size_t size)
{
  return size == 4 && memcmp(record, "SRV1", 4) == 0;
}

/* This is deliberately a public, fixed-size acknowledgement only: it has no
 * owner record, revision, configuration or identity material.  It is written
 * before removing SRV1, so an interrupted final deletion remains a reset
 * pending on the next load rather than a false completion. */
static int load_reset_receipt(const char *root, uint8_t transaction[16])
{
  char path[192];
  uint8_t record[20];
  struct stat info;
  if (snprintf(path, sizeof(path), "%s/reset-receipt", root) >= (int)sizeof(path))
    return -ENAMETOOLONG;
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) return -errno;
  int ret = fstat(fd, &info) < 0 ? -errno : 0;
  if (ret == 0 && (!S_ISREG(info.st_mode) || info.st_size != (off_t)sizeof(record)))
    ret = -EBADMSG;
  size_t done = 0;
  while (ret == 0 && done < sizeof(record))
    {
      ssize_t n = read(fd, record + done, sizeof(record) - done);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { ret = n == 0 ? -EIO : -errno; break; }
      done += (size_t)n;
    }
  if (close(fd) < 0 && ret == 0) ret = -errno;
  if (ret == 0 && memcmp(record, "SRR1", 4)) ret = -EBADMSG;
  if (ret == 0) memcpy(transaction, record + 4, 16);
  mbedtls_platform_zeroize(record, sizeof(record));
  return ret;
}

static int save_reset_receipt(const char *root, const uint8_t transaction[16])
{
  char active[192], pending[192];
  uint8_t record[20] = {'S', 'R', 'R', '1'};
  if (snprintf(active, sizeof(active), "%s/reset-receipt", root) >= (int)sizeof(active) ||
      snprintf(pending, sizeof(pending), "%s/reset-receipt.pending", root) >= (int)sizeof(pending))
    return -ENAMETOOLONG;
  memcpy(record + 4, transaction, 16);
  if (unlink(pending) < 0 && errno != ENOENT) return -errno;
  int fd = open(pending, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK, 0600);
  if (fd < 0) return -errno;
  size_t done = 0;
  int ret = 0;
  while (done < sizeof(record))
    {
      ssize_t n = write(fd, record + done, sizeof(record) - done);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { ret = n == 0 ? -EIO : -errno; break; }
      done += (size_t)n;
    }
  if (ret == 0 && fsync(fd) < 0) ret = -errno;
  if (close(fd) < 0 && ret == 0) ret = -errno;
  if (ret == 0 && rename(pending, active) < 0) ret = -EINPROGRESS;
  if (ret == 0) ret = bkprov_store_sync_directory(root);
  if (ret < 0) (void)unlink(pending);
  mbedtls_platform_zeroize(record, sizeof(record));
  return ret;
}

/* Delete only declared user records. Keep identity, the revocation marker
 * and voice-ota's firmware transaction; never touch mounts or trust counters. */
static int reset_user_tree(const char *path, unsigned int depth, unsigned int *budget)
{
  struct stat st;
  if (depth > 8 || !*budget) return -E2BIG;
  (*budget)--;
  if (lstat(path, &st) < 0) return errno == ENOENT ? 0 : -errno;
  if (S_ISREG(st.st_mode)) return unlink(path) == 0 ? 0 : -errno;
  if (!S_ISDIR(st.st_mode)) return -EPERM;
  DIR *dir = opendir(path);
  if (!dir) return -errno;
  int ret = 0;
  for (;;)
    {
      errno = 0;
      struct dirent *entry = readdir(dir);
      if (!entry) { if (errno) ret = -errno; break; }
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
      char child[320];
      if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child))
        { ret = -ENAMETOOLONG; break; }
      ret = reset_user_tree(child, depth + 1, budget);
      if (ret) break;
    }
  if (closedir(dir) < 0 && !ret) ret = -errno;
  if (!ret && rmdir(path) < 0) ret = -errno;
  return ret;
}

static int reset_user_records(const char *root)
{
  static const char *const names[] = {
    "memory-policy", "memory-snapshot", "cloud-models", "voice-volume", "wake-models"
  };
  unsigned int budget = 4096;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
      char path[192];
      if (snprintf(path, sizeof(path), "%s/%s", root, names[i]) >= (int)sizeof(path))
        return -ENAMETOOLONG;
      int ret = reset_user_tree(path, 0, &budget);
      if (ret) return ret;
    }
  return bkprov_store_sync_directory(root);
}

void bkprov_storage_set_notify(void (*notify)(void))
{
  pthread_mutex_lock(&g_lock);
  g_notify = notify;
  pthread_mutex_unlock(&g_lock);
}

static int prepare_directory(const char *root)
{
  struct stat info;
  if (lstat(root, &info) == 0) return S_ISDIR(info.st_mode) ? 0 : -ENOTDIR;
  if (errno != ENOENT) return -errno;
  char parent[160];
  size_t size = strlen(root);
  if (size >= sizeof(parent) || size < 2) return -EINVAL;
  memcpy(parent, root, size + 1);
  char *slash = strrchr(parent, '/');
  if (slash == NULL) return -EINVAL;
  if (slash == parent) slash[1] = 0; else *slash = 0;
  if (lstat(parent, &info) < 0) return -errno;
  if (!S_ISDIR(info.st_mode)) return -ENOTDIR;
  int check = bkprov_store_check_filesystem(parent);
  if (check < 0) return check;
  if (mkdir(root, 0700) < 0 && errno != EEXIST) return -errno;
#ifndef __NuttX__
  int fd = open(parent, O_RDONLY | O_DIRECTORY);
  if (fd < 0) return -errno;
  int ret = fsync(fd) < 0 ? -errno : 0;
  if (close(fd) < 0 && ret == 0) ret = -errno;
  return ret;
#else
  return 0;
#endif
}

static void *worker(void *context)
{
  struct storage_s *s = context;
  pthread_mutex_lock(&g_lock);
  for (;;)
    {
      while (s->job == JOB_IDLE) pthread_cond_wait(&g_wake, &g_lock);
      enum job_e job = s->job;
      if (job == JOB_STOP) break;
      /* job remains non-idle while unlocked. Public readers never touch
       * these buffers until the worker publishes completion under g_lock. */
      pthread_mutex_unlock(&g_lock);
      int ret;
      if (job == JOB_LOAD)
        {
          mbedtls_platform_zeroize(s->bundle, sizeof(s->bundle));
          s->size = 0;
          s->revision = 0;
          memset(s->transaction, 0, 16);
          ret = prepare_directory(s->root);
          if (ret == 0) ret = bkprov_store_open(&s->store, s->root);
          if (ret == -ENOENT) ret = -ENODEV;
          int identity_ret = ret;
          if (ret == 0)
            ret = bkprov_store_load(&s->store, s->bundle, sizeof(s->bundle),
                                    &s->size, &s->revision, s->transaction);
          if (ret == 0 && reset_marker(s->bundle, s->size)) ret = -EOWNERDEAD;
          memset(s->reset_receipt_transaction, 0, 16);
          s->reset_receipt_status = load_reset_receipt(s->root,
                                                       s->reset_receipt_transaction);
          char identity_root[160];
          snprintf(identity_root, sizeof(identity_root), "%s/identity", s->root);
          if (identity_ret == 0) identity_ret = prepare_directory(identity_root);
          if (identity_ret == 0) identity_ret = bkprov_store_open(&s->identity_store, identity_root);
          uint64_t identity_revision;
          mbedtls_platform_zeroize(s->identity, sizeof(s->identity));
          s->identity_size = 0;
          if (identity_ret == 0)
            identity_ret = bkprov_store_load(&s->identity_store, s->identity, sizeof(s->identity),
                                              &s->identity_size, &identity_revision, NULL);
          s->identity_status = identity_ret;
        }
      else if (job == JOB_IDENTITY)
        {
          uint8_t digest[32];
          ret = mbedtls_sha256(s->identity_candidate, s->identity_candidate_size, digest, 0);
          if (ret == 0)
            ret = bkprov_store_commit(&s->identity_store, 0, digest,
                                        s->identity_candidate, s->identity_candidate_size);
          mbedtls_platform_zeroize(digest, sizeof(digest));
          if (ret == 0)
            {
              memcpy(s->identity, s->identity_candidate, s->identity_candidate_size);
              s->identity_size = s->identity_candidate_size;
            }
        }
      else if (job == JOB_RESET)
        {
          /* The marker remains selected until every product-owned replica
           * has been cleaned. No format, identity erase or broad tree erase. */
          ret = s->reset_cleanup();
          if (ret == 0) ret = reset_user_records(s->root);
          if (ret == 0 && unlink(s->store.pending) < 0 && errno != ENOENT)
            ret = -errno;
          if (ret == 0) ret = save_reset_receipt(s->root, s->transaction);
          if (ret == 0 && unlink(s->store.active) < 0) ret = -errno;
          if (ret == 0) ret = bkprov_store_sync_directory(s->store.directory);
          if (ret == 0)
            {
              uint8_t reset_transaction[16];
              memcpy(reset_transaction, s->transaction, sizeof(reset_transaction));
              mbedtls_platform_zeroize(s->bundle, sizeof(s->bundle));
              mbedtls_platform_zeroize(s->candidate, sizeof(s->candidate));
              s->size = s->candidate_size = 0;
              s->revision = 0;
              memset(s->transaction, 0, 16);
              memset(s->candidate_transaction, 0, 16);
              memcpy(s->reset_receipt_transaction, reset_transaction, 16);
              mbedtls_platform_zeroize(reset_transaction, sizeof(reset_transaction));
              s->reset_receipt_status = 0;
            }
        }
      else
        {
          ret = bkprov_store_commit(&s->store, s->expected,
                                    s->candidate_transaction, s->candidate,
                                    s->candidate_size);
          if (ret == 0)
            {
              mbedtls_platform_zeroize(s->bundle, sizeof(s->bundle));
              memcpy(s->bundle, s->candidate, s->candidate_size);
              s->size = s->candidate_size;
              s->revision = s->expected + 1;
              memcpy(s->transaction, s->candidate_transaction, 16);
            }
        }
      pthread_mutex_lock(&g_lock);
      if (job == JOB_LOAD) s->status = ret;
      else if (job == JOB_IDENTITY)
        {
          s->identity_result = ret;
          s->identity_completed = true;
          if (ret == 0 || ret == -EINPROGRESS) s->identity_status = ret;
        }
      else if (job == JOB_RESET)
        {
          s->reset_result = ret;
          s->reset_completed = true;
          s->status = ret == 0 ? -ENOENT :
                      ret == -EINPROGRESS ? -EINPROGRESS : -EOWNERDEAD;
        }
      else
        {
          s->result = ret;
          s->completed = true;
          if (ret == 0 || ret == -EINPROGRESS) s->status = ret;
          if (ret == 0 && reset_marker(s->bundle, s->size))
            {
              s->status = -EOWNERDEAD;
              s->reset_completed = false;
              s->reset_result = 0;
            }
          /* Candidate is retained for exact idempotent polls until refresh,
           * next transaction or shutdown, never owned by the BLE session. */
        }
      s->job = JOB_IDLE;
      void (*notify)(void) = g_notify;
      pthread_mutex_unlock(&g_lock);
      if (notify) notify();
      pthread_mutex_lock(&g_lock);
    }
  /* The product coordinator belongs to a different NuttX task group from
   * the startup owner. Publish the final buffer-release boundary explicitly;
   * pthread_join cannot reap another group's thread. */
  s->stopped = true;
  pthread_cond_broadcast(&g_wake);
  pthread_mutex_unlock(&g_lock);
  return NULL;
}

int bkprov_storage_start(const char *root)
{
  if (root == NULL || root[0] != '/' || strlen(root) >= 150) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  if (g_storage != NULL) { pthread_mutex_unlock(&g_lock); return -EALREADY; }
  struct storage_s *s = calloc(1, sizeof(*s));
  if (s == NULL) { pthread_mutex_unlock(&g_lock); return -ENOMEM; }
  memcpy(s->root, root, strlen(root) + 1);
  s->job = JOB_LOAD;
  s->status = -EAGAIN;
  s->identity_status = -EAGAIN;
  pthread_attr_t attr;
  int ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      size_t stack_size = 16384;
#ifdef PTHREAD_STACK_MIN
      if (stack_size < PTHREAD_STACK_MIN) stack_size = PTHREAD_STACK_MIN;
#endif
      ret = pthread_attr_setstacksize(&attr, stack_size);
      if (ret == 0) ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      if (ret == 0) ret = pthread_create(&s->thread, &attr, worker, s);
      pthread_attr_destroy(&attr);
    }
  if (ret == 0) g_storage = s;
  else free(s);
  pthread_mutex_unlock(&g_lock);
  return -ret;
}

int bkprov_storage_identity(void *record, size_t capacity, size_t *size)
{
  if (record == NULL || size == NULL) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret = s == NULL ? -ENODEV : s->job == JOB_LOAD || s->job == JOB_IDENTITY ||
            s->job == JOB_STOP ? -EAGAIN : s->identity_status;
  if (ret == 0)
    {
      if (capacity < s->identity_size) ret = -ENOSPC;
      else { memcpy(record, s->identity, s->identity_size); *size = s->identity_size; }
    }
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_identity_install(const void *record, size_t size)
{
  if (record == NULL || size < 48 || size > 8192 ||
      (memcmp(record, "BPI1", 4) && memcmp(record, "BPI2", 4))) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret;
  if (s == NULL) ret = -ENODEV;
  else if (s->job == JOB_LOAD || s->job == JOB_STOP) ret = -EAGAIN;
  else if (s->identity_candidate_size == size && !memcmp(s->identity_candidate, record, size))
    ret = s->identity_completed ? s->identity_result : -EAGAIN;
  else if (s->job != JOB_IDLE) ret = -EBUSY;
  else if (s->identity_status == 0)
    ret = s->identity_size == size && !memcmp(s->identity, record, size) ? 0 : -EEXIST;
  else if (s->identity_status != -ENOENT) ret = s->identity_status;
  else
    {
      mbedtls_platform_zeroize(s->identity_candidate, sizeof(s->identity_candidate));
      memcpy(s->identity_candidate, record, size);
      s->identity_candidate_size = size;
      s->identity_completed = false;
      s->job = JOB_IDENTITY;
      pthread_cond_signal(&g_wake);
      ret = -EAGAIN;
    }
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_snapshot(void *bundle, size_t capacity, size_t *size,
                            uint64_t *revision, uint8_t transaction[16])
{
  if (bundle == NULL || size == NULL || revision == NULL || transaction == NULL)
    return -EINVAL;
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret = s == NULL ? -ENODEV : s->job != JOB_IDLE ? -EAGAIN : s->status;
  if (ret == 0)
    {
      if (capacity < s->size) ret = -ENOSPC;
      else
        {
          memcpy(bundle, s->bundle, s->size);
          *size = s->size; *revision = s->revision;
          memcpy(transaction, s->transaction, 16);
        }
    }
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_commit(uint64_t expected, const uint8_t transaction[16],
                          const void *bundle, size_t size)
{
  static const uint8_t zero[16];
  if (transaction == NULL || bundle == NULL || size == 0 ||
      size > BKPROV_BUNDLE_MAX || expected == UINT64_MAX ||
      memcmp(transaction, zero, 16) == 0) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret;
  if (s == NULL) ret = -ENODEV;
  else if (s->job == JOB_LOAD || s->job == JOB_STOP) ret = -EAGAIN;
  else if (memcmp(s->candidate_transaction, transaction, 16) == 0)
    {
      if (s->expected != expected || s->candidate_size != size ||
          memcmp(s->candidate, bundle, size)) ret = -EINVAL;
      else ret = s->completed ? s->result : -EAGAIN;
    }
  else if (s->job != JOB_IDLE) ret = -EBUSY;
  else if (s->status != 0 && s->status != -ENOENT) ret = s->status;
  else if (s->revision != expected) ret = -ESTALE;
  else
    {
      mbedtls_platform_zeroize(s->candidate, sizeof(s->candidate));
      memcpy(s->candidate, bundle, size);
      memcpy(s->candidate_transaction, transaction, 16);
      s->candidate_size = size; s->expected = expected;
      s->completed = false; s->job = JOB_COMMIT;
      pthread_cond_signal(&g_wake);
      ret = -EAGAIN;
    }
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_refresh(void)
{
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret = s == NULL ? -ENODEV : s->job != JOB_IDLE ? -EBUSY : 0;
  /* A read through the same mounted filesystem cannot prove a previously
   * failed durable publication survived power loss. Preserve uncertainty
   * until a fresh worker/boot reload instead of clearing it by readback. */
  if (ret == 0 && (s->status == -EINPROGRESS || s->identity_status == -EINPROGRESS)) ret = -EINPROGRESS;
  if (ret == 0)
    {
      mbedtls_platform_zeroize(s->candidate, sizeof(s->candidate));
      memset(s->candidate_transaction, 0, 16);
      s->completed = false; s->candidate_size = 0;
      s->reset_completed = false;
      mbedtls_platform_zeroize(s->identity_candidate, sizeof(s->identity_candidate));
      s->identity_candidate_size = 0; s->identity_completed = false;
      s->job = JOB_LOAD;
      pthread_cond_signal(&g_wake);
    }
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_reset_request(uint64_t expected, const uint8_t transaction[16])
{
  if (expected == 0) return -EPERM;
  return bkprov_storage_commit(expected, transaction, "SRV1", 4);
}

int bkprov_storage_reset_pending(void)
{
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret = s == NULL ? -ENODEV : s->job != JOB_IDLE ? -EAGAIN :
            s->status == -EOWNERDEAD ? 1 :
            s->status == 0 || s->status == -ENOENT ? 0 : s->status;
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_reset_finish(int (*cleanup)(void))
{
  if (cleanup == NULL) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret;
  if (s == NULL) ret = -ENODEV;
  else if (s->job != JOB_IDLE) ret = -EAGAIN;
  else if (s->reset_completed) ret = s->reset_result;
  else if (s->status != -EOWNERDEAD) ret = -EPERM;
  else
    {
      s->reset_cleanup = cleanup;
      s->job = JOB_RESET;
      pthread_cond_signal(&g_wake);
      ret = -EAGAIN;
    }
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_reset_receipt(const uint8_t transaction[16])
{
  if (transaction == NULL) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret = s == NULL ? -ENODEV : s->job != JOB_IDLE ? -EAGAIN : 0;
  /* 读取失败或提交结果不确定不能被解释成事务不存在。 */
  if (ret == 0 && s->status != 0 && s->status != -ENOENT &&
      s->status != -EOWNERDEAD)
    ret = s->status;
  if (ret == 0 && s->status == -EOWNERDEAD &&
      !memcmp(s->transaction, transaction, 16))
    ret = BKPROV_STORAGE_RESET_RECEIPT_PENDING;
  else if (ret == 0 && s->reset_receipt_status != 0 &&
           s->reset_receipt_status != -ENOENT)
    ret = s->reset_receipt_status;
  else if (ret == 0 && s->reset_receipt_status == 0 &&
           !memcmp(s->reset_receipt_transaction, transaction, 16))
    ret = BKPROV_STORAGE_RESET_RECEIPT_COMPLETED;
  else if (ret == 0)
    ret = BKPROV_STORAGE_RESET_RECEIPT_ABSENT;
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_receipt(const uint8_t transaction[16])
{
  if (transaction == NULL) return -EINVAL;
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  int ret = s == NULL ? -ENODEV : s->job != JOB_IDLE ? -EAGAIN : s->status;
  if (ret == -ENOENT) ret = 0;
  else if (ret == 0)
    ret = memcmp(s->transaction, transaction, 16) == 0 ? 1 : -EINPROGRESS;
  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkprov_storage_stop(void)
{
  pthread_mutex_lock(&g_lock);
  struct storage_s *s = g_storage;
  if (s == NULL) { pthread_mutex_unlock(&g_lock); return 0; }
  if (s->job != JOB_IDLE) { pthread_mutex_unlock(&g_lock); return -EBUSY; }
  if (s->status == -EINPROGRESS || s->identity_status == -EINPROGRESS)
    { pthread_mutex_unlock(&g_lock); return -EINPROGRESS; }
  s->job = JOB_STOP;
  pthread_cond_signal(&g_wake);
  while (!s->stopped) pthread_cond_wait(&g_wake, &g_lock);
  g_storage = NULL;
  mbedtls_platform_zeroize(s, sizeof(*s));
  free(s);
  pthread_mutex_unlock(&g_lock);
  return 0;
}
