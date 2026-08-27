/* Minimal MCUboot image-version ABI used by the BL2 pair-policy fixture. */
#ifndef BK7258_TESTS_MOCK_BOOTUTIL_IMAGE_H
#define BK7258_TESTS_MOCK_BOOTUTIL_IMAGE_H

#include <stdint.h>

struct image_version
{
  uint8_t iv_major;
  uint8_t iv_minor;
  uint16_t iv_revision;
  uint32_t iv_build_num;
};

#endif /* BK7258_TESTS_MOCK_BOOTUTIL_IMAGE_H */
