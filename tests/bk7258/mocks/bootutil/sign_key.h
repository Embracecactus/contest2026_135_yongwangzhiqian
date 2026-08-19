/*
 * mock bootutil/sign_key.h - host mock of the MCUboot bootutil_key hook.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MOCK_BOOTUTIL_SIGN_KEY_H
#define MOCK_BOOTUTIL_SIGN_KEY_H

#include <stdint.h>

struct bootutil_key
{
  const unsigned char *key;
  const unsigned int *len;
};

#endif /* MOCK_BOOTUTIL_SIGN_KEY_H */
