/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_pc_grants.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fail_sync;
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  struct stat st;
  assert(fstat(fd, &st) == 0);
  if ((fail_sync == 1 && S_ISREG(st.st_mode)) ||
      (fail_sync == 2 && S_ISDIR(st.st_mode)))
    { errno = EIO; return -1; }
  return __real_fsync(fd);
}
static void denied(struct bkpc_grants_s *state, uint64_t revision, int error)
{
  uint8_t key[32]; memset(key, 77, sizeof(key));
  uint32_t caps = 99;
  assert(bkpc_grants_key(state, revision, key, &caps) == error);
  assert(caps == 0);
  for (size_t i = 0; i < sizeof(key); i++) assert(key[i] == 0);
}
int main(int argc, char **argv)
{
  struct bkpc_grants_s state = {0}, reopened = {0};
  uint8_t owner[32] = {11}, other[32] = {22};
  uint8_t client[16] = {33}, key[32] = {44}, next[32] = {55};
  uint8_t tx[16] = {1}, revoke[16] = {2};
  uint8_t actual[32], id[16]; uint32_t caps; uint64_t revision;
  if (argc == 3 && !strcmp(argv[1], "reopen-revoked"))
    {
      assert(bkpc_grants_open(&state, argv[2], owner) == 0);
      assert(bkpc_grants_snapshot(&state, &revision, id, &caps) == 0);
      assert(revision == 2 && caps == 0);
      denied(&state, revision, -EACCES);
      puts("CONTRACT_PASS"); return 0;
    }
  assert(argc == 2);
  char root[] = "/tmp/shaniu-pc-grants-XXXXXX";
  assert(mkdtemp(root));
  assert(bkpc_grants_open(&state, root, owner) == 0);
  denied(&state, 0, -EACCES);
  if (!strcmp(argv[1], "camera"))
    {
      assert(bkpc_grants_set(&state, 0, tx, client, key, BKPC_CAP_CAMERA) == 0);
      assert(bkpc_grants_open(&reopened, root, owner) == 0);
      assert(bkpc_grants_key(&reopened, 1, actual, &caps) == 0);
      assert(caps == 16 && !(caps & (BKPC_CAP_RESOURCES | BKPC_CAP_DIAGNOSTICS)));
      assert(bkpc_grants_set(&state, 1, revoke, NULL, NULL, 0) == 0);
      denied(&state, 2, -EACCES);
    }
  else if (!strcmp(argv[1], "invalid"))
    {
      uint8_t zero[32] = {0};
      assert(bkpc_grants_set(&state, 0, zero, client, key, 1) == -EINVAL);
      assert(bkpc_grants_set(&state, 0, tx, zero, key, 1) == -EINVAL);
      assert(bkpc_grants_set(&state, 0, tx, client, zero, 1) == -EINVAL);
      assert(bkpc_grants_set(&state, 0, tx, client, key, 32) == -EINVAL);
      assert(bkpc_grants_set(&state, 0, tx, client, owner, 1) == -EACCES);
      assert(bkpc_grants_set(&state, 0, tx, client, key, 0) == -EINVAL);
      assert(access(state.store.active, F_OK) < 0 && errno == ENOENT);
    }
  else
    {
      assert(bkpc_grants_set(&state, 0, tx, client, key, 3) == 0);
      assert(bkpc_grants_key(&state, 1, actual, &caps) == 0);
      assert(caps == 3 && !memcmp(actual, key, sizeof(key)));
      if (!strcmp(argv[1], "persist"))
        {
          assert(bkpc_grants_open(&reopened, root, owner) == 0);
          assert(bkpc_grants_key(&reopened, 1, actual, &caps) == 0);
          assert(!memcmp(actual, key, 32));
          assert(bkpc_grants_snapshot(&reopened, &revision, id, &caps) == 0);
          assert(revision == 1 && caps == 3 && !memcmp(id, client, 16));
          assert(bkpc_grants_set(&state, 1, revoke, NULL, NULL, 0) == 0);
          denied(&state, 2, -EACCES);
          struct bkpc_grants_s revoked = {0};
          assert(bkpc_grants_open(&revoked, root, owner) == 0);
          denied(&revoked, 2, -EACCES);
        }
      else if (!strcmp(argv[1], "owner"))
        {
          assert(bkpc_grants_open(&reopened, root, other) == 0);
          denied(&reopened, 1, -EACCES);
          assert(bkpc_grants_snapshot(&reopened, &revision, id, &caps) == 0);
          assert(revision == 1 && caps == 0);
          for (size_t i = 0; i < 16; i++) assert(id[i] == 0);
          assert(bkpc_grants_set(&reopened, 1, revoke, client, next, 1) == 0);
          assert(bkpc_grants_key(&reopened, 2, actual, &caps) == 0);
          assert(!memcmp(actual, next, 32) && caps == 1);
        }
      else if (!strcmp(argv[1], "revision"))
        {
          assert(bkpc_grants_set(&state, 0, tx, client, key, 3) == 0);
          assert(bkpc_grants_set(&state, 0, tx, client, next, 3) == -EEXIST);
          assert(bkpc_grants_set(&state, 0, revoke, client, next, 3) == -ESTALE);
          denied(&state, 0, -ESTALE);
          assert(bkpc_grants_set(&state, 1, revoke, NULL, NULL, 0) == 0);
          assert(bkpc_grants_set(&state, 0, tx, client, key, 3) == -ESTALE);
          denied(&state, 2, -EACCES);
        }
      else if (!strcmp(argv[1], "writefail"))
        {
          fail_sync = 1;
          assert(bkpc_grants_set(&state, 1, revoke, NULL, NULL, 0) == -EIO);
          fail_sync = 0;
          assert(bkpc_grants_open(&reopened, root, owner) == 0);
          assert(bkpc_grants_key(&reopened, 1, actual, &caps) == 0);
          assert(!memcmp(actual, key, 32)); /* Revocation did NOT succeed. */
        }
      else if (!strcmp(argv[1], "uncertain"))
        {
          fail_sync = 2;
          assert(bkpc_grants_set(&state, 1, revoke, NULL, NULL, 0) == -EINPROGRESS);
          fail_sync = 0;
          denied(&state, 1, -EINPROGRESS);
          assert(bkpc_grants_open(&state, root, owner) == -EINPROGRESS);
          assert(bkpc_grants_set(&state, 1, revoke, NULL, NULL, 0) == -EINPROGRESS);
          pid_t child = fork(); assert(child >= 0);
          if (!child) { execl(argv[0], argv[0], "reopen-revoked", root, (char *)NULL); _exit(127); }
          int status; assert(waitpid(child, &status, 0) == child);
          assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
      else if (!strcmp(argv[1], "golden") || !strcmp(argv[1], "aliased-key"))
        {
          /* Independent hashlib vector from the frozen PCG1 format, not the
           * production encoder or a production-generated expected record.
           */
          const uint8_t owner_binding[32] = {0xc6, 0x44, 0x41, 0x87, 0xdf, 0x7e, 0x3d, 0x27, 0xe5, 0x08, 0xdd, 0xe7, 0x54, 0x1d, 0x07, 0x4f, 0xc0, 0x1e, 0x1d, 0x30, 0xf2, 0xa8, 0x49, 0x5a, 0x13, 0xe8, 0x90, 0x9e, 0x12, 0x48, 0x48, 0xfe};
          uint8_t record[88] = {'P','C','G','1'};
          record[7] = 1;
          memcpy(record + 8, owner_binding, 32);
          record[40] = 33;
          record[56] = !strcmp(argv[1], "golden") ? 55 : 11;
          assert(bkprov_store_commit(&state.store, 1, revoke, record, sizeof(record)) == 0);
          int result = bkpc_grants_open(&reopened, root, owner);
          if (!strcmp(argv[1], "golden"))
            {
              assert(result == 0);
              assert(bkpc_grants_key(&reopened, 2, actual, &caps) == 0);
              assert(caps == 1 && !memcmp(actual, next, 32));
            }
          else
            {
              assert(result == -EPROTO);
              denied(&reopened, 2, -ENODEV);
            }
        }
      else if (!strcmp(argv[1], "corrupt"))
        {
          uint8_t record[88] = {'P','C','G','1'};
          record[7] = 32; /* Independent invalid capability golden vector. */
          record[8] = 1; record[40] = 1; record[56] = 1;
          assert(bkprov_store_commit(&state.store, 1, revoke, record, sizeof(record)) == 0);
          assert(bkpc_grants_open(&reopened, root, owner) == -EPROTO);
          denied(&reopened, 2, -ENODEV);
        }
      else assert(!"unknown case");
    }
  if (access(state.store.pending, F_OK) == 0) assert(unlink(state.store.pending) == 0);
  if (access(state.store.active, F_OK) == 0) assert(unlink(state.store.active) == 0);
  assert(rmdir(root) == 0);
  puts("CONTRACT_PASS"); return 0;
}
