/* SPDX-License-Identifier: Apache-2.0 */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bk7258_display_store.h"

static unsigned opened, closed, writes_seen, cancel_calls;
static int read_fault, directory_close_fault, file_close_fault;
DIR *__real_opendir(const char *path);
int __real_closedir(DIR *dir);
struct dirent *__real_readdir(DIR *dir);
int __real_close(int fd);
ssize_t __real_write(int fd, const void *data, size_t size);
DIR *__wrap_opendir(const char *path)
{
  DIR *dir = __real_opendir(path);
  if (dir) opened++;
  return dir;
}
int __wrap_closedir(DIR *dir)
{
  int ret = __real_closedir(dir);
  closed++;
  if (directory_close_fault) { directory_close_fault = 0; errno = EIO; return -1; }
  return ret;
}
struct dirent *__wrap_readdir(DIR *dir)
{
  if (read_fault) { read_fault = 0; errno = EIO; return NULL; }
  return __real_readdir(dir);
}
int __wrap_close(int fd)
{
  int ret = __real_close(fd);
  if (file_close_fault) { file_close_fault = 0; errno = EIO; return -1; }
  return ret;
}
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
  writes_seen++;
  return __real_write(fd, data, size);
}
static int remove_entry(const char *p, const struct stat *st, int kind, struct FTW *walk)
{
  (void)st; (void)kind; (void)walk; return remove(p);
}
static bool cancel(void *context)
{
  return ++cancel_calls >= *(unsigned *)context;
}
static void install_fixture(const char *fixture, const char *root, char id)
{
  char path[512]; unsigned char data[16384];
  int source = open(fixture, O_RDONLY);
  assert(source >= 0);
  ssize_t n = read(source, data, sizeof(data));
  assert(n > 128 && n < (ssize_t)sizeof(data) && close(source) == 0);
  /* IDs are outside TOC/payload CRC coverage; source digest deliberately stays
   * the same, proving it is source metadata, not this file's byte digest. */
  memset(data + 48, 0, 32); data[48] = id;
  snprintf(path, sizeof(path), "%s/shaniu/display/packs/%c.bkep", root, id);
  int target = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
  assert(target >= 0 && write(target, data, n) == n && close(target) == 0);
}
int main(int argc, char **argv)
{
  char root[] = "/tmp/shaniu-catalog-XXXXXX", path[512];
  struct bkdisplay_catalog_page_s page;
  assert(argc == 3 && mkdtemp(root));
  if (!strcmp(argv[2], "missing"))
    {
      assert(bkdisplay_store_catalog_page(root, NULL, NULL, NULL, &page) == -ENOENT);
      snprintf(path, sizeof(path), "%s/shaniu", root);
      assert(access(path, F_OK) < 0 && errno == ENOENT);
    }
  else
    {
      assert(bkdisplay_store_ensure(root) == 0);
      for (const char *p = "zedcba"; *p; p++) install_fixture(argv[1], root, *p);
      snprintf(path, sizeof(path), "%s/shaniu/display/active.json", root);
      int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
      assert(fd >= 0 && write(fd, "sentinel", 8) == 8 && close(fd) == 0);
      writes_seen = 0;
      int ret;
      memset(&page, 0xa5, sizeof(page));
      if (!strcmp(argv[2], "pages"))
        {
          assert(bkdisplay_store_catalog_page(root, NULL, NULL, NULL, &page) == 0);
          assert(page.count == 4 && page.more);
          for (unsigned i = 0; i < 4; i++)
            {
              char name[] = "a.bkep"; name[0] += i;
              assert(!strcmp(page.entries[i].filename, name));
              assert(page.entries[i].info.revision == 1 && page.entries[i].info.width == 160);
              assert(!memcmp(page.entries[0].info.source_sha256, page.entries[i].info.source_sha256, 32));
            }
          assert(bkdisplay_store_catalog_page(root, "d.bkep", NULL, NULL, &page) == 0);
          assert(page.count == 2 && !page.more);
          assert(!strcmp(page.entries[0].filename, "e.bkep") && !strcmp(page.entries[1].filename, "z.bkep"));
          assert(bkdisplay_store_catalog_page(root, "z.bkep", NULL, NULL, &page) == 0);
          assert(page.count == 0 && !page.more);
        }
      else if (!strncmp(argv[2], "cancel-", 7))
        {
          unsigned at = !strcmp(argv[2], "cancel-before") ? 1 : 3;
          assert(bkdisplay_store_catalog_page(root, NULL, cancel, &at, &page) == -ECANCELED);
          assert(page.count == 0 && !page.more);
          assert(opened == (at == 1 ? 0u : 1u));
        }
      else
        {
          if (!strcmp(argv[2], "read-error")) read_fault = 1;
          else if (!strcmp(argv[2], "directory-close")) directory_close_fault = 1;
          else if (!strcmp(argv[2], "file-close")) file_close_fault = 1;
          else if (!strcmp(argv[2], "invalid-cursor"))
            {
              assert(bkdisplay_store_catalog_page(root, "../a.bkep", NULL, NULL, &page) == -EINVAL);
              assert(opened == 0 && page.count == 0);
              goto checked;
            }
          else if (!strcmp(argv[2], "scan-limit"))
            {
              for (unsigned i = 0; i < 257; i++)
                {
                  snprintf(path, sizeof(path), "%s/shaniu/display/packs/x%u.txt", root, i);
                  fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
                  assert(fd >= 0 && close(fd) == 0);
                }
            }
          else if (!strcmp(argv[2], "symlink"))
            {
              snprintf(path, sizeof(path), "%s/shaniu/display/packs/a.bkep", root);
              assert(unlink(path) == 0 && symlink(argv[1], path) == 0);
            }
          else if (!strcmp(argv[2], "corrupt"))
            {
              snprintf(path, sizeof(path), "%s/shaniu/display/packs/a.bkep", root);
              fd = open(path, O_WRONLY | O_TRUNC); assert(fd >= 0);
              assert(write(fd, "broken", 6) == 6 && close(fd) == 0); writes_seen = 0;
            }
          else assert(false);
          ret = bkdisplay_store_catalog_page(root, NULL, NULL, NULL, &page);
          if (!strcmp(argv[2], "scan-limit")) assert(ret == -E2BIG);
          else if (!strcmp(argv[2], "symlink")) assert(ret == -ELOOP);
          else if (!strcmp(argv[2], "corrupt")) assert(ret < 0);
          else assert(ret == -EIO);
          assert(page.count == 0 && !page.more);
        }
checked:
      assert(opened == closed && writes_seen == 0);
      snprintf(path, sizeof(path), "%s/shaniu/display/active.json", root);
      char marker[9] = {0}; fd = open(path, O_RDONLY);
      assert(fd >= 0 && read(fd, marker, 9) == 8 && close(fd) == 0 && !strcmp(marker, "sentinel"));
    }
  assert(nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) == 0);
  puts("PASS"); return 0;
}
