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
int native_main(int argc, char **argv)
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

#ifdef TEST_PACK_CONTROL
#include "bk7258_display_job_control.h"
static struct bkpack_control_s wire;
static const uint8_t epoch[16] = {1,2,3,4};
static const uint8_t client[16] = {9,8,7,6};
static uint8_t nonce[16] = {5,6,7,8};
static void p32(uint8_t *p, uint32_t v)
{ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static void p64(uint8_t *p, uint64_t v) { p32(p,v>>32); p32(p+4,v); }
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static uint64_t u64(const uint8_t *p) { return (uint64_t)u32(p)<<32 | u32(p+4); }
static struct bkcontrol_session_s session;
static uint32_t sequence;
static bool use_session;
static int execute_peer(void *ctx, enum bkcontrol_command_e command,
                        uint32_t arg, struct bkcontrol_status_s *status)
{ (void)ctx; (void)command; (void)arg; (void)status; return -ENOTSUP; }
static int config_route(void *ctx, enum bkcontrol_command_e command,
                        uint32_t kind, uint32_t off, const uint8_t *record,
                        size_t size, struct bkcontrol_status_s *status)
{
  assert(ctx==&wire && kind==16);
  if(command==BKCONTROL_CONFIG_READ)
    return bkpack_control_read(&wire,off,record,size,status,bkdisplay_job_now());
  if(command==BKCONTROL_CONFIG_BEGIN) return size>=64&&size<=4160 ? 0 : -EINVAL;
  assert(command==BKCONTROL_CONFIG_APPLY);
  return bkpack_control_apply(&wire,record,size,bkdisplay_job_now());
}
static int exchange(uint32_t command, const void *data, size_t n, uint8_t out[40])
{
  uint8_t frame[BKCONTROL_REQUEST_MAX]={0};
  assert(n<=BKCONTROL_CONFIG_APPEND_MAX);
  memcpy(frame,"SDC1",4); p32(frame+4,command);p32(frame+8,sequence++);p32(frame+12,n);
  if(n)memcpy(frame+16,data,n);
  int ret=bkcontrol_session_packet(&session,frame,16+n,out);
  return ret<0 ? ret : (int32_t)u32(out+16);
}
static void connect_session(void)
{
  uint8_t key[32]={42},out[40];
  bkcontrol_session_close(&session);sequence=0;
  assert(bkcontrol_session_open(&session,key,execute_peer,&wire)==0);
  assert(bkcontrol_session_set_config_handler(&session,config_route)==0);
  assert(exchange(1,key,32,out)==0);
}
static int request(int op, uint64_t id, uint32_t arg, uint32_t ttl,
                   const void *data, size_t size)
{
  uint8_t r[64+4096] = {0};
  memcpy(r,"RJI1",4); p32(r+4,op); memcpy(r+8,epoch,16);
  memcpy(r+24,nonce,16); p64(r+40,id); p32(r+48,arg);
  p32(r+52,ttl); p32(r+56,size);
  if(size) memcpy(r+64,data,size);
  if(!use_session) return bkpack_control_apply(&wire,r,64+size,bkdisplay_job_now());
  uint8_t begin[8],out[40];p32(begin,16);p32(begin+4,64+size);
  int ret=exchange(16,begin,8,out);
  for(size_t off=0;ret==0&&off<64+size;)
    {size_t n=64+size-off;if(n>32)n=32;ret=exchange(17,r+off,n,out);off+=n;}
  return ret==0 ? exchange(18,NULL,0,out) : ret;
}
static void snapshot(uint8_t out[128], uint8_t tag)
{
  struct bkcontrol_status_s result;
  uint8_t query[16] = {0}; query[0]=tag;
  for(size_t off=0;off<128;off+=16)
    {
      if(use_session)
        {
          uint8_t read[20],out[40];p32(read,(16<<16)|off);memcpy(read+4,query,16);
          assert(exchange(15,read,20,out)==0);
          result.config_total=u32(out+20);memcpy(result.config_chunk,out+24,16);
        }
      else assert(bkpack_control_read(&wire,off,query,16,&result,bkdisplay_job_now())==0);
      assert(result.config_total==128);
      memcpy(out+off,result.config_chunk,16);
    }
}
int main(int argc, char **argv)
{
  assert(argc==3 && mkdtemp(fixture_root));
  assert(sem_init(&g_job_wake,0,0)==0);
  assert(bk7258_display_job_quiesce(false)==0);
  assert(bkpack_control_bind(&wire,7,8,client,epoch)==0);
  if(!strcmp(argv[1],"peer"))
    {
      uint8_t key[32]={42}, frame[BKCONTROL_REQUEST_MAX], out[40];
      assert(bkcontrol_session_open(&session,key,execute_peer,&wire)==0);
      assert(bkcontrol_session_set_config_handler(&session,config_route)==0);
      while(fread(frame,1,16,stdin)==16)
        {
          size_t size=u32(frame+12);
          if(size>BKCONTROL_CONFIG_APPEND_MAX || fread(frame+16,1,size,stdin)!=size)break;
          if(bkcontrol_session_packet(&session,frame,16+size,out)<0)break;
          assert(fwrite(out,1,40,stdout)==40 && fflush(stdout)==0);
        }
      (void)bk7258_display_job_quiesce(true);
      if(created){(void)wait_state(-1);assert(pthread_join(worker,NULL)==0);}
      assert(nftw(fixture_root,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0);
      return 0;
    }
  use_session=!strcmp(argv[1],"session");
  if(use_session) connect_session();
  uint8_t before[128], after[128];
  snapshot(before,1);
  assert(!memcmp(before,"RJS1",4) && u32(before+4)==0);
  assert(!memcmp(before+8,epoch,16) && u64(before+40)==0);
  assert(mounted==0 && created==0); /* Reads have no filesystem side effects. */
  if(!strcmp(argv[1],"malformed"))
    {
      uint8_t bad[64]={0};
      assert(bkpack_control_apply(&wire,bad,64,bkdisplay_job_now())==-EINVAL);
      assert(request(1,0,128,0,NULL,0)==-EINVAL);
      assert(request(1,0,127,5000,NULL,0)==-EINVAL);
      assert(request(2,0,0,1,"a",1)==-EINVAL);
      memcpy(bad,"RJI1",4);p32(bad+4,1);bad[8]=99;bad[24]=1;
      p32(bad+48,128);p32(bad+52,5000);
      assert(bkpack_control_apply(&wire,bad,64,bkdisplay_job_now())==-ESTALE);
      memcpy(bad+8,epoch,16);bad[63]=1;
      assert(bkpack_control_apply(&wire,bad,64,bkdisplay_job_now())==-EINVAL);
      bad[63]=0;p32(bad+56,1);
      assert(bkpack_control_apply(&wire,bad,64,bkdisplay_job_now())==-EINVAL);
      assert(created==0 && mounted==0);
    }
  else
    {
      size_t total=128; uint8_t *bytes=NULL;
      bool success=!strcmp(argv[1],"success")||use_session;
      if(success)
        {
          struct stat info; int fd=open(argv[2],O_RDONLY);
          assert(fd>=0 && fstat(fd,&info)==0);
          total=info.st_size; bytes=malloc(total);
          assert(bytes && read(fd,bytes,total)==(ssize_t)total && close(fd)==0);
        }
      assert(request(1,0,total,5000,NULL,0)==0);
      struct bkdisplay_job_status_s status=wait_state(BKDISPLAY_JOB_RECEIVING);
      uint64_t id=status.id;
      nonce[0]^=1;
      assert(request(2,id,0,0,"abc",3)==-ESTALE);
      nonce[0]^=1;
      if(use_session)
        {connect_session();snapshot(after,2);assert(u64(after+40)==id);}

      if(!strcmp(argv[1],"retry"))
        {
          uint64_t deadline=status.deadline_ms;
          assert(request(1,0,total,5000,NULL,0)==0 && created==1);
          assert(request(1,0,total,6000,NULL,0)==-EEXIST);
          assert(bk7258_display_job_status(&status)==0 && status.deadline_ms==deadline);
          assert(request(2,id,0,0,"abc",3)==0);
          assert(request(2,id,0,0,"abc",3)==0);
          assert(request(2,id,0,0,"abd",3)==-EEXIST);
        }
      if(!strcmp(argv[1],"snapshot"))
        {
          snapshot(after,1); assert(!memcmp(before,after,128));
          snapshot(after,2); assert(u64(after+40)==id);
          assert(!memcmp(after+24,nonce,16));
          struct bkcontrol_status_s result; uint8_t stale[16]={3};
          assert(bkpack_control_read(&wire,16,stale,16,&result,bkdisplay_job_now())==-ESTALE);
        }
      if(!strcmp(argv[1],"authority"))
        {
          uint8_t other[16]={99};
          assert(bkpack_control_bind(&wire,7,9,client,epoch)==-ESTALE);
          assert(bkpack_control_bind(&wire,7,8,other,epoch)==-ESTALE);
          assert(bkpack_control_bind(&wire,7,8,client,other)==-ESTALE);
          bkpack_control_invalidate(&wire);
          assert(request(2,id,0,0,"abc",3)==-EACCES);
        }
      if(success)
        {
          for(size_t off=0;off<total;)
            {
              size_t n=total-off; if(n>4096)n=4096;
              int ret;
              do { ret=request(2,id,off,0,bytes+off,n); if(ret==-EAGAIN||ret==-EBUSY)usleep(1000); }
              while(ret==-EAGAIN||ret==-EBUSY);
              assert(ret==0); off+=n;
              for(int i=0;i<2000;i++)
                { if(bk7258_display_job_status(&status)==0&&status.written==off)break; usleep(1000); }
              assert(status.written==off);
            }
          assert(request(3,id,0,0,NULL,0)==0);
        }
      else if(strcmp(argv[1],"authority")) assert(request(4,id,0,0,NULL,0)==0);
      else { int ret=bk7258_display_job_quiesce(true); assert(ret==0||ret==-EAGAIN); }
      status=wait_state(-1); assert(pthread_join(worker,NULL)==0);
      if(success)
        {
          assert(status.state==BKDISPLAY_JOB_DONE);
          assert(request(3,id,0,0,NULL,0)==0);
          assert(request(4,id,0,0,NULL,0)==-EALREADY);
          char path[512]; snprintf(path,sizeof(path),"%s/shaniu/display/active.json",fixture_root);
          assert(access(path,F_OK)<0); /* Installation must not set default. */
        }
      else assert(status.state==BKDISPLAY_JOB_CANCELED);
      if(strcmp(argv[1],"authority"))
        {
          assert(request(2,id,0,0,"abc",3)==-EALREADY);
          assert(request(4,id+1,0,0,NULL,0)==-ESTALE);
          snapshot(after,4); assert(u32(after+4)==(unsigned)status.state);
          assert(u64(after+40)==id);
        }
      free(bytes);
    }
  assert(mounted==unmounted);
  assert(nftw(fixture_root,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0);
  puts("CONTRACT_PASS");
}
#else
int main(int argc, char **argv) { return native_main(argc,argv); }
#endif
