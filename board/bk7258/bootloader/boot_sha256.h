/*
 * boot_sha256.h - freestanding SHA-256 used by BL1 authorization.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_SHA256_H
#define BK7258_BOOT_SHA256_H

#include <stddef.h>
#include <stdint.h>

struct boot_sha256_context_s
{
  uint32_t state[8];
  uint64_t total;
  uint8_t block[64];
  uint32_t used;
};

void boot_sha256_init(void *context);
void boot_sha256_update(void *context, const uint8_t *data, size_t len);
void boot_sha256_final(void *context, uint8_t digest[32]);

#endif /* BK7258_BOOT_SHA256_H */
