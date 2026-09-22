/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_storage.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool block_sync;
static bool entered;
static bool fail_rename;
static int cleanup_result;
static int cleanup_calls;
static int reset_cleanup(void) { cleanup_calls++; return cleanup_result; }
int __real_rename(const char *from, const char *to);
int __wrap_rename(const char *from, const char *to);
int __wrap_rename(const char *from, const char *to)
{
  pthread_mutex_lock(&lock);
  bool fail = fail_rename;
  pthread_mutex_unlock(&lock);
  if (fail) { errno = EIO; return -1; }
  return __real_rename(from, to);
}
int __real_fsync(int fd);
int __wrap_fsync(int fd);
int __wrap_fsync(int fd)
{
  pthread_mutex_lock(&lock);
  if (block_sync)
    {
      entered = true;
      while (block_sync) pthread_cond_wait(&wake, &lock);
    }
  pthread_mutex_unlock(&lock);
  return __real_fsync(fd);
}
static void tick(void)
{
  struct timespec delay = {0, 1000000};
  nanosleep(&delay, NULL);
}
static int receipt(const uint8_t tx[16])
{
  int ret = -EAGAIN;
  for (int i = 0; i < 3000 && ret == -EAGAIN; i++)
    { ret = bkprov_storage_receipt(tx); if (ret == -EAGAIN) tick(); }
  return ret;
}
int main(int argc, char **argv)
{
  uint8_t tx[16] = {1}, other[16] = {2}, candidate[3] = {5, 6, 7};
  const uint8_t original[3] = {5, 6, 7};
  uint8_t output[16], actual_tx[16];
  size_t size = 0;
  uint64_t revision = 0;
  assert(argc == 2);
  assert(bkprov_storage_start(argv[1]) == 0);
  assert(bkprov_storage_start(argv[1]) == -EALREADY);
  assert(receipt(tx) == 0);
  uint8_t identity[48] = {'B', 'P', 'I', '1'}, identity_out[48];
  identity[5] = 1; identity[16] = 42;
  assert(bkprov_storage_identity(identity_out, sizeof(identity_out), &size) == -ENOENT);
  int identity_ret = bkprov_storage_identity_install(identity, sizeof(identity));
  assert(identity_ret == -EAGAIN);
  for (int i = 0; i < 3000 && identity_ret == -EAGAIN; i++)
    { tick(); identity_ret = bkprov_storage_identity_install(identity, sizeof(identity)); }
  assert(identity_ret == 0);
  assert(bkprov_storage_identity(identity_out, sizeof(identity_out), &size) == 0);
  assert(size == sizeof(identity) && !memcmp(identity_out, identity, sizeof(identity)));
  identity[16]++;
  assert(bkprov_storage_identity_install(identity, sizeof(identity)) == -EEXIST);
  identity[16]--;

  pthread_mutex_lock(&lock); block_sync = true; pthread_mutex_unlock(&lock);
  assert(bkprov_storage_commit(0, tx, candidate, 3) == -EAGAIN);
  memset(candidate, 0, sizeof(candidate));
  bool blocked = false;
  for (int i = 0; i < 3000 && !blocked; i++)
    {
      pthread_mutex_lock(&lock); blocked = entered; pthread_mutex_unlock(&lock);
      if (!blocked) tick();
    }
  assert(blocked);
  /* The caller can release its candidate, disconnect, poll and attempt stop
   * while actual fsync is blocked. None of these wait for the filesystem. */
  assert(bkprov_storage_commit(0, tx, original, 3) == -EAGAIN);
  assert(bkprov_storage_commit(0, other, original, 3) == -EBUSY);
  assert(bkprov_storage_commit(0, tx, candidate, 3) == -EINVAL);
  assert(bkprov_storage_snapshot(output, sizeof(output), &size, &revision, actual_tx) == -EAGAIN);
  assert(bkprov_storage_stop() == -EBUSY);
  assert(bkprov_storage_refresh() == -EBUSY);
  pthread_mutex_lock(&lock); block_sync = false; pthread_cond_signal(&wake); pthread_mutex_unlock(&lock);
  assert(receipt(tx) == 1);
  assert(bkprov_storage_commit(0, tx, original, 3) == 0);
  assert(bkprov_storage_snapshot(output, sizeof(output), &size, &revision, actual_tx) == 0);
  assert(size == 3 && revision == 1 && !memcmp(output, original, 3));
  assert(!memcmp(actual_tx, tx, 16));
  assert(bkprov_storage_receipt(other) == -EINPROGRESS);
  assert(bkprov_storage_commit(0, other, original, 3) == -ESTALE);
  assert(bkprov_storage_stop() == 0);
  assert(bkprov_storage_start(argv[1]) == 0);
  assert(receipt(tx) == 1);
  assert(bkprov_storage_identity(identity_out, sizeof(identity_out), &size) == 0);
  assert(size == sizeof(identity) && !memcmp(identity_out, identity, sizeof(identity)));
  assert(bkprov_storage_identity_install(identity, sizeof(identity)) == 0);
  char user_dir[256], user_file[280], ota_dir[256], ota_file[280];
  assert(snprintf(user_dir, sizeof(user_dir), "%s/memory-snapshot", argv[1]) < (int)sizeof(user_dir));
  assert(snprintf(ota_dir, sizeof(ota_dir), "%s/voice-ota", argv[1]) < (int)sizeof(ota_dir));
  assert(mkdir(user_dir, 0700) == 0 && mkdir(ota_dir, 0700) == 0);
  snprintf(user_file, sizeof(user_file), "%s/config.bin", user_dir);
  snprintf(ota_file, sizeof(ota_file), "%s/config.bin", ota_dir);
  FILE *sample = fopen(user_file, "wb");
  assert(sample && fwrite("user-data", 1, 9, sample) == 9 && fclose(sample) == 0);
  sample = fopen(ota_file, "wb");
  assert(sample && fwrite("firmware-intent", 1, 15, sample) == 15 && fclose(sample) == 0);
  uint8_t reset_tx[16] = {3};
  assert(bkprov_storage_reset_finish(reset_cleanup) == -EPERM);
  assert(bkprov_storage_reset_request(0, reset_tx) == -EPERM);
  int reset_ret = bkprov_storage_reset_request(1, reset_tx);
  for (int i = 0; i < 3000 && reset_ret == -EAGAIN; i++)
    { tick(); reset_ret = bkprov_storage_reset_request(1, reset_tx); }
  assert(reset_ret == 0 && bkprov_storage_reset_pending() == 1);
  assert(bkprov_storage_snapshot(output, sizeof(output), &size, &revision, actual_tx) == -EOWNERDEAD);
  assert(bkprov_storage_commit(2, other, original, 3) == -EOWNERDEAD);
  /* A restart resumes the explicit revocation marker, never an empty store. */
  assert(bkprov_storage_stop() == 0);
  assert(bkprov_storage_start(argv[1]) == 0);
  assert(receipt(reset_tx) == -EOWNERDEAD);
  assert(bkprov_storage_reset_pending() == 1);
  cleanup_result = -EIO;
  reset_ret = bkprov_storage_reset_finish(reset_cleanup);
  for (int i = 0; i < 3000 && reset_ret == -EAGAIN; i++)
    { tick(); reset_ret = bkprov_storage_reset_finish(reset_cleanup); }
  assert(reset_ret == -EIO && cleanup_calls == 1);
  assert(access(user_file, F_OK) == 0 && access(ota_file, F_OK) == 0);
  assert(bkprov_storage_reset_pending() == 1);
  assert(bkprov_storage_refresh() == 0);
  assert(receipt(reset_tx) == -EOWNERDEAD);
  cleanup_result = 0;
  reset_ret = bkprov_storage_reset_finish(reset_cleanup);
  for (int i = 0; i < 3000 && reset_ret == -EAGAIN; i++)
    { tick(); reset_ret = bkprov_storage_reset_finish(reset_cleanup); }
  assert(reset_ret == 0 && cleanup_calls == 2);
  assert(access(user_file, F_OK) < 0 && errno == ENOENT);
  assert(access(ota_file, F_OK) == 0);
  assert(bkprov_storage_reset_pending() == 0 && receipt(reset_tx) == 0);
  assert(bkprov_storage_identity(identity_out, sizeof(identity_out), &size) == 0);
  assert(size == sizeof(identity) && !memcmp(identity_out, identity, sizeof(identity)));
  assert(bkprov_storage_stop() == 0);
  assert(bkprov_storage_start(argv[1]) == 0);
  assert(receipt(reset_tx) == 0);
  assert(bkprov_storage_commit(0, tx, original, 3) == -EAGAIN);
  assert(receipt(tx) == 1);
  for (int cycle = 0; cycle < 2; cycle++)
    {
      reset_tx[0] = 4 + cycle;
      reset_ret = bkprov_storage_reset_request(1, reset_tx);
      for (int i = 0; i < 3000 && reset_ret == -EAGAIN; i++)
        { tick(); reset_ret = bkprov_storage_reset_request(1, reset_tx); }
      assert(reset_ret == 0);
      reset_ret = bkprov_storage_reset_finish(reset_cleanup);
      for (int i = 0; i < 3000 && reset_ret == -EAGAIN; i++)
        { tick(); reset_ret = bkprov_storage_reset_finish(reset_cleanup); }
      assert(reset_ret == 0 && cleanup_calls == 3 + cycle && receipt(reset_tx) == 0);
      assert(bkprov_storage_commit(0, tx, original, 3) == -EAGAIN);
      assert(receipt(tx) == 1);
    }
  pthread_mutex_lock(&lock); fail_rename = true; pthread_mutex_unlock(&lock);
  assert(bkprov_storage_commit(1, other, original, 3) == -EAGAIN);
  assert(receipt(other) == -EINPROGRESS);
  assert(bkprov_storage_refresh() == -EINPROGRESS);
  assert(bkprov_storage_stop() == -EINPROGRESS);
  assert(bkprov_storage_commit(1, other, original, 3) == -EINPROGRESS);
  puts("storage worker blocked I/O, owned copy, reset revocation/resume and restart receipt: PASS");
  return 0;
}
