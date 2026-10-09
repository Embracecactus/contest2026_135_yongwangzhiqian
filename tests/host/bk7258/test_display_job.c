/* SPDX-License-Identifier: Apache-2.0 */
/* Real job, store and pack code; only mount ownership and a blocked write
 * syscall are external peers. No substitute installation state machine. */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bk7258_display_job.h"

static bool leased;
static int opens, closes, release_error;
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_changed = PTHREAD_COND_INITIALIZER;
static bool block_write, block_sync, entered, proceed;
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  pthread_mutex_lock(&io_lock);
  if (block_sync)
    {
      entered = true; pthread_cond_broadcast(&io_changed);
      while (!proceed) pthread_cond_wait(&io_changed, &io_lock);
    }
  pthread_mutex_unlock(&io_lock);
  return __real_fsync(fd);
}
static bool unlink_failure;
int __real_unlink(const char *path);
int __wrap_unlink(const char *path)
{ if (unlink_failure) { unlink_failure=false; errno=EIO; return -1; } return __real_unlink(path); }
static int acquire(void *context)
{ (void)context; assert(!leased); leased = true; opens++; return 0; }
static int release(void *context)
{ (void)context; assert(leased); closes++; if (release_error) return release_error; leased = false; return 0; }
static const struct bkdisplay_job_ops_s ops = {acquire, release};
ssize_t __real_write(int fd, const void *data, size_t size);
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
  pthread_mutex_lock(&io_lock);
  if (block_write)
    {
      entered = true;
      pthread_cond_broadcast(&io_changed);
      while (!proceed) pthread_cond_wait(&io_changed, &io_lock);
    }
  pthread_mutex_unlock(&io_lock);
  return __real_write(fd, data, size);
}
static int remove_entry(const char *path, const struct stat *st, int type, struct FTW *walk)
{ (void)st; (void)type; (void)walk; return remove(path); }
static void *consume(void *context)
{ assert(bkdisplay_job_step(context, 101) == 1); return NULL; }

int main(int argc, char **argv)
{
  char root[] = "/tmp/bkdisplay-job-XXXXXX";
  struct bkdisplay_job_s job = {.lock = PTHREAD_MUTEX_INITIALIZER, .ops = &ops, .root = root};
  struct bkdisplay_job_status_s status;
  struct stat info;
  uint64_t id;
  unsigned char *bytes;
  int fd;
  size_t size, offset;
  assert(argc == 3 && mkdtemp(root));
  fd = open(argv[1], O_RDONLY);
  assert(fd >= 0 && fstat(fd, &info) == 0);
  size = info.st_size;
  bytes = malloc(size);
  assert(bytes && read(fd, bytes, size) == (ssize_t)size && close(fd) == 0);
  assert(bkdisplay_job_begin(&job, 7, size, 100, 1000, &id) == -EACCES);
  assert(bkdisplay_job_gate(&job, true) == 0);
  assert(bkdisplay_job_begin(&job, 7, size, 100, 1000, &id) == 0);
  assert(opens == 0 && closes == 0); /* Submission cannot mount/write. */
  assert(bkdisplay_job_status(&job, &status) == 0);
  assert(status.id == id && status.state == BKDISPLAY_JOB_QUEUED);
  assert(bkdisplay_job_cancel(&job, 8, id) == -ESTALE);
  assert(bkdisplay_job_cancel(&job, 7, id + 1) == -ESTALE);

  if (!strcmp(argv[2], "queued-cancel"))
    {
      assert(bkdisplay_job_cancel(&job, 7, id) == 0);
      assert(bkdisplay_job_step(&job, 101) == 1);
      assert(opens == 0 && closes == 0);
    }
  else if (!strcmp(argv[2], "expiry") || !strcmp(argv[2], "rollback"))
    {
      assert(bkdisplay_job_step(&job, !strcmp(argv[2], "expiry") ? 1000 : 99) == 1);
      assert(opens == 0);
    }
  else
    {
      assert(bkdisplay_job_step(&job, 100) == 1);
      assert(leased && opens == 1);
      if (!strcmp(argv[2], "blocked-cancel"))
        {
          pthread_t worker;
          block_write = true;
          assert(bkdisplay_job_append(&job, 7, id, 0, bytes, 3) == 0);
          assert(bkdisplay_job_append(&job, 7, id, 0, bytes, 3) == -EBUSY);
          assert(pthread_create(&worker, NULL, consume, &job) == 0);
          pthread_mutex_lock(&io_lock);
          while (!entered) pthread_cond_wait(&io_changed, &io_lock);
          pthread_mutex_unlock(&io_lock);
          assert(bkdisplay_job_status(&job, &status) == 0);
          assert(status.state == BKDISPLAY_JOB_WRITING && status.written == 0);
          assert(bkdisplay_job_cancel(&job, 7, id) == 0);
          assert(bkdisplay_job_quiesced(&job) == -EAGAIN && leased);
          pthread_mutex_lock(&io_lock);
          proceed = true;
          pthread_cond_broadcast(&io_changed);
          pthread_mutex_unlock(&io_lock);
          assert(pthread_join(worker, NULL) == 0);
          assert(bkdisplay_job_step(&job, 102) == 1);
        }
      else if (!strcmp(argv[2], "gate") || !strcmp(argv[2], "release-failure") || !strcmp(argv[2], "cleanup-failure"))
        {
          if (!strcmp(argv[2], "release-failure")) release_error = -EIO;
          if (!strcmp(argv[2], "cleanup-failure")) unlink_failure = true;
          assert(bkdisplay_job_gate(&job, false) == 0);
          assert(bkdisplay_job_quiesced(&job) == -EAGAIN);
          assert(bkdisplay_job_step(&job, 101) == 1);
        }
      else
        {
          for (offset = 0; offset < size; )
            {
              size_t count = size - offset;
              if (count > BKDISPLAY_UPLOAD_CHUNK_MAX) count = BKDISPLAY_UPLOAD_CHUNK_MAX;
              assert(bkdisplay_job_append(&job, 7, id, offset, bytes + offset, count) == 0);
              assert(bkdisplay_job_finish(&job, 7, id) == -EBUSY);
              assert(bkdisplay_job_step(&job, 101) == 1);
              offset += count;
            }
          assert(bkdisplay_job_finish(&job, 7, id) == 0);
          if (!strcmp(argv[2], "commit-cancel"))
            assert(bkdisplay_job_cancel(&job, 7, id) == 0);
          if (!strcmp(argv[2], "commit-busy"))
            {
              pthread_t worker;
              block_sync = true;
              assert(pthread_create(&worker, NULL, consume, &job) == 0);
              pthread_mutex_lock(&io_lock);
              while (!entered) pthread_cond_wait(&io_changed, &io_lock);
              pthread_mutex_unlock(&io_lock);
              assert(bkdisplay_job_status(&job, &status) == 0);
              assert(status.state == BKDISPLAY_JOB_COMMITTING);
              assert(bkdisplay_job_cancel(&job, 7, id) == -EBUSY);
              assert(bkdisplay_job_gate(&job, false) == 0);
              assert(bkdisplay_job_quiesced(&job) == -EAGAIN);
              pthread_mutex_lock(&io_lock);
              proceed = true; pthread_cond_broadcast(&io_changed);
              pthread_mutex_unlock(&io_lock);
              assert(pthread_join(worker, NULL) == 0);
            }
          else assert(bkdisplay_job_step(&job, 102) == 1);
        }
    }
  assert(bkdisplay_job_status(&job, &status) == 0);
  if (!strcmp(argv[2], "release-failure"))
    {
      assert(status.state == BKDISPLAY_JOB_UNKNOWN && leased);
      assert(bkdisplay_job_quiesced(&job) == -EIO);
      assert(bkdisplay_job_gate(&job, true) == 0);
      assert(bkdisplay_job_begin(&job, 7, size, 103, 1000, &id) == -EBUSY);
      assert(bkdisplay_job_step(&job, 104) == 0 && closes == 1);
      leased = false; /* External mount peer only; not product recovery. */
    }
  else
    {
      assert(!leased && bkdisplay_job_quiesced(&job) == 0);
      assert(status.state == ((!strcmp(argv[2], "success") || !strcmp(argv[2], "commit-busy")) ? BKDISPLAY_JOB_DONE :
                              !strcmp(argv[2], "cleanup-failure") ? BKDISPLAY_JOB_UNKNOWN : BKDISPLAY_JOB_CANCELED));
      if (!strcmp(argv[2], "cleanup-failure")) assert(status.error == -EIO);
      assert(bkdisplay_job_append(&job, 7, id, 0, bytes, 1) == -EALREADY);
      if (status.state == BKDISPLAY_JOB_DONE)
        {
          char active[512];
          snprintf(active, sizeof(active), "%s/shaniu/display/active.json", root);
          assert(access(active, F_OK) < 0 && errno == ENOENT);
          assert(status.written == size && status.filename[0]);
        }
      assert(bkdisplay_job_gate(&job, true) == 0);
      uint64_t previous = id;
      assert(bkdisplay_job_begin(&job, 7, size, 103, 1000, &id) == 0 && id > previous);
      assert(bkdisplay_job_cancel(&job, 7, previous) == -ESTALE);
      assert(bkdisplay_job_cancel(&job, 7, id) == 0 && bkdisplay_job_step(&job, 104) == 1);
    }
  free(bytes);
  assert(nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
