/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_preferences.h"
#include "bk7258_provision_store.h"
#include <nuttx/mutex.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
static char root[] = "/tmp/shaniu-models-XXXXXX";
static int fail_directory_sync, failed_syncs, filesystem_error;
int nxmutex_lock(mutex_t *m) { return -pthread_mutex_lock(m); }
int nxmutex_unlock(mutex_t *m) { return -pthread_mutex_unlock(m); }
static const char *redirect(const char *path)
{ return !strcmp(path, "/cpdata/shaniu/cloud-models") ? root : path; }
int __real_mkdir(const char *, mode_t);
int __wrap_mkdir(const char *path, mode_t mode) { return __real_mkdir(redirect(path), mode); }
int __real_bkprov_store_open(struct bkprov_store_s *, const char *);
int __wrap_bkprov_store_open(struct bkprov_store_s *store, const char *path)
{ return __real_bkprov_store_open(store, redirect(path)); }
int __real_lstat(const char *, struct stat *);
int __wrap_lstat(const char *path, struct stat *info) { return __real_lstat(redirect(path), info); }
int __real_bkprov_store_check_filesystem(const char *);
int __wrap_bkprov_store_check_filesystem(const char *path)
{ return !strcmp(path, "/cpdata/shaniu") ? filesystem_error : __real_bkprov_store_check_filesystem(path); }
int __real_fsync(int);
int __wrap_fsync(int fd)
{
  struct stat st;
  assert(fstat(fd, &st) == 0);
  if (fail_directory_sync && S_ISDIR(st.st_mode)) {
    failed_syncs++; errno = EIO; return -1;
  }
  return __real_fsync(fd);
}
int main(int argc, char **argv)
{
  if (argc == 3 && !strcmp(argv[1], "--reopen")) {
    assert(strlen(argv[2]) < sizeof(root)); strcpy(root, argv[2]);
    struct bkcloud_models_s loaded;
    assert(bk7258_preferences_cloud_models_get(&loaded) == 0);
    assert(!strcmp(loaded.chat_model, "fixture-b"));
    puts("FRESH_PROCESS_VALID_RECORD");
    return 0;
  }
  assert(argc == 1 && mkdtemp(root));
  struct bkcloud_models_s a = {.asr_model="fixture-asr", .chat_model="fixture-a", .tts_model="fixture-tts"};
  struct bkcloud_models_s b = a, observed;
  strcpy(b.chat_model, "fixture-b");
  assert(bk7258_preferences_cloud_models_set(&a) == 0);
  assert(bk7258_preferences_cloud_models_get(&observed) == 0);
  assert(!strcmp(observed.chat_model, "fixture-a"));
  fail_directory_sync = 1;
  int ret = bk7258_preferences_cloud_models_set(&b);
  assert(failed_syncs == 1);
  fprintf(stderr, "directory sync failed; setter result=%d (expected %d)\n", ret, -EINPROGRESS);
  assert(ret == -EINPROGRESS);
  /* Readable new bytes are intentionally observable through the lower store,
   * but cannot stand in for the failed durability barrier. */
  struct bkprov_store_s store;
  unsigned char record[BKCLOUD_MODELS_RECORD_MAX]; size_t size; uint64_t revision;
  assert(bkprov_store_open(&store, root) == 0);
  assert(bkprov_store_load(&store, record, sizeof(record), &size, &revision, NULL) == 0);
  assert(revision == 2 && bkcloud_models_decode(&observed, record, size) == 0);
  assert(!strcmp(observed.chat_model, "fixture-b"));
  fail_directory_sync = 0;
  assert(bk7258_preferences_cloud_models_get(&observed) == -EINPROGRESS);
  assert(bk7258_preferences_cloud_models_set(&a) == -EINPROGRESS);
  assert(bkprov_store_load(&store, record, sizeof(record), &size, &revision, NULL) == 0 && revision == 2);
  assert(observed.chat_model[0] == 0); /* Failed public read clears its output. */
  pid_t child = fork(); assert(child >= 0);
  if (!child) { execl(argv[0], argv[0], "--reopen", root, (char *)NULL); _exit(127); }
  int child_status; assert(waitpid(child, &child_status, 0) == child);
  assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
  assert(bk7258_preferences_cloud_models_get(&observed) == -EINPROGRESS);
  assert(bk7258_preferences_cloud_models_reset_complete() == -EBUSY);
  assert(unlink(store.active) == 0 && rmdir(root) == 0);
  filesystem_error = -ENODEV;
  assert(bk7258_preferences_cloud_models_reset_complete() == -ENODEV);
  assert(bk7258_preferences_cloud_models_get(&observed) == -EINPROGRESS);
  filesystem_error = 0;
  assert(bk7258_preferences_cloud_models_reset_complete() == 0);
  assert(bk7258_preferences_cloud_models_get(&observed) == -ENOENT);
  assert(bk7258_preferences_cloud_models_set(&a) == 0);
  assert(bk7258_preferences_cloud_models_get(&observed) == 0 && !strcmp(observed.chat_model, "fixture-a"));
  assert(unlink(store.active) == 0 && rmdir(root) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
