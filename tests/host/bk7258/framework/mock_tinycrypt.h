/*
 * mock_tinycrypt.h - host-side TinyCrypt mock controls for tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BK7258_TESTS_MOCK_TINYCRYPT_H
#define BK7258_TESTS_MOCK_TINYCRYPT_H

#include <stdint.h>
#include <stddef.h>

extern int g_uECC_verify_result;
extern unsigned g_uECC_verify_calls;

void mock_tinycrypt_reset(void);
unsigned mock_tinycrypt_verify_calls(void);
unsigned mock_tinycrypt_last_hash_size(void);
int mock_tinycrypt_verify_pubkey_is(const uint8_t *expected, unsigned size);

#endif /* BK7258_TESTS_MOCK_TINYCRYPT_H */