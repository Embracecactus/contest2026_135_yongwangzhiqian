/****************************************************************************
 * chips/bk7258/ap/bk7258_active_image.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_OTA_MANAGER

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/chip/bk7258_active_image.h>
#include <arch/chip/bk7258_image_layout.h>

int bk7258_active_ap_image_version(
  struct bk7258_mcuboot_version_s *version)
{
  const volatile struct bk7258_mcuboot_image_header_s *mapped =
    (const volatile struct bk7258_mcuboot_image_header_s *)
      (uintptr_t)BK7258_AP_FLASH_ADDR;
  struct bk7258_mcuboot_image_header_s header;
  volatile const uint8_t *source = (volatile const uint8_t *)mapped;
  uint8_t *destination = (uint8_t *)&header;
  size_t index;

  if (version == NULL)
    {
      return -EINVAL;
    }

  /* Copy once from immutable XIP so validation observes one local snapshot. */

  for (index = 0; index < sizeof(header); index++)
    {
      destination[index] = source[index];
    }

  if (header.magic != BK7258_MCUBOOT_IMAGE_MAGIC ||
      header.header_size < BK7258_MCUBOOT_IMAGE_HEADER_SIZE ||
      header.image_size < 8u ||
      header.image_size > BK7258_AP_FLASH_SIZE ||
      header.header_size > BK7258_AP_FLASH_SIZE - header.image_size)
    {
      return -EILSEQ;
    }

  *version = header.version;
  return 0;
}

#endif /* CONFIG_BK7258_OTA_MANAGER */
