/*
 * Minimal host-side TinyCrypt API surface used by boot_bl1_manifest.c.
 *
 * The real verifier is not available on the host; tests control the
 * outcome through g_uECC_verify_result and capture the arguments.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BK7258_TESTS_TINYCRYPT_ECC_H
#define BK7258_TESTS_TINYCRYPT_ECC_H

#include <stdint.h>

typedef struct uECC_Curve_t *uECC_Curve;

int uECC_verify(const uint8_t *public_key, const uint8_t *message_hash,
                unsigned hash_size, const uint8_t *signature,
                uECC_Curve curve);

uECC_Curve uECC_secp256r1(void);

#endif /* BK7258_TESTS_TINYCRYPT_ECC_H */