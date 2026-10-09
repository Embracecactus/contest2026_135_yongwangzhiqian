/****************************************************************************
 * tests/host/bk7258/test_display_upload.c
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
#include <unistd.h>
#include "bk7258_display_store.h"

static int fault_fd = -1;
static int write_fault;
static int sync_fault;
static int close_fault;
static int close_calls;
static int directory_fault;
ssize_t __real_write(int fd, const void *data, size_t size);
int __real_fsync(int fd);
int __real_close(int fd);

ssize_t __wrap_write(int fd, const void *data, size_t size)
{
  if (fd == fault_fd && write_fault)
    {
      write_fault = 0;
      errno = EIO;
      return -1;
    }
  return __real_write(fd, data, size);
}

int __wrap_fsync(int fd)
{
  struct stat info;
  if (directory_fault && fstat(fd, &info) == 0 && S_ISDIR(info.st_mode))
    {
      directory_fault = 0;
      errno = EIO;
      return -1;
    }
  if (fd == fault_fd && sync_fault)
    {
      sync_fault = 0;
      errno = EIO;
      return -1;
    }
  return __real_fsync(fd);
}

int __wrap_close(int fd)
{
  if (fd == fault_fd)
    {
      close_calls++;
      if (close_fault)
        {
          close_fault = 0;
          assert(__real_close(fd) == 0);
          errno = EINTR;
          return -1;
        }
    }
  return __real_close(fd);
}

static int remove_entry(const char *path, const struct stat *st, int type,
                        struct FTW *walk)
{
  (void)st; (void)type; (void)walk;
  return remove(path);
}

int main(int argc, char **argv)
{
  char root[] = "/tmp/bkdisplay-upload-XXXXXX";
  char path[512];
  struct stat info;
  unsigned char *bytes;
  size_t size;
  int fd;
  assert(argc >= 3 && mkdtemp(root) != NULL);
  fd = open(argv[1], O_RDONLY);
  assert(fd >= 0 && fstat(fd, &info) == 0);
  size = (size_t)info.st_size;
  bytes = malloc(size);
  assert(bytes != NULL && read(fd, bytes, size) == (ssize_t)size);
  assert(close(fd) == 0 && bkdisplay_store_ensure(root) == 0);
  snprintf(path, sizeof(path), "%s/shaniu/display/staging/.upload.bkep", root);

  if (strcmp(argv[2], "collision") == 0)
    {
      char kept[5];
      fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
      assert(fd >= 0 && write(fd, "owned", 5) == 5 && close(fd) == 0);
      assert(bkdisplay_store_import(root, bytes, size, NULL) == -EEXIST);
      fd = open(path, O_RDONLY);
      assert(fd >= 0 && read(fd, kept, 5) == 5);
      assert(memcmp(kept, "owned", 5) == 0 && close(fd) == 0);
    }
#ifndef UPLOAD_BASELINE
  else
    {
      struct bkdisplay_upload_s upload = {0};
      struct bkdisplay_upload_s competitor = {0};
      struct bkdisplay_store_selection_s selected;
      size_t offset = 0;
      if (strcmp(argv[2], "preserve") == 0 ||
          strcmp(argv[2], "corrupt") == 0 ||
          strncmp(argv[2], "activate-", 9) == 0)
        {
          assert(bkdisplay_store_import(root, bytes, size, NULL) == 0);
        }
      if (strcmp(argv[2], "preserve") == 0 ||
          strncmp(argv[2], "activate-", 9) == 0)
        {
          assert(argc == 4);
          fd = open(argv[3], O_RDONLY);
          assert(fd >= 0 && fstat(fd, &info) == 0);
          size = (size_t)info.st_size;
          bytes = realloc(bytes, size);
          assert(bytes != NULL && read(fd, bytes, size) == (ssize_t)size);
          assert(close(fd) == 0);
        }
      assert(bkdisplay_upload_begin(&upload, root, size) == 0);
      assert(bkdisplay_upload_begin(&competitor, root, size) == -EEXIST);
      if (strcmp(argv[2], "write-failure") == 0)
        {
          fault_fd = upload.fd; /* External syscall fault boundary only. */
          write_fault = 1;
          assert(bkdisplay_upload_append(&upload, 0, bytes, 3) == -EIO);
          assert(bkdisplay_upload_finish(&upload, NULL) == -EALREADY);
          assert(access(path, F_OK) < 0 && errno == ENOENT);
        }
      else if (strcmp(argv[2], "close-failure") == 0)
        {
          int other;
          fault_fd = upload.fd;
          close_fault = 1;
          assert(bkdisplay_upload_cancel(&upload) == -EINTR);
          other = open("/dev/null", O_RDONLY);
          assert(other == fault_fd);
          assert(bkdisplay_upload_cancel(&upload) == -EINTR);
          assert(bkdisplay_upload_begin(&upload, root, size) == -EINTR);
          assert(bkdisplay_upload_finish(&upload, NULL) == -EINTR);
          assert(close_calls == 1 && fcntl(other, F_GETFD) >= 0);
          fault_fd = -1;
          assert(close(other) == 0);
        }
      else if (strcmp(argv[2], "cancel") == 0)
        {
          assert(bkdisplay_upload_append(&upload, 0, bytes, 3) == 0);
          assert(bkdisplay_upload_cancel(&upload) == 0);
          assert(bkdisplay_upload_cancel(&upload) == 0);
          assert(bkdisplay_upload_append(&upload, 3, bytes, 1) == -EALREADY);
          assert(bkdisplay_upload_finish(&upload, NULL) == -EALREADY);
          assert(access(path, F_OK) < 0 && errno == ENOENT);
        }
      else
        {
          assert(bkdisplay_upload_finish(&upload, NULL) == -EAGAIN);
          assert(bkdisplay_upload_append(&upload, 1, bytes, 1) == -EINVAL);
          assert(bkdisplay_upload_append(&upload, 0, bytes, 0) == -EINVAL);
          assert(bkdisplay_upload_append(&upload, 0, bytes,
                                          BKDISPLAY_UPLOAD_CHUNK_MAX + 1) == -EINVAL);
          if (strcmp(argv[2], "corrupt") == 0) bytes[0] ^= 1;
          while (offset < size)
            {
              size_t count = strcmp(argv[2], "fragmented") == 0 ? 7 : 4096;
              if (count > size - offset) count = size - offset;
              assert(bkdisplay_upload_append(&upload, offset, bytes + offset, count) == 0);
              assert(bkdisplay_upload_append(&upload, offset, bytes + offset, count) == -EINVAL);
              offset += count;
            }
          assert(bkdisplay_upload_append(&upload, offset, bytes, 1) == -EINVAL);
          if (strcmp(argv[2], "directory-failure") == 0)
            {
              directory_fault = 1;
              assert(bkdisplay_upload_finish(&upload, NULL) == -EIO);
              snprintf(path, sizeof(path), "%s/shaniu/display/active.json", root);
              assert(access(path, F_OK) < 0 && errno == ENOENT);
              snprintf(path, sizeof(path), "%s/shaniu/display/packs/shaniu-default-v1.bkep", root);
              assert(access(path, R_OK) == 0);
            }
          else if (strcmp(argv[2], "sync-failure") == 0)
            {
              fault_fd = upload.fd;
              sync_fault = 1;
              assert(bkdisplay_upload_finish(&upload, NULL) == -EIO);
              assert(access(path, F_OK) < 0 && errno == ENOENT);
              assert(bkdisplay_store_resolve(root, &selected) == -ENOENT);
            }
          else if (strcmp(argv[2], "corrupt") == 0)
            {
              assert(bkdisplay_upload_finish(&upload, NULL) < 0);
              assert(bkdisplay_store_resolve(root, &selected) == 0);
              assert(strcmp(selected.filename, "shaniu-default-v1.bkep") == 0);
            }
          else
            {
              assert(bkdisplay_upload_finish(&upload, &selected) == 0);
              assert(access(selected.path, R_OK) == 0);
              assert(bkdisplay_upload_finish(&upload, NULL) == -EALREADY);
              assert(bkdisplay_upload_cancel(&upload) == -EALREADY);
              snprintf(path, sizeof(path), "%s/shaniu/display/active.json", root);
              if (strcmp(argv[2], "preserve") == 0 ||
                  strncmp(argv[2], "activate-", 9) == 0)
                {
                  struct bkdisplay_store_selection_s prior;
                  assert(bkdisplay_store_resolve(root, &prior) == 0);
                  assert(strcmp(prior.filename, "shaniu-default-v1.bkep") == 0);
                }
              else
                {
                  assert(access(path, F_OK) < 0 && errno == ENOENT);
                }
              if (strcmp(argv[2], "activate-collision") == 0)
                {
                  char temporary[512], kept[5];
                  struct bkdisplay_store_selection_s prior;
                  snprintf(temporary, sizeof(temporary),
                           "%s/shaniu/display/.active.json.tmp", root);
                  fd = open(temporary, O_CREAT | O_EXCL | O_WRONLY, 0600);
                  assert(fd >= 0 && write(fd, "owned", 5) == 5 && close(fd) == 0);
                  assert(bkdisplay_store_activate(root, selected.filename, NULL) == -EEXIST);
                  fd = open(temporary, O_RDONLY);
                  assert(fd >= 0 && read(fd, kept, 5) == 5 && close(fd) == 0);
                  assert(memcmp(kept, "owned", 5) == 0);
                  assert(bkdisplay_store_resolve(root, &prior) == 0);
                  assert(strcmp(prior.filename, "shaniu-default-v1.bkep") == 0);
                  assert(unlink(temporary) == 0); /* Explicit fixture cleanup. */
                }
              if (strcmp(argv[2], "activate-directory-failure") == 0)
                {
                  struct bkdisplay_store_selection_s result, unchanged;
                  memset(&result, 0x5a, sizeof(result));
                  unchanged = result;
                  directory_fault = 1;
                  assert(bkdisplay_store_activate(root, selected.filename, &result) == -EIO);
                  assert(memcmp(&result, &unchanged, sizeof(result)) == 0);
                  /* Rename already happened: do not claim rollback/durability. */
                  assert(bkdisplay_store_resolve(root, &result) == 0);
                  assert(strcmp(result.filename, selected.filename) == 0);
                }
              assert(bkdisplay_store_activate(root, selected.filename, NULL) == 0);
              assert(access(path, R_OK) == 0);
              /* A duplicate import cannot replace the valid selected pack. */
              assert(bkdisplay_store_import(root, bytes, size, NULL) == -EEXIST);
              assert(bkdisplay_store_resolve(root, &selected) == 0);
              assert(strcmp(selected.filename,
                            (strcmp(argv[2], "preserve") == 0 ||
                             strncmp(argv[2], "activate-", 9) == 0) ?
                            "shaniu-upload-v1.bkep" : "shaniu-default-v1.bkep") == 0);
            }
        }
    }
#endif
  free(bytes);
  assert(nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) == 0);
  puts("CONTRACT_PASS");
  return 0;
}
