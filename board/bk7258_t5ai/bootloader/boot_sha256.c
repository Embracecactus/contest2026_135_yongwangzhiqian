/*
 * boot_sha256.c - compact freestanding SHA-256 implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boot_sha256.h"

#define ROTR(value, bits) (((value) >> (bits)) | ((value) << (32u - (bits))))

static const uint32_t g_sha256_round[64] =
{
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
  0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
  0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
  0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
  0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
  0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
  0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
  0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
  0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
  0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
  0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
  0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
  0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
  0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t load_be32(const uint8_t *value)
{
  return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
         ((uint32_t)value[2] << 8) | value[3];
}

static void store_be32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)(value >> 24);
  destination[1] = (uint8_t)(value >> 16);
  destination[2] = (uint8_t)(value >> 8);
  destination[3] = (uint8_t)value;
}

static void sha256_transform(struct boot_sha256_context_s *context,
                             const uint8_t block[64])
{
  uint32_t schedule[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  uint32_t index;

  for (index = 0; index < 16; index++)
    {
      schedule[index] = load_be32(block + index * 4u);
    }

  for (; index < 64; index++)
    {
      uint32_t x = schedule[index - 15u];
      uint32_t y = schedule[index - 2u];
      uint32_t s0 = ROTR(x, 7) ^ ROTR(x, 18) ^ (x >> 3);
      uint32_t s1 = ROTR(y, 17) ^ ROTR(y, 19) ^ (y >> 10);

      schedule[index] = schedule[index - 16u] + s0 +
                        schedule[index - 7u] + s1;
    }

  a = context->state[0];
  b = context->state[1];
  c = context->state[2];
  d = context->state[3];
  e = context->state[4];
  f = context->state[5];
  g = context->state[6];
  h = context->state[7];

  for (index = 0; index < 64; index++)
    {
      uint32_t sum1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
      uint32_t choice = (e & f) ^ (~e & g);
      uint32_t temporary1 = h + sum1 + choice + g_sha256_round[index] +
                            schedule[index];
      uint32_t sum0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
      uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temporary2 = sum0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }

  context->state[0] += a;
  context->state[1] += b;
  context->state[2] += c;
  context->state[3] += d;
  context->state[4] += e;
  context->state[5] += f;
  context->state[6] += g;
  context->state[7] += h;
}

void boot_sha256_init(void *opaque)
{
  struct boot_sha256_context_s *context = opaque;

  context->state[0] = 0x6a09e667u;
  context->state[1] = 0xbb67ae85u;
  context->state[2] = 0x3c6ef372u;
  context->state[3] = 0xa54ff53au;
  context->state[4] = 0x510e527fu;
  context->state[5] = 0x9b05688cu;
  context->state[6] = 0x1f83d9abu;
  context->state[7] = 0x5be0cd19u;
  context->total = 0;
  context->used = 0;
}

void boot_sha256_update(void *opaque, const uint8_t *data, size_t len)
{
  struct boot_sha256_context_s *context = opaque;

  context->total += len;
  while (len != 0)
    {
      size_t available = 64u - context->used;
      size_t count = len < available ? len : available;
      size_t index;

      for (index = 0; index < count; index++)
        {
          context->block[context->used + index] = data[index];
        }

      context->used += (uint32_t)count;
      data += count;
      len -= count;
      if (context->used == 64u)
        {
          sha256_transform(context, context->block);
          context->used = 0;
        }
    }
}

void boot_sha256_final(void *opaque, uint8_t digest[32])
{
  struct boot_sha256_context_s *context = opaque;
  uint64_t bits = context->total << 3;
  uint32_t index;

  context->block[context->used++] = 0x80u;
  if (context->used > 56u)
    {
      while (context->used < 64u)
        {
          context->block[context->used++] = 0;
        }

      sha256_transform(context, context->block);
      context->used = 0;
    }

  while (context->used < 56u)
    {
      context->block[context->used++] = 0;
    }

  for (index = 0; index < 8; index++)
    {
      context->block[63u - index] = (uint8_t)(bits >> (index * 8u));
    }

  sha256_transform(context, context->block);
  for (index = 0; index < 8; index++)
    {
      store_be32(digest + index * 4u, context->state[index]);
    }

  context->used = 0;
  context->total = 0;
}
