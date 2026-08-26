/****************************************************************************
 * board/bk7258/boards/t5_board/src/bk7258_t5_board_sdio.c
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
#include <arch/chip/bk7258_sdio.h>

#include <driver/gpio.h>

#ifdef CONFIG_FS_FAT
#  include <nuttx/fs/fs.h>
#  include <nuttx/fs/partition.h>
#  include <nuttx/kthread.h>
#  include <nuttx/signal.h>
#endif

#if BK7258_BOARD_SDIO_U3_FLASH_FITTED
#  error "T5-Board TF and optional U3 flash cannot share the SDIO/SFC pins"
#endif

/* These v3.1.1.9 functions are exported by libdriver.a and used by the
 * official SDIO driver, but its immutable wrapper bundle omits the private
 * gpio_driver.h declaration.  Keep the ABI declarations local to this
 * physical-board wrapper.
 */

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t gpio_sdio_sel(int mode);
extern bk_err_t gpio_sdio_one_line_sel(int mode);

#define T5_BOARD_SDIO_PIN_GROUP0 0

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

static int bk7258_t5_board_sdio_unmap_pin(gpio_id_t gpio_id)
{
  return gpio_dev_unmap(gpio_id) == BK_OK ? OK : -EIO;
}

static int bk7258_t5_board_sdio_configure_pin(gpio_id_t gpio_id)
{
  if (bk_gpio_pull_up(gpio_id) != BK_OK ||
      bk_gpio_set_capacity(gpio_id, GPIO_DRIVER_CAPACITY_3) != BK_OK)
    {
      return -EIO;
    }

  return OK;
}

int bk7258_board_sdio_initialize(bool widebus)
{
  bk_err_t ret;

  if (g_t5_board_sdio_initialized)
    {
      return OK;
    }

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  /* The pinned SDK's default GPIO table enables P2/P3/P4 as SDIO but P10
   * and P11 as UART0.  gpio_sdio_sel() silently ignores an individual
   * gpio_hal_func_map() failure when a pin is already owned, so explicitly
   * release every default-mapped pin before selecting the group.  This
   * matches the official/Tuya sdio_host_init_gpio() ordering and is
   * essential for four-bit D2/D3.  P5/D1 is absent from the pinned default
   * table and therefore starts unmapped; gpio_dev_unmap(P5) would itself
   * return BK_ERR_GPIO_INVALID_OPERATE.
   */

  if (bk7258_t5_board_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO) < 0 ||
      bk7258_t5_board_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO) < 0 ||
      bk7258_t5_board_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO) < 0 ||
      (widebus &&
       (bk7258_t5_board_sdio_unmap_pin(
          (gpio_id_t)BK7258_BOARD_SDIO_D2_GPIO) < 0 ||
        bk7258_t5_board_sdio_unmap_pin(
          (gpio_id_t)BK7258_BOARD_SDIO_D3_GPIO) < 0)))
    {
      return -EIO;
    }

  /* Re-assert the profile-selected SDIO pin group here.  A four-bit
   * profile maps all four data pins even though the host initially starts
   * at one bit; NuttX switches the host only after the card accepts ACMD6.
   * The SDK archive was built with GPIO_DEFAULT_SET_SUPPORT, so
   * bk_sdio_host_init() deliberately skips its own pin-group setup and
   * assumes the board did it beforehand.
   */

  ret = widebus ? gpio_sdio_sel(T5_BOARD_SDIO_PIN_GROUP0) :
                  gpio_sdio_one_line_sel(T5_BOARD_SDIO_PIN_GROUP0);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  if (bk7258_t5_board_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO) < 0 ||
      bk7258_t5_board_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO) < 0 ||
      bk7258_t5_board_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO) < 0)
    {
      return -EIO;
    }

  if (widebus)
    {
      if (bk7258_t5_board_sdio_configure_pin(
            (gpio_id_t)BK7258_BOARD_SDIO_D1_GPIO) < 0 ||
          bk7258_t5_board_sdio_configure_pin(
            (gpio_id_t)BK7258_BOARD_SDIO_D2_GPIO) < 0 ||
          bk7258_t5_board_sdio_configure_pin(
            (gpio_id_t)BK7258_BOARD_SDIO_D3_GPIO) < 0)
        {
          return -EIO;
        }
    }

  /* Only configure a card-detect GPIO when the physical board has a
   * verified insertion edge.  T5-Board V1.0.2 keeps P6 high with and
   * without media, so its board contract leaves the pin untouched and uses
   * NuttX's fixed-media probing model instead.
   */

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  ret = gpio_dev_unmap((gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  (void)bk_gpio_disable_output(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  (void)bk_gpio_enable_input(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  (void)bk_gpio_enable_pull(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  (void)bk_gpio_pull_up(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
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
  level = bk_gpio_get_input(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
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
