/****************************************************************************
 * chips/bk7258/include/bk7258_mcuboot_format.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-neutral MCUboot wire-format and trailer geometry used by the BK7258
 * BL2 pair gate and the runtime OTA admission preflight.  This is not a
 * second signature or boot-state authority; MCUboot remains authoritative.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_MCUBOOT_FORMAT_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_MCUBOOT_FORMAT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#  define BK7258_MCUBOOT_STATIC_ASSERT static_assert
#else
#  define BK7258_MCUBOOT_STATIC_ASSERT _Static_assert
#endif

#define BK7258_MCUBOOT_IMAGE_MAGIC           0x96f3b83du
#define BK7258_MCUBOOT_IMAGE_HEADER_SIZE     32u
#define BK7258_MCUBOOT_TLV_INFO_MAGIC        0x6907u
#define BK7258_MCUBOOT_TLV_PROT_INFO_MAGIC   0x6908u
#define BK7258_MCUBOOT_TLV_SECURITY_COUNTER  0x0050u

#define BK7258_MCUBOOT_TRAILER_MAGIC_SIZE    16u
#define BK7258_MCUBOOT_TRAILER_ALIGN         8u
#define BK7258_MCUBOOT_TRAILER_MAGIC_INIT                         \
  {                                                              \
    0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f,              \
    0x35, 0x52, 0x50, 0x0f, 0x2c, 0xb6, 0x79, 0x80               \
  }

#define BK7258_MCUBOOT_COPY_DONE_OFFSET(size)                     \
  ((size) - BK7258_MCUBOOT_TRAILER_MAGIC_SIZE -                  \
   2u * BK7258_MCUBOOT_TRAILER_ALIGN)
#define BK7258_MCUBOOT_IMAGE_OK_OFFSET(size)                       \
  ((size) - BK7258_MCUBOOT_TRAILER_MAGIC_SIZE -                  \
   BK7258_MCUBOOT_TRAILER_ALIGN)

struct bk7258_mcuboot_version_s
{
  uint8_t major;
  uint8_t minor;
  uint16_t revision;
  uint32_t build;
} __attribute__((packed));

struct bk7258_mcuboot_image_header_s
{
  uint32_t magic;
  uint32_t load_address;
  uint16_t header_size;
  uint16_t protected_tlv_size;
  uint32_t image_size;
  uint32_t flags;
  struct bk7258_mcuboot_version_s version;
  uint32_t reserved;
} __attribute__((packed));

struct bk7258_mcuboot_tlv_info_s
{
  uint16_t magic;
  uint16_t total;
} __attribute__((packed));

struct bk7258_mcuboot_tlv_s
{
  uint16_t type;
  uint16_t length;
} __attribute__((packed));

BK7258_MCUBOOT_STATIC_ASSERT(sizeof(struct bk7258_mcuboot_version_s) == 8u,
                            "BK7258 MCUboot version format changed");
BK7258_MCUBOOT_STATIC_ASSERT(sizeof(struct bk7258_mcuboot_image_header_s) ==
                            BK7258_MCUBOOT_IMAGE_HEADER_SIZE,
                            "BK7258 MCUboot header format changed");
BK7258_MCUBOOT_STATIC_ASSERT(sizeof(struct bk7258_mcuboot_tlv_info_s) == 4u,
                            "BK7258 MCUboot TLV info format changed");
BK7258_MCUBOOT_STATIC_ASSERT(sizeof(struct bk7258_mcuboot_tlv_s) == 4u,
                            "BK7258 MCUboot TLV format changed");

#undef BK7258_MCUBOOT_STATIC_ASSERT

static inline bool bk7258_mcuboot_version_equal(
  const struct bk7258_mcuboot_version_s *left,
  const struct bk7258_mcuboot_version_s *right)
{
  return left->major == right->major && left->minor == right->minor &&
         left->revision == right->revision && left->build == right->build;
}

static inline int bk7258_mcuboot_version_compare(
  const struct bk7258_mcuboot_version_s *left,
  const struct bk7258_mcuboot_version_s *right)
{
  if (left->major != right->major)
    {
      return left->major > right->major ? 1 : -1;
    }

  if (left->minor != right->minor)
    {
      return left->minor > right->minor ? 1 : -1;
    }

  if (left->revision != right->revision)
    {
      return left->revision > right->revision ? 1 : -1;
    }

  if (left->build != right->build)
    {
      return left->build > right->build ? 1 : -1;
    }

  return 0;
}

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_MCUBOOT_FORMAT_H */
