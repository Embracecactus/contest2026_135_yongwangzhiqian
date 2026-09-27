/* SPDX-License-Identifier: Apache-2.0 */
/* Native service/worker plus actual job/store/volume. Only NuttX scheduling,
 * mount syscalls and physical root are mapped to host external peers. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "bk7258_display_job_service.h"
#include "bk7258_media_volume.h"

static char fixture_root[] = "/tmp/bkdisplay-native-job-XXXXXX";
static pthread_t worker;
static int (*entry)(int, char **);
static int created, mounted, unmounted;
static bool create_failure, mount_failure, unmount_failure;
static void *thread_entry(void *unused)
{ (void)unused; entry(0, NULL); return NULL; }
int task_create(const char *name, int priority, int stack,
                int (*mainfn)(int, char **), char *const argv[])
{
  (void)argv;
  assert(!strcmp(name, "bkpack") && priority == 75 && stack == 6144);
  if (create_failure) { errno = EAGAIN; return -1; }
  entry = mainfn;
  assert(pthread_create(&worker, NULL, thread_entry, NULL) == 0);
  created++;
  return created;
}
int nxsig_usleep(uint32_t usec) { return usleep(usec); }
int nxsem_clockwait_uninterruptible(sem_t *sem, clockid_t clock,
                                    const struct timespec *deadline)
{
  int ret;
  do { ret = sem_clockwait(sem, clock, deadline); } while (ret < 0 && errno == EINTR);
  return ret == 0 ? 0 : -errno;
}
static int mount_peer(const char *source, const char *target, const char *type,
                      unsigned long flags, const void *data)
{
  assert(!strcmp(source, "/dev/test-pack") && !strcmp(target, fixture_root));
  assert(!strcmp(type, "vfat") && !flags && !data);
  if (mount_failure) { errno = EIO; return -1; }
  mounted++;
  return 0;
}
static int unmount_peer(const char *target)
{
  assert(!strcmp(target, fixture_root));
  if (unmount_failure) { errno = EIO; return -1; }
  unmounted++;
  return 0;
}
static int mkdir_peer(const char *path, mode_t mode)
{ return !strcmp(path, "/mnt") ? 0 : mkdir(path, mode); }
#define CONFIG_BK7258_DISPLAY_BLOCKDEV "/dev/test-pack"
#define CONFIG_BK7258_DISPLAY_SERVICE_PRIORITY 75
#define CONFIG_BK7258_DISPLAY_SERVICE_STACKSIZE 6144
#define BKDISPLAY_JOB_ROOT fixture_root
#define mount mount_peer
#define umount unmount_peer
#define mkdir mkdir_peer
#include "bk7258_display_job_service.c"
#undef mount
#undef umount
#undef mkdir

static struct bkdisplay_job_status_s wait_state(int state)
{
  struct bkdisplay_job_status_s status;
  for (int i=0; i<2000; i++)
    {
      int ret = bk7258_display_job_status(&status);
      if (!ret && ((int)status.state == state || (state < 0 && status.state >= BKDISPLAY_JOB_DONE))) return status;
      usleep(1000);
    }
  assert(0 && "native worker did not publish expected state");
  return status;
}
static int remove_entry(const char *path, const struct stat *st, int type, struct FTW *walk)
{ (void)st; (void)type; (void)walk; return remove(path); }
int main(int argc, char **argv)
{
  uint64_t id;
  struct bkdisplay_job_status_s status;
  bool success = argc == 3 && !strcmp(argv[1], "success");
  size_t total = 128;
  unsigned char *bytes = NULL;
  if (success)
    {
      struct stat info;
      int fd = open(argv[2], O_RDONLY);
      assert(fd >= 0 && fstat(fd, &info) == 0);
      total = info.st_size;
      bytes = malloc(total);
      assert(bytes && read(fd, bytes, total) == (ssize_t)total);
      assert(close(fd) == 0);
    }
  assert((argc == 2 || success) && mkdtemp(fixture_root));
  /* Initialize the host semaphore ABI, never a product success state. */
  assert(sem_init(&g_job_wake, 0, 0) == 0);
  assert(bk7258_display_job_quiesce(false) == 0);
  create_failure = !strcmp(argv[1], "start-failure");
  mount_failure = !strcmp(argv[1], "mount-failure");
  unmount_failure = !strcmp(argv[1], "unmount-failure");
  bool conflict = !strcmp(argv[1], "volume-conflict");
  if (conflict) assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  int ret = bk7258_display_job_begin(7, total, bkdisplay_job_now()+5000, &id);
  if (create_failure)
    {
      assert(ret == -EAGAIN && created == 0 && mounted == 0);
      assert(bk7258_display_job_status(&status) == 0 && status.state == BKDISPLAY_JOB_FAILED);
      assert(bk7258_display_job_quiesce(true) == 0);
    }
  else
    {
      assert(ret == 0 && created == 1);
      if (!mount_failure && !conflict)
        {
          status = wait_state(BKDISPLAY_JOB_RECEIVING);
          if (success)
            {
              for (size_t offset=0; offset<total; )
                {
                  size_t count=total-offset;
                  if(count>BKDISPLAY_UPLOAD_CHUNK_MAX) count=BKDISPLAY_UPLOAD_CHUNK_MAX;
                  assert(bk7258_display_job_append(7,id,offset,bytes+offset,count)==0);
                  offset+=count;
                  bool observed=false;
                  for(int i=0;i<2000;i++)
                    {
                      if(bk7258_display_job_status(&status)==0 && status.written==offset)
                        {observed=true;break;}
                      usleep(1000);
                    }
                  assert(observed);
                }
              assert(bk7258_display_job_finish(7,id)==0);
            }
          else assert(bk7258_display_job_cancel(7, id) == 0);
        }
      status = wait_state(-1);
      assert(pthread_join(worker, NULL) == 0);
      if (unmount_failure)
        {
          assert(status.state == BKDISPLAY_JOB_UNKNOWN && status.release_error == -EIO);
          assert(bk7258_display_job_quiesce(true) == -EIO);
          assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EBUSY);
        }
      else
        {
          assert(status.state == ((mount_failure || conflict) ? BKDISPLAY_JOB_FAILED : success ? BKDISPLAY_JOB_DONE : BKDISPLAY_JOB_CANCELED));
          assert(bk7258_display_job_quiesce(true) == 0);
          if (conflict) assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
          assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
          assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
          assert(mounted == unmounted);
        }
    }
  free(bytes);
  assert(nftw(fixture_root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
