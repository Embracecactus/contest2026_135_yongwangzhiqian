/****************************************************************************
 * apps/bk7258/test_shaniu_runtime_skills_store.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_runtime_skills_store.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_BASE "/tmp/bk7258-runtime-skills-store"
#define TEST_ROOT TEST_BASE "/runtime-skills"

static void remove_store(void)
{
  (void)unlink(TEST_ROOT "/config.bin");
  (void)unlink(TEST_ROOT "/config.pending");
  (void)rmdir(TEST_ROOT "/config.pending");
  (void)rmdir(TEST_ROOT);
  (void)rmdir(TEST_BASE);
}

static int g_force_unknown;
static int g_sync_unknown;

int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  struct stat status;
  if (g_sync_unknown && fstat(fd, &status) == 0 && S_ISDIR(status.st_mode))
    {
      errno = EIO;
      return -1;
    }
  return __real_fsync(fd);
}

int __real_rename(const char *oldpath, const char *newpath);

int __wrap_rename(const char *oldpath, const char *newpath)
{
  if (g_force_unknown)
    {
      errno = EIO;
      return -1;
    }

  return __real_rename(oldpath, newpath);
}

int main(void)
{
  static const char first[] =
    "{\"publickey\":\"key\",\"enableflags\":1}";
  static const char second[] =
    "{\"manifest\":\"next\",\"signature\":\"sig\"}";
  char buffer[BKRUNTIME_SKILLS_STORE_MAX];
  uint64_t revision;
  size_t actual;

  remove_store();
  assert(mkdir(TEST_BASE, 0700) == 0);
  assert(bkruntime_skills_store_load(buffer, sizeof(buffer), &actual,
                                     &revision) == 0);
  assert(actual == 0 && revision == 0);
  assert(bkruntime_skills_store_commit(0, first, sizeof(first) - 1,
                                       &revision) == 0);
  assert(revision == 1);
  assert(bkruntime_skills_store_load(buffer, sizeof(buffer), &actual,
                                     &revision) == 0);
  assert(revision == 1 && actual == sizeof(first) - 1);
  assert(!memcmp(buffer, first, actual));
  assert(bkruntime_skills_store_commit(0, second, sizeof(second) - 1,
                                       &revision) == -ESTALE);
  assert(mkdir(TEST_ROOT "/config.pending", 0700) == 0);
  assert(bkruntime_skills_store_commit(1, second, sizeof(second) - 1,
                                       &revision) == -EISDIR);
  assert(rmdir(TEST_ROOT "/config.pending") == 0);
  g_force_unknown = 1;
  assert(bkruntime_skills_store_commit(1, second, sizeof(second) - 1,
                                       &revision) == -EINPROGRESS);
  g_force_unknown = 0;
  assert(bkruntime_skills_store_load(buffer, sizeof(buffer), &actual,
                                     &revision) == 0);
  assert(revision == 1 && !memcmp(buffer, first, actual));
  assert(bkruntime_skills_store_commit(1, second, sizeof(second) - 1,
                                       &revision) == 0);
  assert(revision == 2);
  g_sync_unknown = 1;
  assert(bkruntime_skills_store_commit(2, first, sizeof(first) - 1,
                                       &revision) == -EINPROGRESS);
  g_sync_unknown = 0;
  assert(bkruntime_skills_store_load(buffer, sizeof(buffer), &actual,
                                     &revision) == 0);
  assert(revision == 3 && actual == sizeof(first) - 1 &&
         !memcmp(buffer, first, actual));
  assert(bkruntime_skills_store_commit(2, second,
                                       BKRUNTIME_SKILLS_STORE_MAX + 1,
                                       &revision) == -EINVAL);
  remove_store();
  puts("RUNTIME_SKILLS_STORE_PASS");
  return 0;
}
