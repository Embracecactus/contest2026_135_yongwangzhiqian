/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_nfc_bindings.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static int fail_sync;
int __real_fsync(int);
int __wrap_fsync(int fd)
{
  struct stat st; assert(fstat(fd, &st) == 0);
  if ((fail_sync == 1 && S_ISDIR(st.st_mode)) ||
      (fail_sync == 2 && S_ISREG(st.st_mode))) { errno = EIO; return -1; }
  return __real_fsync(fd);
}
static struct bknfc_card_s card(unsigned n)
{ struct bknfc_card_s c = { .size = 4, .uid = {1,2,3,0} }; c.uid[3] = n; return c; }
int main(int argc, char **argv)
{
  struct bknfc_bindings_s state = {0}, reopened = {0};
  struct bknfc_card_s a = card(4), b = card(5);
  uint64_t duration = 999;
  if (argc == 3 && !strcmp(argv[1], "reopen"))
    {
      assert(bknfc_bindings_open(&state, argv[2]) == 0);
      assert(bknfc_bindings_lookup(&state, &a, &duration) == 0 && duration == 90000);
      puts("FRESH_PROCESS_PASS"); return 0;
    }
  assert(argc == 2);
  char root[] = "/tmp/shaniu-cards-XXXXXX"; assert(mkdtemp(root));
  assert(bknfc_bindings_open(&state, root) == 0 && state.revision == 0);
  if (!strcmp(argv[1], "persist"))
    {
      assert(bknfc_bindings_lookup(&state,&a,&duration) == -ENOENT && duration == 0);
      assert(bknfc_bindings_set(&state,0,1,0,&a,60000) == 0);
      assert(state.revision == 1);
      assert(bknfc_bindings_open(&reopened,root) == 0);
      assert(reopened.revision == 1);
      assert(bknfc_bindings_lookup(&reopened,&a,&duration) == 0 && duration == 60000);
      assert(bknfc_bindings_set(&state,1,2,0,NULL,0) == 0);
      assert(bknfc_bindings_lookup(&state,&a,&duration) == -ENOENT && duration == 0);
    }
  else if (!strcmp(argv[1], "revision"))
    {
      assert(bknfc_bindings_set(&state,0,10,0,&a,60000) == 0);
      assert(bknfc_bindings_set(&state,0,10,0,&a,60000) == 0 && state.revision == 1);
      assert(bknfc_bindings_set(&state,0,10,0,&b,60000) == -EEXIST);
      assert(bknfc_bindings_set(&state,0,11,0,&b,60000) == -ESTALE);
      assert(bknfc_bindings_set(&state,1,11,1,&a,60000) == -EEXIST);
      assert(bknfc_bindings_set(&state,1,11,1,&b,60000) == 0);
      assert(bknfc_bindings_set(&state,0,10,0,&a,60000) == -ESTALE);
      assert(state.revision == 2);
    }
  else if (!strcmp(argv[1], "invalid"))
    {
      assert(bknfc_bindings_set(&state,0,0,0,&a,60000) == -EINVAL);
      assert(bknfc_bindings_set(&state,0,1,8,&a,60000) == -EINVAL);
      assert(bknfc_bindings_set(&state,0,1,0,&a,0) == -EINVAL);
      a.size=5; assert(bknfc_bindings_set(&state,0,1,0,&a,60000) == -EINVAL);
      assert(state.revision == 0 && access(state.store.active,F_OK) < 0);
      a=card(4); uint8_t wire[200]={0}; memcpy(wire,"NCB1",4);
      wire[8]=4; wire[10]=1; wire[27]=1; /* golden record: first slot duration=1 */
      assert(bknfc_bindings_decode(&reopened,wire,sizeof(wire)) == 0);
      wire[31]=1; assert(bknfc_bindings_decode(&reopened,wire,sizeof(wire)) == -EPROTO);
      wire[31]=0; wire[8]=5; assert(bknfc_bindings_decode(&reopened,wire,sizeof(wire)) == -EPROTO);
    }
  else if (!strcmp(argv[1], "corrupt"))
    {
      uint8_t wire[200]={0}, transaction[16]={'B','A','D','1'};
      memcpy(wire,"NCB1",4); transaction[15]=1;
      assert(bkprov_store_commit(&state.store,0,transaction,wire,sizeof(wire))==0);
      assert(bknfc_bindings_open(&reopened,root)==-EPROTO);
      assert(bknfc_bindings_lookup(&reopened,&a,&duration)==-ENODEV && duration==0);
    }
  else if (!strcmp(argv[1], "writefail"))
    {
      assert(bknfc_bindings_set(&state,0,1,0,&a,60000) == 0);
      fail_sync=2;
      assert(bknfc_bindings_set(&state,1,2,0,&a,90000) == -EIO);
      fail_sync=0;
      assert(state.revision == 1);
      assert(bknfc_bindings_lookup(&state,&a,&duration) == 0 && duration == 60000);
      assert(bknfc_bindings_open(&reopened,root) == 0);
      assert(bknfc_bindings_lookup(&reopened,&a,&duration) == 0 && duration == 60000);
    }
  else if (!strcmp(argv[1], "durability"))
    {
      assert(bknfc_bindings_set(&state,0,1,0,&a,60000) == 0);
      fail_sync=1;
      assert(bknfc_bindings_set(&state,1,2,0,&a,90000) == -EINPROGRESS);
      fail_sync=0;
      assert(bknfc_bindings_lookup(&state,&a,&duration) == -EINPROGRESS && duration==0);
      assert(bknfc_bindings_set(&state,1,3,0,NULL,0) == -EINPROGRESS);
      assert(bknfc_bindings_open(&state,root) == -EINPROGRESS);
      pid_t child=fork(); assert(child>=0);
      if(!child) { execl(argv[0],argv[0],"reopen",root,(char *)NULL); _exit(127); }
      int status; assert(waitpid(child,&status,0)==child);
      assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
  else assert(0);
  if (access(state.store.active,F_OK)==0) assert(unlink(state.store.active)==0);
  if (access(state.store.pending,F_OK)==0) assert(unlink(state.store.pending)==0);
  assert(rmdir(root)==0);
  puts("CONTRACT_PASS"); return 0;
}
