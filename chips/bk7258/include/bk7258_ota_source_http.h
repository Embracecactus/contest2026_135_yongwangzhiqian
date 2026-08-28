/****************************************************************************
 * chips/bk7258/include/bk7258_ota_source_http.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_HTTP_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_HTTP_H

#include <nuttx/config.h>

#include <arch/chip/bk7258_ota.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct bk7258_ota_http_source_s
{
  void *priv;
};

#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP
int bk7258_ota_http_source_initialize(
  struct bk7258_ota_http_source_s *source, const char *catalog_url,
  const char *ca_path);
const struct bk7258_ota_source_ops_s *bk7258_ota_http_source_ops(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_HTTP_H */
