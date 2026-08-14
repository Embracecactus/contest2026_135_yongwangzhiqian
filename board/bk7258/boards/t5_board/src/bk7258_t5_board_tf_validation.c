/****************************************************************************
 * board/bk7258/boards/t5_board/src/bk7258_t5_board_tf_validation.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded, non-destructive T5-Board TF-card validation through the public
 * NuttX MMC/SD block and FAT filesystem interfaces.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_T5_BOARD_TF_VALIDATION

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/partition.h>
#include <nuttx/kthread.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_sdio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKTFVAL_BLOCKDEV          "/dev/mmcsd0"
#define BKTFVAL_PARTDEV_LEN       24
#define BKTFVAL_PARTNAME_LEN      32
#define BKTFVAL_MAX_PARTITIONS    32
#define BKTFVAL_MOUNTROOT         "/mnt"
#define BKTFVAL_MOUNTPOINT        "/mnt/tf"
#define BKTFVAL_FILE_PREFIX       BKTFVAL_MOUNTPOINT "/BKTF"
#define BKTFVAL_PAYLOAD_SIZE      4096u
#define BKTFVAL_STACKSIZE         4096
#define BKTFVAL_POLL_US           100000u
#define BKTFVAL_INITIAL_TIMEOUT   1200u
#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
#  define BKTFVAL_HOTPLUG_TIMEOUT 1800u
#  define BKTFVAL_MEDIA_MODE       "hotplug"
#else
#  define BKTFVAL_MEDIA_MODE       "fixed"
#endif
#define BKTFVAL_MAGIC             0x46544b42u /* "BKTF" */
#define BKTFVAL_VERSION           1u
#define BKTFVAL_RUNNING           1u
#define BKTFVAL_PASSED            2u
#define BKTFVAL_FAILED            3u

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum bktfval_stage_e
{
  BKTFVAL_STAGE_START = 1,
  BKTFVAL_STAGE_WAIT_INITIAL,
  BKTFVAL_STAGE_MOUNT,
  BKTFVAL_STAGE_CREATE,
  BKTFVAL_STAGE_WRITE,
  BKTFVAL_STAGE_FSYNC,
  BKTFVAL_STAGE_READ,
  BKTFVAL_STAGE_VERIFY,
  BKTFVAL_STAGE_UNLINK,
  BKTFVAL_STAGE_UNMOUNT,
  BKTFVAL_STAGE_WAIT_EJECT,
  BKTFVAL_STAGE_WAIT_REINSERT
};

struct bktfval_diag_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t state;
  int32_t result;
  uint32_t stage;
  uint32_t bus_width;
  uint32_t cycles;
  uint32_t bytes;
  uint32_t checksum;
};

struct bktfval_partition_s
{
  size_t index;
  size_t firstblock;
  size_t nblocks;
  bool tried;
  char name[BKTFVAL_PARTNAME_LEN];
};

struct bktfval_partitions_s
{
  struct bktfval_partition_s entry[BKTFVAL_MAX_PARTITIONS];
  size_t count;
  bool truncated;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Retain the final result in AP SRAM so J-Link can inspect it even when the
 * RPMsg syslog path is unavailable.
 */

volatile struct bktfval_diag_s g_bk7258_tf_validation_diag;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t g_bktfval_payload[BKTFVAL_PAYLOAD_SIZE] aligned_data(4);
static char g_bktfval_blockdev[BKTFVAL_PARTDEV_LEN] = BKTFVAL_BLOCKDEV;
static struct bktfval_partitions_s g_bktfval_partitions;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bktfval_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static uint8_t bktfval_pattern(uint32_t cycle, size_t offset)
{
  return (uint8_t)((offset * 33u + cycle * 17u + 0x5au) & 0xffu);
}

static uint32_t bktfval_checksum(FAR const uint8_t *buffer, size_t length)
{
  uint32_t value = 2166136261u;
  size_t index;

  for (index = 0; index < length; index++)
    {
      value ^= buffer[index];
      value *= 16777619u;
    }

  return value;
}

static void bktfval_partition_handler(FAR struct partition_s *part,
                                      FAR void *arg)
{
  FAR struct bktfval_partitions_s *partitions = arg;
  FAR struct bktfval_partition_s *entry;

  if (part->nblocks == 0)
    {
      return;
    }

  if (partitions->count >= BKTFVAL_MAX_PARTITIONS)
    {
      partitions->truncated = true;
      return;
    }

  entry = &partitions->entry[partitions->count++];
  entry->index = part->index;
  entry->firstblock = part->firstblock;
  entry->nblocks = part->nblocks;
  snprintf(entry->name, sizeof(entry->name), "%s", part->name);
}

static FAR struct bktfval_partition_s *bktfval_next_partition(void)
{
  FAR struct bktfval_partition_s *selected = NULL;
  FAR struct bktfval_partition_s *entry;
  size_t index;

  for (index = 0; index < g_bktfval_partitions.count; index++)
    {
      entry = &g_bktfval_partitions.entry[index];
      if (!entry->tried &&
          (selected == NULL || entry->nblocks > selected->nblocks))
        {
          selected = entry;
        }
    }

  if (selected != NULL)
    {
      selected->tried = true;
    }

  return selected;
}

static int bktfval_mount(void)
{
  FAR struct bktfval_partition_s *selected;
  char partdev[BKTFVAL_PARTDEV_LEN];
  int ret;

  ret = nx_mount(g_bktfval_blockdev, BKTFVAL_MOUNTPOINT, "vfat", 0, NULL);
  if (ret != -EINVAL || strcmp(g_bktfval_blockdev, BKTFVAL_BLOCKDEV) != 0)
    {
      return ret;
    }

  /* The NuttX FAT mount directly understands a raw FAT volume and DOS MBR,
   * but not a GPT protective MBR.  Use the standard partition layer as a
   * non-destructive fallback.  GPT does not guarantee that the largest
   * partition contains FAT, so try candidates from largest to smallest and
   * retain the first partition that the FAT driver actually accepts.
   */

  memset(&g_bktfval_partitions, 0, sizeof(g_bktfval_partitions));
  ret = parse_block_partition(BKTFVAL_BLOCKDEV,
                              bktfval_partition_handler,
                              &g_bktfval_partitions);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bktfval_partitions.count == 0)
    {
      return -ENODEV;
    }

  if (g_bktfval_partitions.truncated)
    {
      syslog(LOG_WARNING,
             "BKTF GPT has more than %u usable partitions; testing first %u\n",
             BKTFVAL_MAX_PARTITIONS, BKTFVAL_MAX_PARTITIONS);
    }

  while ((selected = bktfval_next_partition()) != NULL)
    {
      snprintf(partdev, sizeof(partdev), BKTFVAL_BLOCKDEV "p%u",
               (unsigned int)selected->index + 1u);
      ret = register_blockpartition(partdev, 0660, BKTFVAL_BLOCKDEV,
                                    (off_t)selected->firstblock,
                                    (off_t)selected->nblocks);
      if (ret < 0 && ret != -EEXIST)
        {
          return ret;
        }

      syslog(LOG_INFO,
             "BKTF GPT candidate %s name='%s': first=%lu blocks=%lu\n",
             partdev, selected->name, (unsigned long)selected->firstblock,
             (unsigned long)selected->nblocks);
      ret = nx_mount(partdev, BKTFVAL_MOUNTPOINT, "vfat", 0, NULL);
      if (ret == OK)
        {
          snprintf(g_bktfval_blockdev, sizeof(g_bktfval_blockdev), "%s",
                   partdev);
          syslog(LOG_INFO, "BKTF selected FAT partition %s\n",
                 g_bktfval_blockdev);
          return OK;
        }

      if (ret != -EINVAL)
        {
          return ret;
        }
    }

  return -EINVAL;
}

static int bktfval_wait_blockdev(bool present, uint32_t polls,
                                 enum bktfval_stage_e stage)
{
  struct stat statbuf;
  uint32_t poll;
  bool exists;

  g_bk7258_tf_validation_diag.stage = stage;
  for (poll = 0; poll < polls; poll++)
    {
      errno = 0;
      exists = stat(BKTFVAL_BLOCKDEV, &statbuf) == 0;
      if (!exists && errno != 0 && errno != ENOENT)
        {
          return bktfval_errno();
        }

      if (exists == present)
        {
          return OK;
        }

      usleep(BKTFVAL_POLL_US);
    }

  return -ETIMEDOUT;
}

static int bktfval_write_all(int fd, FAR const uint8_t *buffer,
                             size_t length)
{
  size_t offset = 0;

  while (offset < length)
    {
      ssize_t written = write(fd, buffer + offset, length - offset);
      if (written < 0)
        {
          return bktfval_errno();
        }

      if (written == 0)
        {
          return -EIO;
        }

      offset += written;
    }

  return OK;
}

static int bktfval_read_all(int fd, FAR uint8_t *buffer, size_t length)
{
  size_t offset = 0;

  while (offset < length)
    {
      ssize_t nread = read(fd, buffer + offset, length - offset);
      if (nread < 0)
        {
          return bktfval_errno();
        }

      if (nread == 0)
        {
          return -ENODATA;
        }

      offset += nread;
    }

  return OK;
}

static int bktfval_cycle(uint32_t cycle)
{
  char path[32];
  uint32_t expected;
  unsigned int candidate;
  size_t index;
  bool mounted = false;
  bool owned = false;
  int fd = -1;
  int ret;
  int cleanup_ret;

  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_MOUNT;

  /* Minimal validation profiles do not necessarily create /mnt during
   * bring-up.  mkdir() does not create missing parents, so establish the
   * conventional mount root before its TF-card child directory.
   */

  if (mkdir(BKTFVAL_MOUNTROOT, 0777) < 0 && errno != EEXIST)
    {
      return bktfval_errno();
    }

  if (mkdir(BKTFVAL_MOUNTPOINT, 0777) < 0 && errno != EEXIST)
    {
      return bktfval_errno();
    }

  ret = bktfval_mount();
  if (ret < 0)
    {
      return ret;
    }

  mounted = true;
  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_CREATE;
  for (candidate = 0; candidate < 100; candidate++)
    {
      snprintf(path, sizeof(path), BKTFVAL_FILE_PREFIX "%02u.BIN",
               candidate);
      fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
      if (fd >= 0)
        {
          owned = true;
          break;
        }

      if (errno != EEXIST)
        {
          ret = bktfval_errno();
          goto out;
        }
    }

  if (fd < 0)
    {
      ret = -EEXIST;
      goto out;
    }

  for (index = 0; index < sizeof(g_bktfval_payload); index++)
    {
      g_bktfval_payload[index] = bktfval_pattern(cycle, index);
    }

  expected = bktfval_checksum(g_bktfval_payload,
                              sizeof(g_bktfval_payload));
  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_WRITE;
  ret = bktfval_write_all(fd, g_bktfval_payload,
                          sizeof(g_bktfval_payload));
  if (ret < 0)
    {
      goto out;
    }

  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_FSYNC;
  if (fsync(fd) < 0)
    {
      ret = bktfval_errno();
      goto out;
    }

  if (close(fd) < 0)
    {
      fd = -1;
      ret = bktfval_errno();
      goto out;
    }

  fd = -1;
  memset(g_bktfval_payload, 0, sizeof(g_bktfval_payload));
  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_READ;
  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      ret = bktfval_errno();
      goto out;
    }

  ret = bktfval_read_all(fd, g_bktfval_payload,
                         sizeof(g_bktfval_payload));
  if (ret < 0)
    {
      goto out;
    }

  if (close(fd) < 0)
    {
      fd = -1;
      ret = bktfval_errno();
      goto out;
    }

  fd = -1;
  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_VERIFY;
  for (index = 0; index < sizeof(g_bktfval_payload); index++)
    {
      if (g_bktfval_payload[index] != bktfval_pattern(cycle, index))
        {
          ret = -EILSEQ;
          goto out;
        }
    }

  if (bktfval_checksum(g_bktfval_payload,
                       sizeof(g_bktfval_payload)) != expected)
    {
      ret = -EILSEQ;
      goto out;
    }

  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_UNLINK;
  if (unlink(path) < 0)
    {
      ret = bktfval_errno();
      goto out;
    }

  owned = false;
  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_UNMOUNT;
  ret = nx_umount2(BKTFVAL_MOUNTPOINT, 0);
  if (ret < 0)
    {
      goto out;
    }

  mounted = false;
  g_bk7258_tf_validation_diag.cycles = cycle;
  g_bk7258_tf_validation_diag.bytes += sizeof(g_bktfval_payload);
  g_bk7258_tf_validation_diag.checksum = expected;
  syslog(LOG_INFO,
         "BKTF cycle=%lu/2 PASS width=%u bytes=%u checksum=%08lx\n",
         (unsigned long)cycle, BK7258_SDIO_BUS_WIDTH_4BIT ? 4 : 1,
         (unsigned int)sizeof(g_bktfval_payload),
         (unsigned long)expected);
  return OK;

out:
  if (fd >= 0)
    {
      close(fd);
    }

  if (owned)
    {
      unlink(path);
    }

  if (mounted)
    {
      cleanup_ret = nx_umount2(BKTFVAL_MOUNTPOINT, 0);
      if (ret == OK && cleanup_ret < 0)
        {
          ret = cleanup_ret;
        }
    }

  return ret;
}

static int bktfval_thread(int argc, FAR char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;

  memset((FAR void *)&g_bk7258_tf_validation_diag, 0,
         sizeof(g_bk7258_tf_validation_diag));
  g_bk7258_tf_validation_diag.magic = BKTFVAL_MAGIC;
  g_bk7258_tf_validation_diag.version = BKTFVAL_VERSION;
  g_bk7258_tf_validation_diag.size = sizeof(g_bk7258_tf_validation_diag);
  g_bk7258_tf_validation_diag.state = BKTFVAL_RUNNING;
  g_bk7258_tf_validation_diag.stage = BKTFVAL_STAGE_START;
  g_bk7258_tf_validation_diag.bus_width =
    BK7258_SDIO_BUS_WIDTH_4BIT ? 4 : 1;

  syslog(LOG_INFO,
         "BKTF waiting for FAT card on %s (width=%u, insert-before-reset)\n",
         BKTFVAL_BLOCKDEV, BK7258_SDIO_BUS_WIDTH_4BIT ? 4 : 1);

  ret = bktfval_wait_blockdev(true, BKTFVAL_INITIAL_TIMEOUT,
                              BKTFVAL_STAGE_WAIT_INITIAL);
  if (ret == OK)
    {
      ret = bktfval_cycle(1);
    }

  if (ret == OK)
    {
#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
      syslog(LOG_INFO, "BKTF remove card now; waiting for eject\n");
      ret = bktfval_wait_blockdev(false, BKTFVAL_HOTPLUG_TIMEOUT,
                                  BKTFVAL_STAGE_WAIT_EJECT);

      if (ret == OK)
        {
          syslog(LOG_INFO, "BKTF insert card now; waiting for reinsert\n");
          ret = bktfval_wait_blockdev(true, BKTFVAL_HOTPLUG_TIMEOUT,
                                      BKTFVAL_STAGE_WAIT_REINSERT);
        }
#else
      syslog(LOG_INFO,
             "BKTF fixed-media slot: repeating without eject/reinsert\n");
#endif
    }

  if (ret == OK)
    {
      ret = bktfval_cycle(2);
    }

  g_bk7258_tf_validation_diag.result = ret;
  g_bk7258_tf_validation_diag.state =
    ret == OK ? BKTFVAL_PASSED : BKTFVAL_FAILED;
  if (ret == OK)
    {
      syslog(LOG_INFO,
             "BKTF PASS width=%u cycles=2 bytes=%lu media=%s\n",
             BK7258_SDIO_BUS_WIDTH_4BIT ? 4 : 1,
             (unsigned long)g_bk7258_tf_validation_diag.bytes,
             BKTFVAL_MEDIA_MODE);
    }
  else
    {
      syslog(LOG_ERR, "BKTF FAIL width=%u stage=%lu ret=%d\n",
             BK7258_SDIO_BUS_WIDTH_4BIT ? 4 : 1,
             (unsigned long)g_bk7258_tf_validation_diag.stage, ret);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_t5_board_tf_validation_initialize(void)
{
  int pid;

  pid = kthread_create("bktf-validate", SCHED_PRIORITY_DEFAULT,
                       BKTFVAL_STACKSIZE, bktfval_thread, NULL);
  return pid < 0 ? pid : OK;
}

#endif /* CONFIG_BK7258_T5_BOARD_TF_VALIDATION */
