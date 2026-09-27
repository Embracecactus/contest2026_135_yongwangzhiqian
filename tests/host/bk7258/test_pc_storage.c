/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_storage.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool blocked, entered;
static atomic_int failure;
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  struct stat st;
  assert(fstat(fd, &st) == 0);
  pthread_mutex_lock(&lock);
  if (blocked)
    {
      entered = true;
      pthread_cond_signal(&wake);
      while (blocked) pthread_cond_wait(&wake, &lock);
    }
  pthread_mutex_unlock(&lock);
  if ((atomic_load(&failure) == 1 && S_ISREG(st.st_mode)) ||
      (atomic_load(&failure) == 2 && S_ISDIR(st.st_mode)))
    { errno = EIO; return -1; }
  return __real_fsync(fd);
}
static void tick(void)
{
  const struct timespec delay = {0, 1000000};
  nanosleep(&delay, NULL);
}
static const uint8_t phone[32] = {11}, owner2[32] = {22};
static const uint8_t pc[32] = {44}, client[16] = {33};
static const uint8_t tx[16] = {1}, next[16] = {2}, config_tx[16] = {7};
static int receipt(const uint8_t transaction[16])
{
  int ret = -EAGAIN;
  for (int i = 0; i < 3000 && ret == -EAGAIN; i++)
    { ret = bkprov_storage_receipt(transaction); if (ret == -EAGAIN) tick(); }
  return ret;
}
static int load(uint64_t revision, const uint8_t key[32])
{
  int ret = -EAGAIN;
  for (int i = 0; i < 3000 && ret == -EAGAIN; i++)
    { ret = bkprov_storage_pc_load(revision, key); if (ret == -EAGAIN) tick(); }
  return ret;
}
static int set(uint64_t config, uint64_t expected, const uint8_t transaction[16],
                 uint32_t caps)
{
  int ret = -EAGAIN;
  for (int i = 0; i < 3000 && ret == -EAGAIN; i++)
    {
      ret = bkprov_storage_pc_set(config, expected, transaction,
                                  caps ? client : NULL, caps ? pc : NULL, caps);
      if (ret == -EAGAIN) tick();
    }
  return ret;
}
static int cleanup(void) { return 0; }
static void denied(uint64_t revision, int expected)
{
  struct bkprov_pc_snapshot_s view;
  memset(&view, 77, sizeof(view));
  assert(bkprov_storage_pc_snapshot(revision, &view) == expected);
  const uint8_t *bytes = (const uint8_t *)&view;
  for (size_t i = 0; i < sizeof(view); i++) assert(bytes[i] == 0);
}
int main(int argc, char **argv)
{
  assert(argc == 3);
  assert(bkprov_storage_start(argv[2]) == 0 && receipt(config_tx) == 0);
  assert(bkprov_storage_pc_load(0, phone) == -ENOENT);
  assert(bkprov_storage_commit(0, config_tx, "owner fixture", 13) == -EAGAIN);
  assert(receipt(config_tx) == 1);
  assert(load(1, phone) == 0);
  struct bkprov_pc_snapshot_s view;
  assert(bkprov_storage_pc_snapshot(1, &view) == 0 && view.capabilities == 0);
  if (!strcmp(argv[1], "blocked-copy"))
    {
      uint8_t borrowed[32]; memcpy(borrowed, pc, 32);
      pthread_mutex_lock(&lock); blocked = true; pthread_mutex_unlock(&lock);
      assert(bkprov_storage_pc_set(1, 0, tx, client, borrowed, 3) == -EAGAIN);
      memset(borrowed, 99, sizeof(borrowed));
      bool ready = false;
      for (int i = 0; i < 3000 && !ready; i++)
        { pthread_mutex_lock(&lock); ready = entered; pthread_mutex_unlock(&lock); if (!ready) tick(); }
      assert(ready);
      denied(1, -EAGAIN);
      assert(bkprov_storage_pc_set(1, 0, tx, client, pc, 3) == -EAGAIN);
      assert(bkprov_storage_pc_set(1, 0, tx, client, borrowed, 3) == -EINVAL);
      assert(bkprov_storage_pc_set(1, 0, next, client, pc, 3) == -EBUSY);
      assert(bkprov_storage_stop() == -EBUSY && bkprov_storage_refresh() == -EBUSY);
      pthread_mutex_lock(&lock); blocked = false; pthread_cond_signal(&wake); pthread_mutex_unlock(&lock);
      assert(set(1, 0, tx, 3) == 0);
    }
  else assert(set(1, 0, tx, 3) == 0);
  assert(bkprov_storage_pc_snapshot(1, &view) == 0);
  assert(view.revision == 1 && view.capabilities == 3 && !memcmp(view.key, pc, 32));
  assert(!memcmp(view.client, client, 16) && !memcmp(view.transaction, tx, 16));
  if (!strcmp(argv[1], "revision"))
    {
      assert(set(1, 0, tx, 3) == 0);
      assert(set(1, 0, tx, 1) == -EINVAL);
      assert(set(1, 0, next, 3) == -ESTALE);
      assert(bkprov_storage_commit(1, next, "new owner", 9) == -EAGAIN);
      assert(receipt(next) == 1);
      denied(1, -ESTALE);
      assert(set(1, 1, next, 3) == -ESTALE);
      assert(load(2, owner2) == 0);
      assert(bkprov_storage_pc_snapshot(2, &view) == 0);
      assert(view.revision == 1 && view.capabilities == 0);
      for (size_t i = 0; i < sizeof(view.key); i++) assert(view.key[i] == 0);
      assert(set(2, 1, next, 1) == 0);
      assert(bkprov_storage_pc_snapshot(2, &view) == 0 && view.revision == 2);
    }
  else if (!strcmp(argv[1], "reopen"))
    {
      assert(bkprov_storage_stop() == 0);
      assert(bkprov_storage_start(argv[2]) == 0 && receipt(config_tx) == 1);
      assert(load(1, phone) == 0);
      assert(bkprov_storage_pc_snapshot(1, &view) == 0);
      assert(view.revision == 1 && !memcmp(view.key, pc, 32));
      assert(set(1, 1, next, 0) == 0);
      assert(bkprov_storage_stop() == 0);
      assert(bkprov_storage_start(argv[2]) == 0 && receipt(config_tx) == 1);
      assert(load(1, phone) == 0);
      assert(bkprov_storage_pc_snapshot(1, &view) == 0);
      assert(view.revision == 2 && view.capabilities == 0);
    }
  else if (!strcmp(argv[1], "write-failure"))
    {
      atomic_store(&failure, 1);
      assert(set(1, 1, next, 0) == -EIO);
      atomic_store(&failure, 0);
      assert(set(1, 1, next, 0) == -EIO); /* Exact retry reports the result. */
      assert(bkprov_storage_pc_snapshot(1, &view) == 0 && view.capabilities == 3);
      const uint8_t retry[16] = {3};
      assert(set(1, 1, retry, 0) == 0);
    }
  else if (!strcmp(argv[1], "unknown"))
    {
      atomic_store(&failure, 2);
      assert(set(1, 1, next, 0) == -EINPROGRESS);
      atomic_store(&failure, 0);
      denied(1, -EINPROGRESS);
      assert(load(1, phone) == -EINPROGRESS);
      assert(bkprov_storage_refresh() == -EINPROGRESS);
      assert(bkprov_storage_stop() == -EINPROGRESS);
      puts("CONTRACT_PASS"); return 0;
    }
  else if (!strcmp(argv[1], "reset"))
    {
      int ret = -EAGAIN;
      for (int i = 0; i < 3000 && ret == -EAGAIN; i++)
        { ret = bkprov_storage_reset_request(1, next); if (ret == -EAGAIN) tick(); }
      assert(ret == 0);
      denied(2, -EOWNERDEAD);
      ret = -EAGAIN;
      for (int i = 0; i < 3000 && ret == -EAGAIN; i++)
        { ret = bkprov_storage_reset_finish(cleanup); if (ret == -EAGAIN) tick(); }
      assert(ret == 0);
      denied(0, -ENOENT);
      assert(bkprov_storage_commit(0, config_tx, "same owner", 10) == -EAGAIN);
      assert(receipt(config_tx) == 1 && load(1, phone) == 0);
      assert(bkprov_storage_pc_snapshot(1, &view) == 0 && view.capabilities == 0);
    }
  else assert(!strcmp(argv[1], "blocked-copy"));
  assert(bkprov_storage_stop() == 0);
  puts("CONTRACT_PASS"); return 0;
}
