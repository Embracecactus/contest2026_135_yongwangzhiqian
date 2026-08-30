/*
 * mock_tinycrypt.c - host-side TinyCrypt mock (verify result injection).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <string.h>

#include <tinycrypt/ecc.h>

int g_uECC_verify_result = 1;
unsigned g_uECC_verify_calls = 0;
static uint8_t g_last_public_key[65];
static uint8_t g_last_signature[64];
static unsigned g_last_hash_size = 0;

uECC_Curve uECC_secp256r1(void)
{
  return (uECC_Curve)(uintptr_t)0x1u;
}

int uECC_verify(const uint8_t *public_key, const uint8_t *message_hash,
                unsigned hash_size, const uint8_t *signature,
                uECC_Curve curve)
{
  (void)message_hash;
  (void)curve;

  g_uECC_verify_calls++;
  g_last_hash_size = hash_size;
  if (public_key != NULL && signature != NULL)
    {
      uint8_t key_size = hash_size >= 32u ? 64u : 65u;

      memset(g_last_public_key, 0, sizeof(g_last_public_key));
      memcpy(g_last_public_key, public_key, key_size);
      memset(g_last_signature, 0, sizeof(g_last_signature));
      memcpy(g_last_signature, signature, hash_size >= 32u ? 64u : 64u);
    }

  return g_uECC_verify_result;
}

/* Exposed only to the test binary through the header below. */
void mock_tinycrypt_reset(void)
{
  g_uECC_verify_result = 1;
  g_uECC_verify_calls = 0;
  g_last_hash_size = 0;
}

unsigned mock_tinycrypt_verify_calls(void)
{
  return g_uECC_verify_calls;
}

unsigned mock_tinycrypt_last_hash_size(void)
{
  return g_last_hash_size;
}

int mock_tinycrypt_verify_pubkey_is(const uint8_t *expected, unsigned size)
{
  return size <= 65u && memcmp(g_last_public_key, expected, size) == 0;
}