/****************************************************************************
 * boards/bk7258/t5_board/src/bk7258_t5_board_sdio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board V1.0.2 TF-card physical binding.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SDIO

#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_pinmux.h>
#include <arch/chip/bk7258_sdio.h>

#ifdef CONFIG_FS_FAT
#  include <nuttx/fs/fs.h>
#  include <nuttx/fs/partition.h>
#  include <nuttx/kthread.h>
#  include <nuttx/signal.h>
#endif

#if BK7258_BOARD_SDIO_U3_FLASH_FITTED
#  error "T5-Board TF and optional U3 flash cannot share the SDIO/SFC pins"
#endif

#ifdef CONFIG_FS_FAT
#  define T5_BOARD_TF_BLOCKDEV       "/dev/mmcsd0"
#  define T5_BOARD_TF_MOUNTROOT      "/mnt"
#  define T5_BOARD_TF_MOUNTPOINT     "/mnt/tf"
#  define T5_BOARD_TF_MOUNT_RETRIES  300u
#  define T5_BOARD_TF_MOUNT_POLL_US  100000u
#  define T5_BOARD_TF_MOUNT_STACK    3072
#  define T5_BOARD_TF_MAX_PARTITIONS 32
#  define T5_BOARD_TF_PARTDEV_LEN    32
#endif

#ifdef CONFIG_FS_FAT
struct bk7258_t5_board_tf_partition_s
{
  size_t index;
  size_t firstblock;
  size_t nblocks;
  bool tried;
};

struct bk7258_t5_board_tf_partitions_s
{
  struct bk7258_t5_board_tf_partition_s
    entry[T5_BOARD_TF_MAX_PARTITIONS];
  size_t count;
  bool truncated;
};
#endif

static bool g_t5_board_sdio_initialized;
#ifdef CONFIG_FS_FAT
static bool g_t5_board_tf_mount_started;
static struct bk7258_t5_board_tf_partitions_s g_t5_board_tf_partitions;
#endif

#ifdef CONFIG_FS_FAT
static void bk7258_t5_board_tf_partition_handler(
  FAR struct partition_s *part, FAR void *arg)
{
  FAR struct bk7258_t5_board_tf_partitions_s *partitions = arg;
  FAR struct bk7258_t5_board_tf_partition_s *entry;

  if (part->nblocks == 0)
    {
      return;
    }

  if (partitions->count >= T5_BOARD_TF_MAX_PARTITIONS)
    {
      partitions->truncated = true;
      return;
    }

  entry = &partitions->entry[partitions->count++];
  entry->index = part->index;
  entry->firstblock = part->firstblock;
  entry->nblocks = part->nblocks;
}

static FAR struct bk7258_t5_board_tf_partition_s *
  bk7258_t5_board_tf_next_partition(void)
{
  FAR struct bk7258_t5_board_tf_partition_s *selected = NULL;
  FAR struct bk7258_t5_board_tf_partition_s *entry;
  size_t index;

  for (index = 0; index < g_t5_board_tf_partitions.count; index++)
    {
      entry = &g_t5_board_tf_partitions.entry[index];
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

static int bk7258_t5_board_tf_mount(void)
{
  FAR struct bk7258_t5_board_tf_partition_s *selected;
  char partdev[T5_BOARD_TF_PARTDEV_LEN];
  int length;
  int ret;

  ret = nx_mount(T5_BOARD_TF_BLOCKDEV, T5_BOARD_TF_MOUNTPOINT,
                 "vfat", 0, NULL);
  if (ret != -EINVAL)
    {
      return ret;
    }

  /* FAT directly accepts a raw volume or DOS MBR, but not a GPT protective
   * MBR.  Discover standard block partitions only for that case, then let
   * the FAT driver select the first usable candidate by actually mounting it.
   */

  memset(&g_t5_board_tf_partitions, 0,
         sizeof(g_t5_board_tf_partitions));
  ret = parse_block_partition(T5_BOARD_TF_BLOCKDEV,
                              bk7258_t5_board_tf_partition_handler,
                              &g_t5_board_tf_partitions);
  if (ret < 0)
    {
      return ret;
    }

  if (g_t5_board_tf_partitions.count == 0)
    {
      return -ENODEV;
    }

  if (g_t5_board_tf_partitions.truncated)
    {
      syslog(LOG_WARNING,
             "T5-Board TF: more than %u partitions; testing first %u\n",
             T5_BOARD_TF_MAX_PARTITIONS, T5_BOARD_TF_MAX_PARTITIONS);
    }

  while ((selected = bk7258_t5_board_tf_next_partition()) != NULL)
    {
      length = snprintf(partdev, sizeof(partdev), T5_BOARD_TF_BLOCKDEV "p%u",
                        (unsigned int)selected->index + 1u);
      if (length < 0 || (size_t)length >= sizeof(partdev))
        {
          return -ENAMETOOLONG;
        }

      ret = register_blockpartition(partdev, 0660, T5_BOARD_TF_BLOCKDEV,
                                    (off_t)selected->firstblock,
                                    (off_t)selected->nblocks);
      if (ret < 0 && ret != -EEXIST)
        {
          return ret;
        }

      ret = nx_mount(partdev, T5_BOARD_TF_MOUNTPOINT, "vfat", 0, NULL);
      if (ret == OK || ret == -EBUSY)
        {
          syslog(LOG_INFO, "T5-Board TF: selected FAT partition %s\n",
                 partdev);
          return OK;
        }

      if (ret != -EINVAL)
        {
          return ret;
        }
    }

  return -EINVAL;
}

static int bk7258_t5_board_tf_mount_worker(int argc, char *argv[])
{
  struct stat statbuf;
  unsigned int retry;
  int ret = -ETIMEDOUT;

  (void)argc;
  (void)argv;

  if ((mkdir(T5_BOARD_TF_MOUNTROOT, 0777) < 0 && errno != EEXIST) ||
      (mkdir(T5_BOARD_TF_MOUNTPOINT, 0777) < 0 && errno != EEXIST))
    {
      ret = -errno;
      syslog(LOG_ERR, "T5-Board TF: mountpoint creation failed: %d\n",
             ret);
      return ret;
    }

  /* T5-Board has no reliable card-detect edge.  Probe only the media that
   * was present before reset, while MMC/SD completes its asynchronous slot
   * enumeration.  Accept raw FAT, DOS MBR and standard GPT partitioning.
   */

  for (retry = 0; retry < T5_BOARD_TF_MOUNT_RETRIES; retry++)
    {
      if (stat(T5_BOARD_TF_BLOCKDEV, &statbuf) == 0)
        {
          ret = bk7258_t5_board_tf_mount();
          if (ret == OK || ret == -EBUSY)
            {
              syslog(LOG_INFO, "T5-Board TF: mounted at %s\n",
                     T5_BOARD_TF_MOUNTPOINT);
              return OK;
            }

          if (ret != -ENOENT && ret != -ENODEV && ret != -EAGAIN &&
              ret != -EINVAL)
            {
              break;
            }
        }

      nxsig_usleep(T5_BOARD_TF_MOUNT_POLL_US);
    }

  syslog(LOG_WARNING, "T5-Board TF: fixed-media mount unavailable: %d\n",
         ret);
  return ret;
}

int bk7258_t5_board_tf_mount_initialize(void)
{
  int pid;

  if (g_t5_board_tf_mount_started)
    {
      return OK;
    }

  pid = kthread_create("bk7258-tf-mount", SCHED_PRIORITY_DEFAULT,
                       T5_BOARD_TF_MOUNT_STACK,
                       bk7258_t5_board_tf_mount_worker, NULL);
  if (pid < 0)
    {
      return pid;
    }

  g_t5_board_tf_mount_started = true;
  return OK;
}
#endif

const struct bk7258_sdio_pin_config_s g_bk7258_board_sdio_pins =
{
  .map_mode = BK7258_BOARD_SDIO_MAP_MODE,
  .clk_pin = BK7258_BOARD_SDIO_CLK_GPIO,
  .cmd_pin = BK7258_BOARD_SDIO_CMD_GPIO,
  .data_pin =
  {
    BK7258_BOARD_SDIO_D0_GPIO,
    BK7258_BOARD_SDIO_D1_GPIO,
    BK7258_BOARD_SDIO_D2_GPIO,
    BK7258_BOARD_SDIO_D3_GPIO,
  },
};

int bk7258_board_sdio_prepare(bool widebus)
{
  (void)widebus;

  if (g_t5_board_sdio_initialized)
    {
      return OK;
    }

  /* T5-Board V1.0.2 has no verified insertion edge.  Keep this optional
   * branch for a future reviewed board revision without giving the board
   * ownership of the SDIO controller pin group.
   */

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  if (bk7258_gpio_configure_input(BK7258_BOARD_SDIO_CARD_DETECT_GPIO,
                                   BK7258_GPIO_PULL_UP) < 0)
    {
      return -EIO;
    }
#endif

  g_t5_board_sdio_initialized = true;
  return OK;
}

bool bk7258_board_sdio_card_present(void)
{
#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  bool level;
#endif

  if (!g_t5_board_sdio_initialized)
    {
      return false;
    }

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  if (bk7258_gpio_read_input(BK7258_BOARD_SDIO_CARD_DETECT_GPIO,
                              &level) < 0)
    {
      return false;
    }

  return BK7258_BOARD_SDIO_CARD_DETECT_ACTIVE_LOW ? !level : level;
#else
  /* NuttX documents an always-present status for slots without reliable
   * insertion information.  The upper half probes once during slot setup;
   * the card must therefore be inserted before reset.
   */

  return true;
#endif
}

#endif /* CONFIG_BK7258_SDIO */
