/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_ota_source_file.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_FILE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_FILE_H

#include <nuttx/config.h>

#include <stdbool.h>

#include <arch/chip/bk7258_ota.h>
#include <arch/chip/bk7258_ota_catalog.h>

#define BK7258_OTA_FILE_ROOT_SIZE 128u
#define BK7258_OTA_FILE_PATH_SIZE \
  (BK7258_OTA_FILE_ROOT_SIZE + BK7258_OTA_CATALOG_URI_SIZE)

struct bk7258_ota_file_source_s
{
  char root[BK7258_OTA_FILE_ROOT_SIZE];
  char path[2][BK7258_OTA_FILE_PATH_SIZE];
  int fd[2];
  volatile bool canceled;
  struct bk7258_ota_catalog_s catalog;
};

#ifdef CONFIG_BK7258_OTA_SOURCE_FILE
int bk7258_ota_file_source_initialize(
  struct bk7258_ota_file_source_s *source, const char *root);
const struct bk7258_ota_source_ops_s *bk7258_ota_file_source_ops(void);
#endif

#endif
