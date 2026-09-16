/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_IDENTITY_H
#define __APP_BK7258_PROVISION_IDENTITY_H
#include <stddef.h>
#include <stdint.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>

/* BPI1: magic4, version:be16=1, reserved:be16=0, cert_len:be16,
 * key_len:be16, reserved:be32=0, secret32, leaf DER, private-key DER.
 * Supplied only through the trusted operator channel, never BLE.
 * Zero initialize. The object cannot be moved while its contexts are lent.
 */
struct bkprov_identity_s
{
  mbedtls_x509_crt certificate;
  mbedtls_pk_context key;
  uint8_t secret[32];
  uint8_t *record;
  size_t size;
  size_t certificate_size;
  size_t key_size;
};
int bkprov_identity_load(struct bkprov_identity_s *identity,
                          const void *record, size_t size);
void bkprov_identity_clear(struct bkprov_identity_s *identity);
#endif
