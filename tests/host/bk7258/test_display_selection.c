/****************************************************************************
 * tests/host/bk7258/test_display_selection.c
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "bk7258_display_store.h"
static unsigned writes;
static bool fail_directory;
ssize_t __real_write(int fd, const void *p, size_t n);
int __real_fsync(int fd);
ssize_t __wrap_write(int fd, const void *p, size_t n)
{ writes++; return __real_write(fd, p, n); }
int __wrap_fsync(int fd)
{
  struct stat s;
  if (fail_directory && fstat(fd, &s) == 0 && S_ISDIR(s.st_mode))
    { fail_directory = false; errno = EIO; return -1; }
  return __real_fsync(fd);
}
static int remove_entry(const char *p, const struct stat *s, int t, struct FTW *w)
{ (void)s; (void)t; (void)w; return remove(p); }
static void text(const char *p, const char *s)
{
  int fd = open(p, O_WRONLY | O_TRUNC | O_CREAT, 0600);
  assert(fd >= 0 && write(fd, s, strlen(s)) == (ssize_t)strlen(s));
  assert(close(fd) == 0);
}
static void install(const char *root, const char *path)
{
  FILE *f = fopen(path, "rb"); assert(f);
  assert(fseek(f, 0, SEEK_END) == 0); long n = ftell(f); rewind(f);
  unsigned char *b = malloc(n); assert(b && fread(b, 1, n, f) == (size_t)n);
  assert(fclose(f) == 0);
  struct bkdisplay_upload_s u = {0};
  assert(bkdisplay_upload_begin(&u, root, n) == 0);
  for (size_t off = 0; off < (size_t)n;)
    { size_t take = n-off; if (take > 4096) take = 4096;
      assert(bkdisplay_upload_append(&u, off, b+off, take) == 0); off += take; }
  assert(bkdisplay_upload_finish(&u, NULL) == 0); free(b);
}
int main(int argc, char **argv)
{
  assert(argc == 3);
  char root[] = "/tmp/selection-XXXXXX", path[512]; assert(mkdtemp(root));
  install(root, argv[1]);
  snprintf(path, sizeof(path), "%s/shaniu/display/active.json", root);
  struct bkdisplay_selection_version_s v, sentinel;
  const char *name = BKDISPLAY_STORE_DEFAULT_PACK;
  assert(bkdisplay_store_selection_version(root, &v) == 0);
  assert(v.revision == 0 && !strcmp(v.filename, name));
  if (!strcmp(argv[2], "migration"))
    {
      text(path, "{\"format\":\"shaniu-display-active/1\",\"pack\":\"shaniu-default-v1.bkep\"}\n");
      unsigned count = writes;
      assert(bkdisplay_store_selection_version(root, &v) == 0 && v.revision == 0);
      assert(writes == count);
      assert(bkdisplay_store_activate_checked(root, name, 0, &v) == 0 && v.revision == 1);
      const char expected[] = "{\"format\":\"shaniu-display-active/2\",\"pack\":\"shaniu-default-v1.bkep\",\"revision\":\"0000000000000001\"}\n";
      char actual[sizeof(expected)]; int fd = open(path, O_RDONLY);
      assert(fd >= 0 && read(fd, actual, sizeof(actual)) == (ssize_t)sizeof(expected)-1);
      assert(!memcmp(actual, expected, sizeof(expected)-1) && close(fd) == 0);
      pid_t child = fork(); assert(child >= 0);
      if (!child) { memset(&v, 0xff, sizeof(v));
        assert(bkdisplay_store_selection_version(root, &v) == 0 && v.revision == 1);
        assert(!strcmp(v.filename, name)); _exit(0); }
      int status; assert(waitpid(child, &status, 0) == child);
      assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
      struct bkdisplay_store_selection_s selected;
      assert(bkdisplay_store_resolve(root, &selected) == 0 && !selected.fallback);
    }
  else if (!strcmp(argv[2], "malformed"))
    {
      const char *bad[] = {
        "{\"pack\":\"../bad.bkep\"}\n",
        "{\"format\":\"shaniu-display-active/2\",\"pack\":\"shaniu-default-v1.bkep\",\"revision\":\"0000000000000000\"}\n",
        "{\"format\":\"shaniu-display-active/2\",\"pack\":\"shaniu-default-v1.bkep\",\"revision\":\"000000000000000G\"}\n" };
      for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
        { text(path, bad[i]); unsigned count = writes;
          assert(bkdisplay_store_selection_version(root, &v) == -EPROTO);
          assert(bkdisplay_store_activate_checked(root, name, 0, &v) == -EPROTO);
          assert(bkdisplay_store_activate(root, name, NULL) == -EPROTO);
          assert(writes == count); }
      assert(bkdisplay_store_reset_selection(root) == 0);
      assert(bkdisplay_store_activate_checked(root, name, 0, &v) == 0);
    }
  else if (!strcmp(argv[2], "overflow"))
    {
      text(path, "{\"format\":\"shaniu-display-active/2\",\"pack\":\"shaniu-default-v1.bkep\",\"revision\":\"ffffffffffffffff\"}\n");
      unsigned count = writes;
      assert(bkdisplay_store_selection_version(root, &v) == 0 && v.revision == UINT64_MAX);
      assert(bkdisplay_store_activate_checked(root, name, UINT64_MAX, &v) == -EOVERFLOW);
      assert(bkdisplay_store_activate(root, name, NULL) == -EOVERFLOW);
      assert(writes == count);
    }
  else
    {
      assert(bkdisplay_store_activate_checked(root, name, 0, &v) == 0 && v.revision == 1);
      if (!strcmp(argv[2], "local-writer"))
        { assert(bkdisplay_store_activate(root, name, NULL) == 0);
          assert(bkdisplay_store_selection_version(root, &v) == 0 && v.revision == 2); }
      else if (!strcmp(argv[2], "directory-failure"))
        { fail_directory = true; memset(&sentinel, 0xa5, sizeof(sentinel)); v = sentinel;
          assert(bkdisplay_store_activate_checked(root, name, 1, &v) == -EIO);
          assert(!memcmp(&v, &sentinel, sizeof(v)));
          assert(bkdisplay_store_selection_version(root, &v) == 0 && v.revision == 2); }
      else
        { assert(!strcmp(argv[2], "stale"));
          assert(bkdisplay_store_activate_checked(root, name, 1, &v) == 0 && v.revision == 2); }
      unsigned count = writes;
      assert(bkdisplay_store_activate_checked(root, name, 1, &v) == -ESTALE);
      assert(writes == count && v.revision == 2);
    }
  assert(nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) == 0);
  puts("CONTRACT_PASS durable selection version");
}
