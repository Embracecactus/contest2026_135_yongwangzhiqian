/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/ap/
 * bk7258_ota_catalog.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verify exact signed catalog bytes before parsing their fail-closed schema.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_OTA_MANAGER

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <netutils/cJSON.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include <arch/chip/bk7258_image_layout.h>
#include <arch/chip/bk7258_ota_catalog.h>

#define BK7258_OTA_CATALOG_FORMAT "bk7258.ota/2"
#define BK7258_OTA_BOARD_FAMILY   "bk7258"

#if defined(CONFIG_ARCH_BOARD_CUSTOM_NAME)
#  define BK7258_OTA_PHYSICAL_BOARD CONFIG_ARCH_BOARD_CUSTOM_NAME
#elif defined(CONFIG_ARCH_BOARD)
#  define BK7258_OTA_PHYSICAL_BOARD CONFIG_ARCH_BOARD
#else
#  error "BK7258 OTA requires the NuttX physical-board identity"
#endif

extern const uint8_t bk7258_ota_catalog_public_key_der[];
extern const size_t bk7258_ota_catalog_public_key_der_size;

static const uint8_t g_bk7258_ota_layout_sha256[BK7258_OTA_SHA256_SIZE] =
  BK7258_LAYOUT_SHA256_BYTES;

static bool bk7258_ota_catalog_exact_object(
  const cJSON *object, const char *const *names, size_t count)
{
  const cJSON *child;
  size_t observed = 0u;
  size_t index;

  if (!cJSON_IsObject(object))
    {
      return false;
    }

  cJSON_ArrayForEach(child, object)
    {
      observed++;
    }
  if (observed != count)
    {
      return false;
    }

  for (index = 0u; index < count; index++)
    {
      if (cJSON_GetObjectItemCaseSensitive(object, names[index]) == NULL)
        {
          return false;
        }
    }

  return true;
}

static int bk7258_ota_catalog_hex(const cJSON *item, uint8_t *output,
                                  size_t size)
{
  size_t index;

  if (!cJSON_IsString(item) || item->valuestring == NULL ||
      strlen(item->valuestring) != size * 2u)
    {
      return -EINVAL;
    }

  for (index = 0u; index < size; index++)
    {
      char high = item->valuestring[index * 2u];
      char low = item->valuestring[index * 2u + 1u];
      uint8_t high_value;
      uint8_t low_value;

      if ((high < '0' || (high > '9' && high < 'a') || high > 'f') ||
          (low < '0' || (low > '9' && low < 'a') || low > 'f'))
        {
          return -EINVAL;
        }

      high_value = high <= '9' ? (uint8_t)(high - '0') :
                   (uint8_t)(high - 'a' + 10);
      low_value = low <= '9' ? (uint8_t)(low - '0') :
                  (uint8_t)(low - 'a' + 10);
      output[index] = (uint8_t)((high_value << 4) | low_value);
    }

  return 0;
}

static int bk7258_ota_catalog_u32(const cJSON *item, uint32_t *value)
{
  if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
      item->valuedouble > (double)UINT32_MAX ||
      item->valuedouble != (double)(uint32_t)item->valuedouble)
    {
      return -EINVAL;
    }

  *value = (uint32_t)item->valuedouble;
  return 0;
}

static int bk7258_ota_catalog_string(const cJSON *item, char *output,
                                     size_t size)
{
  size_t length;

  if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
      return -EINVAL;
    }

  length = strlen(item->valuestring);
  if (length == 0u || length >= size)
    {
      return -EINVAL;
    }

  memcpy(output, item->valuestring, length + 1u);
  return 0;
}

static int bk7258_ota_catalog_version_number(const char **cursor,
                                             uint32_t maximum,
                                             char delimiter,
                                             uint32_t *value)
{
  const char *text = *cursor;
  uint32_t result = 0u;

  if (*text < '0' || *text > '9')
    {
      return -EINVAL;
    }

  do
    {
      uint32_t digit = (uint32_t)(*text - '0');

      if (result > (maximum - digit) / 10u)
        {
          return -ERANGE;
        }
      result = result * 10u + digit;
      text++;
    }
  while (*text >= '0' && *text <= '9');

  if (*text != delimiter)
    {
      return -EINVAL;
    }

  *value = result;
  *cursor = delimiter == '\0' ? text : text + 1;
  return 0;
}

static int bk7258_ota_catalog_version(
  const char *text, struct bk7258_mcuboot_version_s *version)
{
  uint32_t major;
  uint32_t minor;
  uint32_t revision;
  uint32_t build;

  if (bk7258_ota_catalog_version_number(&text, UINT8_MAX, '.', &major) < 0 ||
      bk7258_ota_catalog_version_number(&text, UINT8_MAX, '.', &minor) < 0 ||
      bk7258_ota_catalog_version_number(&text, UINT16_MAX, '+',
                                        &revision) < 0 ||
      bk7258_ota_catalog_version_number(&text, UINT32_MAX, '\0', &build) < 0)
    {
      return -EINVAL;
    }

  version->major = (uint8_t)major;
  version->minor = (uint8_t)minor;
  version->revision = (uint16_t)revision;
  version->build = build;
  return 0;
}

static bool bk7258_ota_catalog_uri_safe(const char *uri)
{
  return uri[0] != '/' && strchr(uri, '\\') == NULL &&
         strstr(uri, "../") == NULL && strstr(uri, "/..") == NULL &&
         strcmp(uri, "..") != 0;
}

static int bk7258_ota_catalog_image(
  const cJSON *object, enum bk7258_ota_image_e image,
  struct bk7258_ota_catalog_s *result)
{
  static const char *const fields[] = {"uri", "size", "sha256"};
  uint32_t expected = image == BK7258_OTA_IMAGE_CP ?
                      BK7258_CP_RAW_PHYSICAL_SIZE :
                      BK7258_AP_RAW_PHYSICAL_SIZE;
  uint32_t size;
  int ret;

  if (!bk7258_ota_catalog_exact_object(object, fields,
                                        sizeof(fields) / sizeof(fields[0])))
    {
      return -EINVAL;
    }

  ret = bk7258_ota_catalog_string(
          cJSON_GetObjectItemCaseSensitive(object, "uri"),
          result->uri[image], sizeof(result->uri[image]));
  if (ret < 0 || !bk7258_ota_catalog_uri_safe(result->uri[image]))
    {
      return -EINVAL;
    }

  ret = bk7258_ota_catalog_u32(
          cJSON_GetObjectItemCaseSensitive(object, "size"), &size);
  if (ret < 0 || size != expected)
    {
      return -EINVAL;
    }
  result->manifest.image[image].physical_size = size;

  return bk7258_ota_catalog_hex(
    cJSON_GetObjectItemCaseSensitive(object, "sha256"),
    result->manifest.image[image].sha256, BK7258_OTA_SHA256_SIZE);
}

static int bk7258_ota_catalog_target(const cJSON *object)
{
  static const char *const fields[] =
  {
    "board_family", "physical_board"
  };
  const cJSON *item;

  if (!bk7258_ota_catalog_exact_object(
        object, fields, sizeof(fields) / sizeof(fields[0])))
    {
      return -EINVAL;
    }

  item = cJSON_GetObjectItemCaseSensitive(object, "board_family");
  if (!cJSON_IsString(item) || item->valuestring == NULL ||
      strcmp(item->valuestring, BK7258_OTA_BOARD_FAMILY) != 0)
    {
      return -EINVAL;
    }

  item = cJSON_GetObjectItemCaseSensitive(object, "physical_board");
  if (!cJSON_IsString(item) || item->valuestring == NULL ||
      strcmp(item->valuestring, BK7258_OTA_PHYSICAL_BOARD) != 0)
    {
      return -EINVAL;
    }

  return 0;
}

static int bk7258_ota_catalog_parse(
  const uint8_t *catalog, size_t catalog_size,
  struct bk7258_ota_catalog_s *result)
{
  static const char *const root_fields[] =
  {
    "format", "board_family", "target", "layout", "version",
    "security_counter", "cp", "ap", "package_id"
  };
  static const char *const layout_fields[] = {"identity", "sha256"};
  const cJSON *layout;
  const cJSON *item;
  cJSON *root;
  int ret = -EINVAL;

  root = cJSON_ParseWithLength((const char *)catalog, catalog_size);
  if (root == NULL ||
      !bk7258_ota_catalog_exact_object(
        root, root_fields, sizeof(root_fields) / sizeof(root_fields[0])))
    {
      goto out;
    }

  item = cJSON_GetObjectItemCaseSensitive(root, "format");
  if (!cJSON_IsString(item) || item->valuestring == NULL ||
      strcmp(item->valuestring, BK7258_OTA_CATALOG_FORMAT) != 0)
    {
      goto out;
    }
  item = cJSON_GetObjectItemCaseSensitive(root, "board_family");
  if (!cJSON_IsString(item) || item->valuestring == NULL ||
      strcmp(item->valuestring, BK7258_OTA_BOARD_FAMILY) != 0 ||
      bk7258_ota_catalog_target(
        cJSON_GetObjectItemCaseSensitive(root, "target")) < 0)
    {
      goto out;
    }

  layout = cJSON_GetObjectItemCaseSensitive(root, "layout");
  if (!bk7258_ota_catalog_exact_object(
        layout, layout_fields,
        sizeof(layout_fields) / sizeof(layout_fields[0])))
    {
      goto out;
    }
  item = cJSON_GetObjectItemCaseSensitive(layout, "identity");
  if (!cJSON_IsString(item) || item->valuestring == NULL ||
      strcmp(item->valuestring, BK7258_LAYOUT_ID) != 0 ||
      bk7258_ota_catalog_hex(
        cJSON_GetObjectItemCaseSensitive(layout, "sha256"),
        result->manifest.layout_sha256, BK7258_OTA_SHA256_SIZE) < 0 ||
      memcmp(result->manifest.layout_sha256, g_bk7258_ota_layout_sha256,
             sizeof(g_bk7258_ota_layout_sha256)) != 0)
    {
      goto out;
    }

  if (bk7258_ota_catalog_string(
        cJSON_GetObjectItemCaseSensitive(root, "version"),
        result->version, sizeof(result->version)) < 0 ||
      bk7258_ota_catalog_version(result->version,
                                 &result->manifest.image_version) < 0 ||
      bk7258_ota_catalog_u32(
        cJSON_GetObjectItemCaseSensitive(root, "security_counter"),
        &result->security_counter) < 0 || result->security_counter == 0u ||
      bk7258_ota_catalog_string(
        cJSON_GetObjectItemCaseSensitive(root, "package_id"),
        result->package_id, sizeof(result->package_id)) < 0 ||
      strlen(result->package_id) != BK7258_OTA_CATALOG_ID_SIZE - 1u ||
      bk7258_ota_catalog_hex(
        cJSON_GetObjectItemCaseSensitive(root, "package_id"),
        result->manifest.package_id, BK7258_OTA_PACKAGE_ID_SIZE) < 0 ||
      bk7258_ota_catalog_image(
        cJSON_GetObjectItemCaseSensitive(root, "cp"),
        BK7258_OTA_IMAGE_CP, result) < 0 ||
      bk7258_ota_catalog_image(
        cJSON_GetObjectItemCaseSensitive(root, "ap"),
        BK7258_OTA_IMAGE_AP, result) < 0)
    {
      goto out;
    }

  result->manifest.security_counter = result->security_counter;
  result->manifest.version = BK7258_OTA_MANIFEST_VERSION;
  ret = 0;

out:
  cJSON_Delete(root);
  return ret;
}

int bk7258_ota_catalog_verify(const uint8_t *catalog, size_t catalog_size,
                              const uint8_t *signature,
                              size_t signature_size,
                              struct bk7258_ota_catalog_s *result)
{
  mbedtls_pk_context key;
  uint8_t digest[32];
  int ret;

  if (catalog == NULL || signature == NULL || result == NULL ||
      catalog_size == 0u || catalog_size > BK7258_OTA_CATALOG_MAX_SIZE ||
      signature_size < 8u ||
      signature_size > BK7258_OTA_CATALOG_MAX_SIGNATURE)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  mbedtls_pk_init(&key);
  ret = mbedtls_pk_parse_public_key(
          &key, bk7258_ota_catalog_public_key_der,
          bk7258_ota_catalog_public_key_der_size);
  if (ret == 0)
    {
      ret = mbedtls_sha256(catalog, catalog_size, digest, 0);
    }
  if (ret == 0)
    {
      ret = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest,
                              sizeof(digest), signature, signature_size);
    }
  mbedtls_pk_free(&key);
  if (ret != 0)
    {
      return -EKEYREJECTED;
    }

  return bk7258_ota_catalog_parse(catalog, catalog_size, result);
}

#endif /* CONFIG_BK7258_OTA_MANAGER */
