/****************************************************************************
 * tests/host/bk7258/test_bk7258_display_pack.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bk7258_display_pack.h"
#include "bk7258_display_store.h"

static void copy_file(const char *source, const char *target)
{
  uint8_t buffer[4096];
  int input;
  int output;

  input = open(source, O_RDONLY);
  assert(input >= 0);
  output = open(target, O_WRONLY | O_CREAT | O_EXCL, 0600);
  assert(output >= 0);

  for (;;)
    {
      ssize_t nread = read(input, buffer, sizeof(buffer));
      size_t done = 0;

      assert(nread >= 0);
      if (nread == 0)
        {
          break;
        }

      while (done < (size_t)nread)
        {
          ssize_t written = write(output, buffer + done,
                                  (size_t)nread - done);
          assert(written > 0);
          done += (size_t)written;
        }
    }

  assert(close(output) == 0);
  assert(close(input) == 0);
}

static void write_text(const char *path, const char *text)
{
  int fd = open(path, O_WRONLY | O_TRUNC);

  assert(fd >= 0);
  assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
  assert(close(fd) == 0);
}

int main(int argc, char **argv)
{
  char root[] = "/tmp/bkdisplay-test-XXXXXX";
  char packs[BKDISPLAY_PACK_PATH_SIZE];
  char staging[BKDISPLAY_PACK_PATH_SIZE];
  char installed[BKDISPLAY_PACK_PATH_SIZE];
  char staged[BKDISPLAY_PACK_PATH_SIZE];
  char active[BKDISPLAY_PACK_PATH_SIZE];
  char corrupt[BKDISPLAY_PACK_PATH_SIZE];
  char legacy_root[BKDISPLAY_PACK_PATH_SIZE];
  char legacy_display[BKDISPLAY_PACK_PATH_SIZE];
  char legacy_packs[BKDISPLAY_PACK_PATH_SIZE];
  char legacy_installed[BKDISPLAY_PACK_PATH_SIZE];
  struct bkdisplay_store_selection_s selection;
  struct bkdisplay_pack_info_s info;
  struct bkdisplay_pack_s *pack = NULL;
  uint16_t *unmapped;
  uint16_t *left;
  uint16_t *right;
  unsigned int x;
  unsigned int y;
  int fd;

  assert(argc == 2);
  assert(mkdtemp(root) != NULL);
  assert(bkdisplay_store_ensure(root) == 0);
  assert(snprintf(packs, sizeof(packs), "%s/shaniu/display/packs", root) > 0);
  assert(snprintf(staging, sizeof(staging), "%s/shaniu/display/staging",
                  root) > 0);
  assert(snprintf(installed, sizeof(installed), "%s/%s", packs,
                  BKDISPLAY_STORE_DEFAULT_PACK) > 0);
  assert(snprintf(staged, sizeof(staged), "%s/%s", staging,
                  BKDISPLAY_STORE_DEFAULT_PACK) > 0);
  assert(snprintf(active, sizeof(active), "%s/shaniu/display/active.json",
                  root) > 0);
  assert(snprintf(corrupt, sizeof(corrupt), "%s/corrupt.bkep", root) > 0);

  assert(bkdisplay_store_resolve(root, &selection) == -ENOENT);

  /* A factory-copied default pack works before active.json exists. */

  copy_file(argv[1], installed);
  assert(bkdisplay_store_resolve(root, &selection) == 0);
  assert(selection.fallback);
  assert(strcmp(selection.filename, BKDISPLAY_STORE_DEFAULT_PACK) == 0);
  assert(strcmp(selection.info.pack_id, "shaniu-default-v1") == 0);
  assert(selection.info.revision == 1);
  assert(unlink(installed) == 0);

  /* Windows may select an existing uppercase legacy tree even when the
   * operator enters the canonical lowercase path.  Resolve that tree only
   * when no canonical active marker or default pack exists.
   */

  assert(snprintf(legacy_root, sizeof(legacy_root), "%s/SHANIU", root) > 0);
  assert(snprintf(legacy_display, sizeof(legacy_display),
                  "%s/DISPLAY", legacy_root) > 0);
  assert(snprintf(legacy_packs, sizeof(legacy_packs),
                  "%s/PACKS", legacy_display) > 0);
  assert(snprintf(legacy_installed, sizeof(legacy_installed), "%s/%s",
                  legacy_packs, BKDISPLAY_STORE_DEFAULT_PACK) > 0);
  assert(mkdir(legacy_root, 0700) == 0);
  assert(mkdir(legacy_display, 0700) == 0);
  assert(mkdir(legacy_packs, 0700) == 0);
  copy_file(argv[1], legacy_installed);
  assert(bkdisplay_store_resolve(root, &selection) == 0);
  assert(selection.fallback);
  assert(strcmp(selection.path, legacy_installed) == 0);
  assert(unlink(legacy_installed) == 0);
  assert(rmdir(legacy_packs) == 0);
  assert(rmdir(legacy_display) == 0);
  assert(rmdir(legacy_root) == 0);

  /* A staged pack is fully validated, moved, and activated through a
   * fsynced temporary marker rename.
   */

  copy_file(argv[1], staged);
  assert(bkdisplay_store_install(root, BKDISPLAY_STORE_DEFAULT_PACK,
                                 &selection) == 0);
  assert(!selection.fallback);
  assert(access(staged, F_OK) < 0 && errno == ENOENT);
  assert(access(installed, R_OK) == 0);
  assert(bkdisplay_store_resolve(root, &selection) == 0);
  assert(!selection.fallback);

  assert(bkdisplay_pack_open(installed, &pack, &info) == 0);
  assert(info.width == BKDISPLAY_CANVAS_WIDTH);
  assert(info.height == BKDISPLAY_CANVAS_HEIGHT);
  assert(info.entry_count == 11);
  unmapped = malloc(BKDISPLAY_CANVAS_PIXELS * sizeof(*unmapped));
  left = malloc(BKDISPLAY_CANVAS_PIXELS * sizeof(*left));
  right = malloc(BKDISPLAY_CANVAS_PIXELS * sizeof(*right));
  assert(unmapped != NULL && left != NULL && right != NULL);
  assert(bkdisplay_pack_render(pack, "neutral", BKDISPLAY_SIDE_UNMAPPED,
                               unmapped, BKDISPLAY_CANVAS_PIXELS) == 0);
  assert(bkdisplay_pack_render(pack, "neutral", BKDISPLAY_SIDE_LEFT,
                               left, BKDISPLAY_CANVAS_PIXELS) == 0);
  assert(bkdisplay_pack_render(pack, "neutral", BKDISPLAY_SIDE_RIGHT,
                               right, BKDISPLAY_CANVAS_PIXELS) == 0);
  assert(memcmp(unmapped, left,
                BKDISPLAY_CANVAS_PIXELS * sizeof(*left)) == 0);
  for (y = 0; y < BKDISPLAY_CANVAS_HEIGHT; y++)
    {
      for (x = 0; x < BKDISPLAY_CANVAS_WIDTH; x++)
        {
          assert(right[y * BKDISPLAY_CANVAS_WIDTH + x] ==
                 left[y * BKDISPLAY_CANVAS_WIDTH +
                      (BKDISPLAY_CANVAS_WIDTH - 1u - x)]);
        }
    }

  assert(bkdisplay_pack_render(pack, "missing", BKDISPLAY_SIDE_UNMAPPED,
                               left, BKDISPLAY_CANVAS_PIXELS) == -EADDRNOTAVAIL);
  bkdisplay_pack_close(pack);
  pack = NULL;
  free(right);
  free(left);
  free(unmapped);

  /* A damaged payload and malformed active marker fail closed. */

  copy_file(argv[1], corrupt);
  fd = open(corrupt, O_RDWR);
  assert(fd >= 0);
  assert(lseek(fd, -1, SEEK_END) >= 0);
  {
    uint8_t byte;

    assert(read(fd, &byte, 1) == 1);
    byte ^= 0x01u;
    assert(lseek(fd, -1, SEEK_CUR) >= 0);
    assert(write(fd, &byte, 1) == 1);
  }
  assert(close(fd) == 0);
  assert(bkdisplay_pack_open(corrupt, &pack, NULL) == -EBADMSG);
  assert(pack == NULL);

  write_text(active, "{\"pack\":\"../escape.bkep\"}\n");
  assert(bkdisplay_store_resolve(root, &selection) == -EPROTO);
  /* A corrupt marker has no trustworthy revision. Preserve it until the
   * explicit selection reset, then exercise recovery with a new selection.
   */

  assert(bkdisplay_store_activate(root, BKDISPLAY_STORE_DEFAULT_PACK,
                                  &selection) == -EPROTO);
  assert(bkdisplay_store_resolve(root, &selection) == -EPROTO);
  assert(bkdisplay_store_reset_selection(root) == 0);
  assert(bkdisplay_store_activate(root, BKDISPLAY_STORE_DEFAULT_PACK,
                                  &selection) == 0);
  assert(bkdisplay_store_install(root, "../escape.bkep", NULL) == -EINVAL);

  assert(unlink(corrupt) == 0);
  assert(unlink(active) == 0);
  assert(unlink(installed) == 0);
  assert(rmdir(staging) == 0);
  assert(rmdir(packs) == 0);
  assert(snprintf(active, sizeof(active), "%s/shaniu/display", root) > 0);
  assert(rmdir(active) == 0);
  assert(snprintf(active, sizeof(active), "%s/shaniu", root) > 0);
  assert(rmdir(active) == 0);
  assert(rmdir(root) == 0);

  printf("BKDISPLAY_PACK_HOST_PASS\n");
  return 0;
}
