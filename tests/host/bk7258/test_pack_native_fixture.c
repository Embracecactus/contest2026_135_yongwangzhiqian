/* SPDX-License-Identifier: Apache-2.0 */
/* Reuse actual native task/store fixture; only OS scheduling/mount are peers. */
#define main unused_native_fixture_main
#include "test_display_job_service.c"
#undef main

void test_pack_native_start(void)
{
  assert(mkdtemp(fixture_root));
  assert(sem_init(&g_job_wake,0,0)==0);
  assert(bk7258_display_job_quiesce(false)==0);
}

void test_pack_native_finish(const char *expected_path)
{
  struct bkdisplay_job_status_s status;
  int compared=0;
  (void)bk7258_display_job_quiesce(true);
  if(created)
    {
      status=wait_state(-1);
      assert(pthread_join(worker,NULL)==0);
      assert(created==1); /* Reconnect must not create another upload. */
      if(status.state==BKDISPLAY_JOB_DONE)
        {
          char path[512];
          snprintf(path,sizeof(path),"%s/shaniu/display/packs/%s",fixture_root,status.filename);
          FILE *actual=fopen(path,"rb"), *expected=fopen(expected_path,"rb");
          assert(actual && expected);
          unsigned char left[4096],right[4096];
          for(;;)
            {
              size_t a=fread(left,1,sizeof(left),actual),b=fread(right,1,sizeof(right),expected);
              assert(a==b && !memcmp(left,right,a));
              if(a==0){assert(!ferror(actual)&&!ferror(expected));break;}
            }
          assert(fclose(actual)==0 && fclose(expected)==0);
          compared=1;
        }
    }
  char active[512];snprintf(active,sizeof(active),"%s/shaniu/display/active.json",fixture_root);
  assert(access(active,F_OK)<0 && errno==ENOENT);
  assert(mounted==unmounted);
  fprintf(stderr,"native-pack workers=%d installed-files-compared=%d default-unchanged=1\n",created,compared);
  assert(nftw(fixture_root,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0);
}
