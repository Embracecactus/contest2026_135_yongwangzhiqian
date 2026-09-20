/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_storage.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool block_sync;
static bool entered;
static bool fail_rename;
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
  pthread_mutex_lock(&lock); fail_rename = true; pthread_mutex_unlock(&lock);
  assert(bkprov_storage_commit(1, other, original, 3) == -EAGAIN);
  assert(receipt(other) == -EINPROGRESS);
  assert(bkprov_storage_refresh() == -EINPROGRESS);
  assert(bkprov_storage_stop() == -EINPROGRESS);
  assert(bkprov_storage_commit(1, other, original, 3) == -EINPROGRESS);
  puts("storage worker blocked I/O, owned copy and restart receipt: PASS");
  return 0;
}
