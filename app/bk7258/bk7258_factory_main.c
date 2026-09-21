/****************************************************************************
 * app/bk7258/bk7258_factory_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP operator command for the first-use factory transaction.
 *
 *   bkfactory begin --transaction <16-32 hex> --confirm factory-init
 *   bkfactory mount
 *   bkfactory status
 *
 * The command owns two things and nothing else: the flash transaction record
 * (authorization plus progress, outside every filesystem a deployment may
 * initialize) and the single boot-time decision about how the user data
 * partition is mounted. It never formats a filesystem that already holds a
 * valid LittleFS, it never touches the soldered SD NAND, and once the record
 * leaves BK7258_FACTORY_PENDING no later boot can re-enter initialization.
 *
 * The AP product learns the same authorization from a small mirrored record
 * this command writes after a successful mount.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_FACTORY

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#include <arch/chip/bk7258_factory_record.h>
#include <arch/chip/bk7258_image_layout.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKFACTORY_DEVICE   "/dev/mtdblock0"
#define BKFACTORY_TARGET   "/data"
#define BKFACTORY_ROOT     "/data/shaniu"
#define BKFACTORY_IDENTITY BKFACTORY_ROOT "/identity"
#define BKFACTORY_CONFIG   BKFACTORY_ROOT "/config.bin"
#define BKFACTORY_MIRROR   BKFACTORY_ROOT "/factory-state"
#define BKFACTORY_CONFIRM  "factory-init"

/* Mirrored record the AP product reads through RPMsgFS. It carries the same
 * authorization the flash record owns; it is written only after the mount
 * succeeded, so it can never describe file states that do not exist yet.
 */
#define BKFACTORY_MIRROR_MAGIC 0x31464853u /* "SHF1" */

struct bkfactory_mirror_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t state;
  uint32_t generation;
  uint32_t layout_id;
  uint8_t transaction[16];
  uint8_t evidence[32];
  uint32_t crc;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t bkfactory_crc32(FAR const uint8_t *data, size_t length)
{
  uint32_t crc = 0xffffffffu;
  size_t i;
  unsigned int bit;

  for (i = 0; i < length; i++)
    {
      crc ^= data[i];
      for (bit = 0; bit < 8u; bit++)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

  return ~crc;
}

/* The selected layout identity, generated from the partition CSV. */
static const uint8_t g_bkfactory_layout_sha[32] = BK7258_LAYOUT_SHA256_BYTES;

static uint32_t bkfactory_layout_id(void)
{
  return ((uint32_t)g_bkfactory_layout_sha[0] << 24) |
         ((uint32_t)g_bkfactory_layout_sha[1] << 16) |
         ((uint32_t)g_bkfactory_layout_sha[2] << 8) |
         g_bkfactory_layout_sha[3];
}

static bool bkfactory_exists(FAR const char *path)
{
  struct stat info;

  return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int bkfactory_hex(FAR const char *text, FAR uint8_t *target,
                         size_t bytes)
{
  size_t length = strlen(text);
  size_t width = bytes * 2u;
  size_t i;
  size_t slot;

  if (length == 0u || (length & 1u) != 0u || length > width)
    {
      return -EINVAL;
    }

  for (i = 0; i < length; i++)
    {
      char value = text[i];

      if (!((value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F')))
        {
          return -EINVAL;
        }
    }

  /* A shorter operator id is right-aligned inside the 16-byte field. */
  memset(target, 0, bytes);
  for (i = 0; i < length; i += 2u)
    {
      char pair[3] = {text[i], text[i + 1u], '\0'};

      slot = (width - length + i) / 2u;
      target[slot] = (uint8_t)strtoul(pair, NULL, 16);
    }

  return 0;
}

static int bkfactory_mirror(FAR const struct bk7258_factory_record_s *record)
{
  struct bkfactory_mirror_s mirror;
  int fd;
  ssize_t written;

  memset(&mirror, 0, sizeof(mirror));
  mirror.magic = BKFACTORY_MIRROR_MAGIC;
  mirror.version = 1u;
  mirror.length = sizeof(mirror);
  mirror.state = record->state;
  mirror.generation = record->generation;
  mirror.layout_id = record->layout_id;
  memcpy(mirror.transaction, record->transaction, sizeof(mirror.transaction));
  memcpy(mirror.evidence, record->evidence, sizeof(mirror.evidence));
  mirror.crc = bkfactory_crc32((FAR const uint8_t *)&mirror,
                               sizeof(mirror) - sizeof(mirror.crc));

  if (mkdir(BKFACTORY_ROOT, 0700) < 0 && errno != EEXIST)
    {
      return -errno;
    }

  fd = open(BKFACTORY_MIRROR, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
    {
      return -errno;
    }

  written = write(fd, &mirror, sizeof(mirror));
  if (written != (ssize_t)sizeof(mirror))
    {
      (void)close(fd);
      return -EIO;
    }

  if (close(fd) < 0)
    {
      return -errno;
    }

  return 0;
}

static int bkfactory_mount_state(FAR const char *mode,
                                 FAR const struct bk7258_factory_record_s *
                                 record)
{
  struct statfs info;
  bool identity;
  bool config;
  int ret;

  if (statfs(BKFACTORY_TARGET, &info) == 0 &&
      info.f_type == LITTLEFS_SUPER_MAGIC)
    {
      return 0;
    }

  if (mount(BKFACTORY_DEVICE, BKFACTORY_TARGET, "littlefs", 0, mode) < 0)
    {
      return -errno;
    }

  if (statfs(BKFACTORY_TARGET, &info) < 0)
    {
      return -errno;
    }

  if (info.f_type != LITTLEFS_SUPER_MAGIC)
    {
      return -EINVAL;
    }

  /* Derive the published state from what the mounted volume really holds.
   * Only BK7258_FACTORY_PENDING authorizes the initializing mount above;
   * everything below only moves the record forward.
   */

  identity = bkfactory_exists(BKFACTORY_IDENTITY);
  config = bkfactory_exists(BKFACTORY_CONFIG);
  ret = 0;

  if (identity && config)
    {
      ret = bk7258_factory_record_advance(BK7258_FACTORY_DEPLOYED);
    }
  else if (identity)
    {
      ret = bk7258_factory_record_advance(BK7258_FACTORY_UNCLAIMED_READY);
    }
  else if (record->state >= BK7258_FACTORY_IDENTITY_READY)
    {
      /* The state says an identity was committed, the volume says it is gone.
       * That is a fault, never an authorization to generate another one.
       */
      ret = bk7258_factory_record_advance(BK7258_FACTORY_FAULT);
    }
  else if (record->state == BK7258_FACTORY_PENDING)
    {
      ret = bk7258_factory_record_advance(BK7258_FACTORY_STORAGE_READY);
    }

  return ret;
}

static int bkfactory_mount(void)
{
  struct bk7258_factory_record_s record;
  struct bk7258_factory_record_s published;
  const char *mode = "autoformat";
  bool initializing;
  int ret;

  ret = bk7258_factory_record_read(&record);
  if (ret < 0)
    {
      fprintf(stderr, "BKFACTORY MOUNT FAIL stage=record ret=%d\n", ret);
      return ret;
    }

  initializing = record.state == BK7258_FACTORY_PENDING;
  if (!initializing)
    {
      /* Never format content that is not already a valid LittleFS. */
      mode = NULL;
    }

  ret = bkfactory_mount_state(mode, &record);
  if (ret < 0)
    {
      fprintf(stderr,
              "BKFACTORY MOUNT FAIL stage=mount state=%s format=%u ret=%d\n",
              bk7258_factory_state_name(record.state), initializing ? 1u : 0u,
              ret);
      return ret;
    }

  ret = bk7258_factory_record_read(&published);
  if (ret < 0)
    {
      fprintf(stderr, "BKFACTORY MOUNT FAIL stage=reread ret=%d\n", ret);
      return ret;
    }

  if (published.state == BK7258_FACTORY_PENDING)
    {
      published.state = BK7258_FACTORY_STORAGE_READY;
    }

  ret = bkfactory_mirror(&published);
  if (ret < 0)
    {
      fprintf(stderr, "BKFACTORY MOUNT FAIL stage=mirror ret=%d\n", ret);
      return ret;
    }

  printf("BKFACTORY MOUNT PASS state=%s format=%u identity=%s config=%s\n",
         bk7258_factory_state_name(published.state), initializing ? 1u : 0u,
         bkfactory_exists(BKFACTORY_IDENTITY) ? "present" : "absent",
         bkfactory_exists(BKFACTORY_CONFIG) ? "present" : "absent");
  return 0;
}

static int bkfactory_status(void)
{
  struct bk7258_factory_record_s record;
  struct statfs info;
  struct stat entry;
  int ret;

  ret = bk7258_factory_record_read(&record);
  if (ret < 0)
    {
      fprintf(stderr, "BKFACTORY STATUS FAIL stage=record ret=%d\n", ret);
      return ret;
    }

  printf("BKFACTORY STATUS state=%s generation=%lu layout=%08lx "
         "transaction=",
         bk7258_factory_state_name(record.state),
         (unsigned long)record.generation, (unsigned long)record.layout_id);
  for (unsigned int i = 0; i < sizeof(record.transaction); i++)
    {
      printf("%02x", record.transaction[i]);
    }

  printf("\n");

  if (statfs(BKFACTORY_TARGET, &info) == 0)
    {
      printf("BKFACTORY STATUS data=mounted type=%08lx littlefs=%s\n",
             (unsigned long)info.f_type,
             info.f_type == LITTLEFS_SUPER_MAGIC ? "yes" : "no");
      printf("BKFACTORY STATUS identity=%s config=%s\n",
             bkfactory_exists(BKFACTORY_IDENTITY) ? "present" : "absent",
             bkfactory_exists(BKFACTORY_CONFIG) ? "present" : "absent");
    }
  else if (stat(BKFACTORY_TARGET, &entry) == 0)
    {
      printf("BKFACTORY STATUS data=unmounted target=present\n");
    }
  else
    {
      printf("BKFACTORY STATUS data=absent\n");
    }

  return 0;
}

static int bkfactory_begin(FAR const char *transaction, FAR const char *confirm)
{
  struct bk7258_factory_record_s record;
  uint32_t generation;
  int ret;

  if (confirm == NULL || strcmp(confirm, BKFACTORY_CONFIRM) != 0)
    {
      fprintf(stderr, "BKFACTORY BEGIN FAIL ret=%d\n", -EINVAL);
      return -EINVAL;
    }

  ret = bk7258_factory_record_read(&record);
  if (ret < 0)
    {
      fprintf(stderr, "BKFACTORY BEGIN FAIL stage=record ret=%d\n", ret);
      return ret;
    }

  generation = record.generation;

  /* A new deployment is the only path that may leave DEPLOYED. Everything the
   * device does on its own uses bk7258_factory_record_advance().
   */
  memset(&record, 0, sizeof(record));
  record.magic = BK7258_FACTORY_MAGIC;
  record.version = BK7258_FACTORY_VERSION;
  record.length = sizeof(record);
  record.generation = generation == 0u ? 1u : generation + 1u;
  record.state = BK7258_FACTORY_PENDING;
  record.layout_id = bkfactory_layout_id();
  memcpy(record.evidence, g_bkfactory_layout_sha, sizeof(record.evidence));
  ret = bkfactory_hex(transaction, record.transaction,
                      sizeof(record.transaction));
  if (ret < 0)
    {
      fprintf(stderr, "BKFACTORY BEGIN FAIL ret=%d\n", ret);
      return ret;
    }

  ret = bk7258_factory_record_write(&record);
  if (ret < 0)
    {
      fprintf(stderr, "BKFACTORY BEGIN FAIL stage=write ret=%d\n", ret);
      return ret;
    }

  printf("BKFACTORY BEGIN PASS state=pending layout=%08lx\n",
         (unsigned long)record.layout_id);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  if (argc == 2 && strcmp(argv[1], "mount") == 0)
    {
      return bkfactory_mount() < 0 ? 1 : 0;
    }

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      return bkfactory_status() < 0 ? 1 : 0;
    }

  if (argc == 6 && strcmp(argv[1], "begin") == 0 &&
      strcmp(argv[2], "--transaction") == 0 && strcmp(argv[4], "--confirm") == 0)
    {
      return bkfactory_begin(argv[3], argv[5]) < 0 ? 1 : 0;
    }

  fprintf(stderr,
          "usage: %s mount\n"
          "       %s status\n"
          "       %s begin --transaction <32 hex> --confirm %s\n"
          "mount is the boot-time storage decision: only a pending factory\n"
          "transaction may initialize the user filesystem, and a valid\n"
          "LittleFS is never rewritten. status is read-only.\n",
          argv[0], argv[0], argv[0], BKFACTORY_CONFIRM);
  return 1;
}

#endif /* CONFIG_BK7258_APP_FACTORY */
