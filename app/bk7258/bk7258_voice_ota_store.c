/****************************************************************************
 * app/bk7258/bk7258_voice_ota_store.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#define _POSIX_C_SOURCE 200809L
#include "bk7258_voice_ota_store.h"
#include "bk7258_provision_store.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <nuttx/mutex.h>

#define BKVOICE_OTA_RECORD_SIZE 68u

static mutex_t g_lock = NXMUTEX_INITIALIZER;
static struct bkprov_store_s g_store;
static char g_root[160];
static uint64_t g_revision;
static struct bkvoice_ota_intent_s g_intent;
static bool g_started;
static bool g_loaded;
static bool g_present;

static void put32(uint8_t *p, uint32_t value)
{
  p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static void put16(uint8_t *p, uint16_t value)
{
  p[0] = (uint8_t)(value >> 8);
  p[1] = (uint8_t)value;
}

static uint16_t get16(const uint8_t *p)
{
  return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t get32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static bool digest_valid(const uint8_t digest[32])
{
  bool zero = true; bool ones = true;
  unsigned int i;
  for (i = 0; i < 32u; i++)
    {
      zero = zero && digest[i] == 0u;
      ones = ones && digest[i] == 0xffu;
    }
  return !zero && !ones;
}

static bool version_valid(const struct bk7258_mcuboot_version_s *v)
{
  /* MCUboot's erased/unknown fields are invalid independently. */
  if (v->major == 0xffu || v->minor == 0xffu ||
      v->revision == UINT16_MAX || v->build == UINT32_MAX) return false;
  return v->major != 0u || v->minor != 0u || v->revision != 0u || v->build != 0u;
}

static bool version_zero(const struct bk7258_mcuboot_version_s *v)
{
  return v->major == 0u && v->minor == 0u && v->revision == 0u &&
         v->build == 0u;
}

static bool target_valid(const struct bkvoice_ota_intent_s *intent)
{
  return version_valid(&intent->target_version) &&
         intent->target_security_counter > intent->source_security_counter &&
         bk7258_mcuboot_version_compare(&intent->target_version,
                                        &intent->source_version) > 0;
}

static bool intent_valid(const struct bkvoice_ota_intent_s *intent)
{
  return intent != NULL && intent->state >= BKVOICE_OTA_DOWNLOADING &&
         intent->state <= BKVOICE_OTA_TRIAL &&
         digest_valid(intent->manifest_sha256) && version_valid(&intent->source_version) &&
         intent->source_security_counter != 0u &&
         (target_valid(intent) ||
          (intent->state == BKVOICE_OTA_DOWNLOADING &&
           version_zero(&intent->target_version) &&
           intent->target_security_counter == 0u)) &&
         intent->source_boot_generation != 0u && intent->source_boot_generation != UINT32_MAX;
}

static bool same_identity(const struct bkvoice_ota_intent_s *a,
                          const struct bkvoice_ota_intent_s *b)
{
  return memcmp(a->manifest_sha256, b->manifest_sha256, 32) == 0 &&
         a->source_version.major == b->source_version.major &&
         a->source_version.minor == b->source_version.minor &&
         a->source_version.revision == b->source_version.revision &&
         a->source_version.build == b->source_version.build &&
         a->source_security_counter == b->source_security_counter &&
         a->source_boot_generation == b->source_boot_generation;
}

static bool same_target(const struct bkvoice_ota_intent_s *a,
                        const struct bkvoice_ota_intent_s *b)
{
  return bk7258_mcuboot_version_equal(&a->target_version,
                                      &b->target_version) &&
         a->target_security_counter == b->target_security_counter;
}

static int prepare_directory(const char *root)
{
  struct stat info;
  char parent[160];
  char *slash;
  size_t length;
  int ret;
  if (lstat(root, &info) == 0)
    return S_ISDIR(info.st_mode) ? bkprov_store_check_filesystem(root) : -ENOTDIR;
  if (errno != ENOENT) return -errno;
  length = strlen(root);
  if (length < 2 || length >= sizeof(parent)) return -EINVAL;
  memcpy(parent, root, length + 1);
  slash = strrchr(parent, '/');
  if (slash == NULL || slash == parent) return -EINVAL;
  *slash = '\0';
  if (lstat(parent, &info) < 0) return errno == ENOENT ? -EAGAIN : -errno;
  if (!S_ISDIR(info.st_mode)) return -ENOTDIR;
  ret = bkprov_store_check_filesystem(parent);
  if (ret < 0) return ret;
  if (mkdir(root, 0700) < 0 && errno != EEXIST) return -errno;
  if (lstat(root, &info) < 0) return -errno;
  return S_ISDIR(info.st_mode) ? bkprov_store_check_filesystem(root) : -ENOTDIR;
}

static void encode(uint8_t record[BKVOICE_OTA_RECORD_SIZE],
                   const struct bkvoice_ota_intent_s *intent)
{
  memset(record, 0, BKVOICE_OTA_RECORD_SIZE);
  memcpy(record, "BVO2", 4);
  if (intent != NULL)
    {
      record[4] = (uint8_t)intent->state;
      memcpy(record + 8, intent->manifest_sha256, 32);
      /* BVO1 is a fixed wire record, never a native struct image. */
      record[40] = intent->source_version.major;
      record[41] = intent->source_version.minor;
      put16(record + 42, intent->source_version.revision);
      put32(record + 44, intent->source_version.build);
      put32(record + 48, intent->source_security_counter);
      record[52] = intent->target_version.major;
      record[53] = intent->target_version.minor;
      put16(record + 54, intent->target_version.revision);
      put32(record + 56, intent->target_version.build);
      put32(record + 60, intent->target_security_counter);
      put32(record + 64, intent->source_boot_generation);
    }
}

static int decode(const uint8_t record[BKVOICE_OTA_RECORD_SIZE],
                  struct bkvoice_ota_intent_s *intent, bool *present)
{
  uint8_t tombstone[BKVOICE_OTA_RECORD_SIZE];
  if (memcmp(record, "BVO2", 4) != 0 || record[5] != 0 || record[6] != 0 || record[7] != 0)
    return -EBADMSG;
  if (record[4] == 0u)
    {
      encode(tombstone, NULL);
      if (memcmp(record, tombstone, sizeof(tombstone)) != 0) return -EBADMSG;
      *present = false;
      return 0;
    }
  intent->state = (enum bkvoice_ota_state_e)record[4];
  memcpy(intent->manifest_sha256, record + 8, 32);
  intent->source_version.major = record[40];
  intent->source_version.minor = record[41];
  intent->source_version.revision = get16(record + 42);
  intent->source_version.build = get32(record + 44);
  intent->source_security_counter = get32(record + 48);
  intent->target_version.major = record[52];
  intent->target_version.minor = record[53];
  intent->target_version.revision = get16(record + 54);
  intent->target_version.build = get32(record + 56);
  intent->target_security_counter = get32(record + 60);
  intent->source_boot_generation = get32(record + 64);
  if (!intent_valid(intent)) return -EBADMSG;
  *present = true;
  return 0;
}

static int load_locked(void)
{
  uint8_t record[BKVOICE_OTA_RECORD_SIZE];
  size_t size;
  uint64_t revision;
  bool present;
  int ret;
  if (g_loaded) return 0;
  ret = prepare_directory(g_root);
  if (ret < 0) return ret;
  ret = bkprov_store_open(&g_store, g_root);
  if (ret < 0) return ret;
  ret = bkprov_store_load(&g_store, record, sizeof(record), &size, &revision, NULL);
  if (ret == -ENOENT)
    {
      g_revision = 0; g_present = false; g_loaded = true;
      return 0;
    }
  if (ret < 0) return ret;
  if (size != sizeof(record)) return -EBADMSG;
  ret = decode(record, &g_intent, &present);
  if (ret < 0) return ret;
  g_revision = revision; g_present = present; g_loaded = true;
  return 0;
}

int bkvoice_ota_store_start(const char *root)
{
  size_t length;
  int ret;
  if (root == NULL) return -EINVAL;
  length = strlen(root);
  if (length < 2 || length >= sizeof(g_root) || root[0] != '/' || root[length - 1] == '/')
    return -EINVAL;
  ret = nxmutex_lock(&g_lock);
  if (ret < 0) return ret;
  if (g_started) ret = strcmp(g_root, root) == 0 ? -EALREADY : -EBUSY;
  else { memcpy(g_root, root, length + 1); g_started = true; ret = 0; }
  nxmutex_unlock(&g_lock);
  return ret;
}

int bkvoice_ota_store_load(struct bkvoice_ota_intent_s *intent, uint64_t *revision)
{
  int ret;
  if (intent == NULL || revision == NULL) return -EINVAL;
  ret = nxmutex_lock(&g_lock);
  if (ret < 0) return ret;
  if (!g_started) ret = -ENODEV;
  else
    {
      ret = load_locked();
      if (ret == 0)
        {
          *revision = g_revision;
          if (g_present) *intent = g_intent;
          else { memset(intent, 0, sizeof(*intent)); ret = -ENOENT; }
        }
    }
  nxmutex_unlock(&g_lock);
  return ret;
}

static void invalidate(void)
{
  memset(&g_store, 0, sizeof(g_store));
  memset(&g_intent, 0, sizeof(g_intent));
  g_revision = 0; g_loaded = false; g_present = false;
}

int bkvoice_ota_store_commit(const struct bkvoice_ota_intent_s *intent)
{
  uint8_t record[BKVOICE_OTA_RECORD_SIZE];
  uint8_t transaction[16] = {'B', 'V', 'O', '1'};
  int ret;
  if (!intent_valid(intent)) return -EINVAL;
  ret = nxmutex_lock(&g_lock);
  if (ret < 0) return ret;
  if (!g_started) { ret = -ENODEV; goto done; }
  ret = load_locked();
  if (ret < 0) goto done;
  if (g_present)
    {
      if (!same_identity(&g_intent, intent)) { ret = -EALREADY; goto done; }
      if (intent->state < g_intent.state) { ret = -ESTALE; goto done; }
      if (!version_zero(&g_intent.target_version) &&
          !same_target(&g_intent, intent)) { ret = -EALREADY; goto done; }
      if (intent->state == g_intent.state && same_target(&g_intent, intent))
        { ret = 0; goto done; }
    }
  if (g_revision == UINT64_MAX) { ret = -EOVERFLOW; goto done; }
  encode(record, intent);
  memcpy(transaction + 4, intent->manifest_sha256, 12);
  ret = bkprov_store_commit(&g_store, g_revision, transaction, record, sizeof(record));
  if (ret == 0)
    { g_revision++; g_intent = *intent; g_present = true; }
  else if (ret == -EINPROGRESS) invalidate();
done:
  nxmutex_unlock(&g_lock);
  return ret;
}

int bkvoice_ota_store_clear(const uint8_t expected_manifest_sha256[32])
{
  uint8_t record[BKVOICE_OTA_RECORD_SIZE];
  uint8_t transaction[16] = {'B', 'V', 'O', '1', 'C', 'L', 'E', 'A', 'R'};
  int ret;
  if (!digest_valid(expected_manifest_sha256)) return -EINVAL;
  ret = nxmutex_lock(&g_lock);
  if (ret < 0) return ret;
  if (!g_started) { ret = -ENODEV; goto done; }
  ret = load_locked();
  if (ret < 0) goto done;
  if (!g_present) { ret = -ENOENT; goto done; }
  if (memcmp(expected_manifest_sha256, g_intent.manifest_sha256, 32) != 0)
    { ret = -ESTALE; goto done; }
  if (g_revision == UINT64_MAX) { ret = -EOVERFLOW; goto done; }
  encode(record, NULL);
  memcpy(transaction + 9, expected_manifest_sha256, 7);
  ret = bkprov_store_commit(&g_store, g_revision, transaction, record, sizeof(record));
  if (ret == 0)
    { g_revision++; memset(&g_intent, 0, sizeof(g_intent)); g_present = false; }
  else if (ret == -EINPROGRESS) invalidate();
done:
  nxmutex_unlock(&g_lock);
  return ret;
}

int bkvoice_ota_store_reload(void)
{
  int ret = nxmutex_lock(&g_lock);
  if (ret < 0) return ret;
  if (!g_started) ret = -ENODEV;
  else { invalidate(); ret = 0; }
  nxmutex_unlock(&g_lock);
  return ret;
}
