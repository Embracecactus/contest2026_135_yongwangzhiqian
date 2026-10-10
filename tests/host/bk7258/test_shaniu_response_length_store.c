/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_preferences.h"
#include "bk7258_provision_store.h"
#include <assert.h>
#include <errno.h>
#include <nuttx/mutex.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char root[] = "/tmp/shaniu-response-length-XXXXXX";
static int fail_directory_sync;
static unsigned int mkdir_calls;
int nxmutex_lock(mutex_t *m) { return -pthread_mutex_lock(m); }
int nxmutex_unlock(mutex_t *m) { return -pthread_mutex_unlock(m); }
int __real_mkdir(const char *, mode_t);
int __wrap_mkdir(const char *p, mode_t m)
{ mkdir_calls++; return __real_mkdir(!strcmp(p, "/cpdata/shaniu/response-length") ? root : p, m); }
int __real_bkprov_store_open(struct bkprov_store_s *, const char *);
int __wrap_bkprov_store_open(struct bkprov_store_s *s, const char *p)
{ return __real_bkprov_store_open(s, !strcmp(p, "/cpdata/shaniu/response-length") ? root : p); }
int __real_lstat(const char *, struct stat *);
int __wrap_lstat(const char *p, struct stat *s)
{ return __real_lstat(!strcmp(p, "/cpdata/shaniu/response-length") ? root : p, s); }
int __real_fsync(int);
int __wrap_fsync(int fd)
{
  struct stat s; assert(fstat(fd, &s) == 0);
  if (fail_directory_sync && S_ISDIR(s.st_mode)) { errno = EIO; return -1; }
  return __real_fsync(fd);
}

static pthread_barrier_t writers_ready;
struct writer_s { unsigned int mode; int result; };
static void *write_same_revision(void *context)
{
  struct writer_s *writer = context;
  uint8_t transaction[16] = {3};
  transaction[1] = writer->mode + 1;
  (void)pthread_barrier_wait(&writers_ready);
  writer->result = bk7258_preferences_response_length_set(writer->mode,
                                                         2, transaction);
  return NULL;
}

int main(int argc, char **argv)
{
  struct bk7258_response_length_s value;
  uint8_t one[16] = {1}, two[16] = {2};
  if (argc == 3 && !strcmp(argv[1], "--reopen"))
    {
      assert(strlen(argv[2]) < sizeof(root)); strcpy(root, argv[2]);
      assert(bk7258_preferences_response_length_get(&value) == 0);
      assert(value.mode == BK7258_RESPONSE_LENGTH_DETAILED && value.revision == 2);
      puts("FRESH_PROCESS_VALID_RECORD"); return 0;
    }
  assert(argc == 1);
  assert(mkdtemp(root));
  assert(rmdir(root) == 0); /* Upgrade: even the new preference directory is absent. */
  assert(bk7258_preferences_response_length_get(&value) == 0);
  assert(value.mode == BK7258_RESPONSE_LENGTH_STANDARD && value.revision == 0);
  assert(mkdir_calls == 0); /* A default read must not materialize a record. */
  assert(bk7258_preferences_response_length_set(BK7258_RESPONSE_LENGTH_CONCISE,
                                                 0, one) == 0);
  assert(bk7258_preferences_response_length_get(&value) == 0);
  assert(value.mode == BK7258_RESPONSE_LENGTH_CONCISE && value.revision == 1);
  assert(bk7258_preferences_response_length_set(BK7258_RESPONSE_LENGTH_CONCISE,
                                                 0, one) == 0);
  assert(bk7258_preferences_response_length_set(BK7258_RESPONSE_LENGTH_DETAILED,
                                                 0, two) == -ESTALE);
  assert(bk7258_preferences_response_length_set(BK7258_RESPONSE_LENGTH_DETAILED,
                                                 1, two) == 0);
  assert(bk7258_preferences_response_length_get(&value) == 0);
  assert(value.mode == BK7258_RESPONSE_LENGTH_DETAILED && value.revision == 2);
  pid_t child = fork(); assert(child >= 0);
  if (!child) { execl(argv[0], argv[0], "--reopen", root, (char *)NULL); _exit(127); }
  int child_status; assert(waitpid(child, &child_status, 0) == child);
  assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
  assert(bk7258_preferences_response_length_set(BK7258_RESPONSE_LENGTH_STANDARD,
                                                 2, (uint8_t[16]){0}) == -EINVAL);
  assert(bk7258_preferences_response_length_set((enum bk7258_response_length_e)-1,
                                                 2, one) == -EINVAL);
  assert(bk7258_preferences_response_length_set(BK7258_RESPONSE_LENGTH_STANDARD,
                                                 UINT64_MAX, one) == -EINVAL);
  struct writer_s writers[2] = {{0, -999}, {1, -999}};
  pthread_t threads[2];
  assert(pthread_barrier_init(&writers_ready, NULL, 2) == 0);
  for (unsigned int i = 0; i < 2; i++)
    assert(pthread_create(&threads[i], NULL, write_same_revision, &writers[i]) == 0);
  for (unsigned int i = 0; i < 2; i++) assert(pthread_join(threads[i], NULL) == 0);
  assert((writers[0].result == 0 && writers[1].result == -ESTALE) ||
         (writers[1].result == 0 && writers[0].result == -ESTALE));
  assert(pthread_barrier_destroy(&writers_ready) == 0);
  assert(bk7258_preferences_response_length_get(&value) == 0);
  assert(value.revision == 3 && value.mode == (writers[0].result ? 1 : 0));
  fail_directory_sync = 1;
  assert(bk7258_preferences_response_length_set(BK7258_RESPONSE_LENGTH_STANDARD,
                                                 3, one) == -EINPROGRESS);
  fail_directory_sync = 0;
  assert(bk7258_preferences_response_length_get(&value) == -EINPROGRESS);
  puts("CONTRACT_PASS response-length CAS/default/uncertain");
  return 0;
}
