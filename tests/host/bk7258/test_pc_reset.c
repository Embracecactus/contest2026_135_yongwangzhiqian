/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_pc_grants.h"
#include "bk7258_provision_storage.h"
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static atomic_int fault;
static char pc_root[256];
static ino_t pc_inode;
int __real_unlink(const char *path);
int __wrap_unlink(const char *path)
{
  if (atomic_load(&fault) == 1 && strstr(path, "/pc-grants/config.pending"))
    { errno = EIO; return -1; }
  return __real_unlink(path);
}
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  struct stat st;
  assert(fstat(fd, &st) == 0);
  if (atomic_load(&fault) == 2 && S_ISDIR(st.st_mode) && st.st_ino == pc_inode)
    { errno = EIO; return -1; }
  return __real_fsync(fd);
}
static void tick(void)
{
  const struct timespec delay = {0, 1000000};
  nanosleep(&delay, NULL);
}
static int cleanup(void) { return 0; }
static int wait_receipt(const uint8_t tx[16])
{
  int ret = -EAGAIN;
  for (unsigned int i = 0; i < 3000 && ret == -EAGAIN; i++)
    { ret = bkprov_storage_receipt(tx); if (ret == -EAGAIN) tick(); }
  return ret;
}
static int finish(void)
{
  int ret = -EAGAIN;
  for (unsigned int i = 0; i < 3000 && ret == -EAGAIN; i++)
    { ret = bkprov_storage_reset_finish(cleanup); if (ret == -EAGAIN) tick(); }
  return ret;
}
static void file(const char *path)
{
  FILE *f = fopen(path, "wb");
  assert(f && fwrite("keep", 1, 4, f) == 4 && fclose(f) == 0);
}
int main(int argc, char **argv)
{
  assert(argc == 3);
  const char *root = argv[2];
  char active[280], pending[280], unrelated[280], outside[280];
  snprintf(pc_root, sizeof(pc_root), "%s/pc-grants", root);
  snprintf(active, sizeof(active), "%s/config.bin", pc_root);
  snprintf(pending, sizeof(pending), "%s/config.pending", pc_root);
  snprintf(unrelated, sizeof(unrelated), "%s/unrelated", pc_root);
  snprintf(outside, sizeof(outside), "%s/retained", root);
  const uint8_t owner[32] = {11}, key[32] = {44}, id[16] = {33};
  const uint8_t tx[16] = {1}, reset_tx[16] = {2};
  struct bkpc_grants_s grant = {0}, reopened = {0};
  if (!strcmp(argv[1], "recover"))
    {
      assert(bkprov_storage_start(root) == 0);
      assert(wait_receipt(reset_tx) == -EOWNERDEAD);
      assert(bkprov_storage_reset_pending() == 1);
      assert(finish() == 0);
      assert(bkprov_storage_reset_receipt(reset_tx) == BKPROV_STORAGE_RESET_RECEIPT_COMPLETED);
      assert(bkpc_grants_open(&reopened, pc_root, owner) == 0);
      uint8_t out[32]; uint32_t caps;
      assert(bkpc_grants_key(&reopened, 0, out, &caps) == -EACCES);
      assert(access(unrelated, F_OK) == 0 && access(outside, F_OK) == 0);
      assert(bkprov_storage_stop() == 0);
      puts("CONTRACT_PASS"); return 0;
    }
  bool absent = !strcmp(argv[1], "absent");
  bool symlinked = !strcmp(argv[1], "symlink");
  bool no_reset = !strcmp(argv[1], "no-marker");
  int failure = !strcmp(argv[1], "unlink") ? 1 : !strcmp(argv[1], "sync") ? 2 : 0;
  assert(absent || symlinked || no_reset || failure || !strcmp(argv[1], "clear"));
  file(outside);
  if (!absent)
    {
      assert(mkdir(pc_root, 0700) == 0);
      assert(bkpc_grants_open(&grant, pc_root, owner) == 0);
      assert(bkpc_grants_set(&grant, 0, tx, id, key, 3) == 0);
      file(pending); file(unrelated);
      struct stat st; assert(stat(pc_root, &st) == 0); pc_inode = st.st_ino;
      if (symlinked)
        {
          char saved[280]; snprintf(saved, sizeof(saved), "%s/retained-pc", root);
          assert(rename(pc_root, saved) == 0 && symlink(saved, pc_root) == 0);
        }
    }
  assert(bkprov_storage_start(root) == 0 && wait_receipt(tx) == 0);
  assert(bkprov_storage_commit(0, tx, "test", 4) == -EAGAIN);
  assert(wait_receipt(tx) == 1);
  if (no_reset)
    {
      assert(finish() == -EPERM);
      assert(bkpc_grants_open(&reopened, pc_root, owner) == 0);
      uint8_t out[32]; uint32_t caps;
      assert(bkpc_grants_key(&reopened, 1, out, &caps) == 0);
      assert(caps == 3 && !memcmp(out, key, 32));
    }
  else
    {
      int ret = -EAGAIN;
      for (unsigned int i = 0; i < 3000 && ret == -EAGAIN; i++)
        { ret = bkprov_storage_reset_request(1, reset_tx); if (ret == -EAGAIN) tick(); }
      assert(ret == 0);
      atomic_store(&fault, failure);
      ret = finish();
      if (failure == 2)
        {
          assert(ret == -EINPROGRESS);
          assert(bkprov_storage_reset_pending() == -EINPROGRESS);
          assert(bkprov_storage_reset_receipt(reset_tx) == -EINPROGRESS);
          assert(bkprov_storage_refresh() == -EINPROGRESS);
          assert(bkprov_storage_stop() == -EINPROGRESS);
          pid_t child = fork(); assert(child >= 0);
          if (!child)
            { execl(argv[0], argv[0], "recover", root, (char *)NULL); _exit(127); }
          int status; assert(waitpid(child, &status, 0) == child);
          assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
          puts("CONTRACT_PASS"); return 0;
        }
      if (failure || symlinked)
        {
          assert(ret < 0 && ret != -EAGAIN);
          assert(bkprov_storage_reset_pending() == 1);
          assert(bkprov_storage_reset_receipt(reset_tx) == BKPROV_STORAGE_RESET_RECEIPT_PENDING);
          atomic_store(&fault, 0);
          assert(bkprov_storage_stop() == 0);
          assert(bkprov_storage_start(root) == 0);
          assert(wait_receipt(reset_tx) == -EOWNERDEAD);
          if (symlinked)
            {
              assert(access(active, F_OK) == 0 && access(pending, F_OK) == 0);
              assert(finish() < 0);
              assert(bkprov_storage_stop() == 0);
              puts("CONTRACT_PASS"); return 0;
            }
          ret = finish();
        }
      assert(ret == 0);
      assert(bkprov_storage_reset_receipt(reset_tx) == BKPROV_STORAGE_RESET_RECEIPT_COMPLETED);
      if (!absent)
        {
          assert(access(active, F_OK) < 0 && errno == ENOENT);
          assert(access(pending, F_OK) < 0 && errno == ENOENT);
          assert(access(unrelated, F_OK) == 0);
          assert(bkpc_grants_open(&reopened, pc_root, owner) == 0);
          uint8_t out[32]; uint32_t caps;
          assert(bkpc_grants_key(&reopened, 0, out, &caps) == -EACCES);
        }
      else assert(access(pc_root, F_OK) < 0 && errno == ENOENT);
    }
  assert(access(outside, F_OK) == 0);
  assert(bkprov_storage_stop() == 0);
  puts("CONTRACT_PASS"); return 0;
}
