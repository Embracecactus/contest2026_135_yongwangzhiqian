/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __CHIPS_BK7258_CP_BK7258_OTA_IMAGE_H
#define __CHIPS_BK7258_CP_BK7258_OTA_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/chip/bk7258_ota.h>

struct bk7258_ota_image_metadata_s
{
  struct bk7258_mcuboot_version_s version;
  uint32_t security_counter;
  bool security_counter_present;
};

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_ota_source_image_metadata(
  FAR const struct bk7258_ota_source_ops_s *ops, FAR void *context,
  enum bk7258_ota_image_e image, uint32_t physical_size,
  uint32_t logical_size,
  FAR struct bk7258_ota_image_metadata_s *metadata);

int bk7258_ota_xip_image_metadata(
  uint32_t xip, uint32_t logical_size,
  FAR struct bk7258_ota_image_metadata_s *metadata);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_CP_BK7258_OTA_IMAGE_H */
