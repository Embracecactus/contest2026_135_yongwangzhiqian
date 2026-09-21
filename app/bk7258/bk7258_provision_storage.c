/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_storage.h"
#include "bk7258_provision_store.h"
#include "bk7258_provision_claim.h"
#include <errno.h>
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

enum job_e { JOB_IDLE, JOB_LOAD, JOB_COMMIT, JOB_IDENTITY, JOB_STOP };
struct storage_s
{
  pthread_t thread;
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
      else
        {
          s->result = ret;
          s->completed = true;
          if (ret == 0 || ret == -EINPROGRESS) s->status = ret;
          /* Candidate is retained for exact idempotent polls until refresh,
           * next transaction or shutdown, never owned by the BLE session. */
        }
      s->job = JOB_IDLE;
      void (*notify)(void) = g_notify;
      pthread_mutex_unlock(&g_lock);
      if (notify) notify();
      pthread_mutex_lock(&g_lock);
    }
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
      mbedtls_platform_zeroize(s->identity_candidate, sizeof(s->identity_candidate));
      s->identity_candidate_size = 0; s->identity_completed = false;
      s->job = JOB_LOAD;
      pthread_cond_signal(&g_wake);
    }
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
  pthread_mutex_unlock(&g_lock);
  int ret = pthread_join(s->thread, NULL);
  pthread_mutex_lock(&g_lock);
  if (ret == 0)
    {
      g_storage = NULL;
      mbedtls_platform_zeroize(s, sizeof(*s));
      free(s);
    }
  pthread_mutex_unlock(&g_lock);
  return -ret;
}
