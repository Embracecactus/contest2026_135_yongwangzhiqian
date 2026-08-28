/****************************************************************************
 * chips/bk7258/include/bk7258_ota_catalog.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_CATALOG_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_CATALOG_H

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/chip/bk7258_ota.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_OTA_CATALOG_MAX_SIZE       2048u
#define BK7258_OTA_CATALOG_MAX_SIGNATURE  80u
#define BK7258_OTA_CATALOG_ID_SIZE        65u
#define BK7258_OTA_CATALOG_VERSION_SIZE   32u
#define BK7258_OTA_CATALOG_URI_SIZE       192u

struct bk7258_ota_catalog_s
{
  struct bk7258_ota_manifest_s manifest;
  char package_id[BK7258_OTA_CATALOG_ID_SIZE];
  char version[BK7258_OTA_CATALOG_VERSION_SIZE];
  uint32_t security_counter;
  char uri[2][BK7258_OTA_CATALOG_URI_SIZE];
};

#ifdef CONFIG_BK7258_OTA_MANAGER
int bk7258_ota_catalog_verify(const uint8_t *catalog, size_t catalog_size,
                              const uint8_t *signature,
                              size_t signature_size,
                              struct bk7258_ota_catalog_s *result);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_CATALOG_H */
