/****************************************************************************
 * app/bk7258/bk7258_factory_main.c
 * SPDX-License-Identifier: Apache-2.0
 * Startup consumer of a host-authorized factory journal. Never creates one.
 ****************************************************************************/
#include <nuttx/config.h>
#ifdef CONFIG_BK7258_APP_FACTORY
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <arch/chip/bk7258_factory.h>

#define DEVICE  "/dev/mtdblock0"
#define TARGET  "/data"
#define TOKEN   "/data/.shaniu-factory"
#define PENDING "/data/.shaniu-factory.pending"

static int token_read(const char *name, const uint8_t transaction[16])
{
  uint8_t token[BK7258_FACTORY_TOKEN_SIZE + 1];
  int fd = open(name, O_RDONLY);
  if (fd < 0) return -errno;
  ssize_t count = read(fd, token, sizeof(token));
  int ret = count < 0 ? -errno : count != BK7258_FACTORY_TOKEN_SIZE ||
            memcmp(token, "SFB1", 4) || token[4] != 1 ||
            (token[5] != 1 && token[5] != 2) ||
            memcmp(token + 8, transaction, 16) ? -EBADMSG : 0;
  if (!ret)
    for (size_t i = 6; i < BK7258_FACTORY_TOKEN_SIZE; i++)
      if ((i < 8 || i >= 24) && token[i]) ret = -EBADMSG;
  if (close(fd) < 0 && !ret) ret = -errno;
  return ret;
}

static int prepare_token(const uint8_t transaction[16])
{
  uint8_t token[BK7258_FACTORY_TOKEN_SIZE] = {'S', 'F', 'B', '1', 1, 1};
  memcpy(token + 8, transaction, 16);
  /* The active permit is published only after the raw journal is READY. */
  int fd = open(PENDING, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return -errno;
  size_t offset = 0;
  int ret = 0;
  while (offset < sizeof(token))
    {
      ssize_t n = write(fd, token + offset, sizeof(token) - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { ret = n < 0 ? -errno : -EIO; break; }
      offset += n;
    }
  if (!ret && fsync(fd) < 0) ret = -errno;
  if (close(fd) < 0 && !ret) ret = -errno;
  return ret;
}

static int factory_mount(void)
{
  struct bk7258_factory_status_s state;
  struct statfs info;
  int ret = bk7258_factory_status(&state);
  if (ret == -ENOENT)
    {
      /* Existing deployments have no request: mount only, never format. */
      if (statfs(TARGET, &info) == 0 && info.f_type == LITTLEFS_SUPER_MAGIC)
        return 0;
      return mount(DEVICE, TARGET, "littlefs", 0, NULL) < 0 ? -errno : 0;
    }
  if (ret < 0) return ret;
  if (state.phase == BK7258_FACTORY_REQUESTED)
    {
      /* Reject a request against a mounted/previously initialized volume. */
      if (statfs(TARGET, &info) == 0 && info.f_type == LITTLEFS_SUPER_MAGIC)
        return -EBUSY;
      ret = bk7258_factory_advance(state.phase, BK7258_FACTORY_FORMATTING);
      if (ret < 0) return ret;
      state.phase = BK7258_FACTORY_FORMATTING;
    }
  if (statfs(TARGET, &info) < 0 || info.f_type != LITTLEFS_SUPER_MAGIC)
    {
      if (mount(DEVICE, TARGET, "littlefs", 0,
                state.phase == BK7258_FACTORY_FORMATTING ? "autoformat" : NULL) < 0)
        return -errno;
    }
  if (statfs(TARGET, &info) < 0) return -errno;
  if (info.f_type != LITTLEFS_SUPER_MAGIC) return -EXDEV;
  if (state.phase == BK7258_FACTORY_FORMATTING)
    {
      ret = prepare_token(state.transaction);
      if (ret < 0) return ret;
      ret = bk7258_factory_advance(state.phase, BK7258_FACTORY_READY);
      if (ret < 0) return ret;
    }
  ret = token_read(TOKEN, state.transaction);
  if (ret == -ENOENT)
    {
      /* After READY, only finish the already durable pending permit.
       * Never recreate missing authorization on an established device.
       */
      ret = token_read(PENDING, state.transaction);
      if (!ret && rename(PENDING, TOKEN) < 0) ret = -errno;
      if (!ret) ret = token_read(TOKEN, state.transaction);
    }
  if (!ret) sync();
  return ret;
}

int main(int argc, char *argv[])
{
  if (argc != 2 || strcmp(argv[1], "mount"))
    {
      fprintf(stderr, "usage: bkfactory mount (startup; consumes existing authorization only)\n");
      return 1;
    }
  int ret = factory_mount();
  printf("BKFACTORY storage ready=%d result=%d\n", ret == 0, ret);
  return ret < 0 ? 1 : 0;
}
#endif
