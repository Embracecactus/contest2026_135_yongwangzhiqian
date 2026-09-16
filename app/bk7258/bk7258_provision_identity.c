/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_identity.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/platform_util.h>

void bkprov_identity_clear(struct bkprov_identity_s *identity)
{
  if (identity == NULL) return;
  mbedtls_x509_crt_free(&identity->certificate);
  mbedtls_pk_free(&identity->key);
  if (identity->record != NULL)
    {
      mbedtls_platform_zeroize(identity->record, identity->size);
      free(identity->record);
    }
  mbedtls_platform_zeroize(identity, sizeof(*identity));
}

int bkprov_identity_load(struct bkprov_identity_s *identity,
                          const void *record, size_t size)
{
  const uint8_t *p = record;
  static const struct bkprov_identity_s empty;
  if (identity == NULL || p == NULL || size < 48 || size > 8192)
    return -EINVAL;
  if (memcmp(identity, &empty, sizeof(empty)) != 0) return -EBUSY;
  if (memcmp(p, "BPI1", 4) || p[4] || p[5] != 1 || p[6] || p[7] ||
      p[12] || p[13] || p[14] || p[15]) return -EBADMSG;
  size_t certificate_size = ((size_t)p[8] << 8) | p[9];
  size_t key_size = ((size_t)p[10] << 8) | p[11];
  if (certificate_size == 0 || key_size == 0 || certificate_size > 4096 ||
      key_size > 4096 || 48 + certificate_size + key_size != size)
    return -EBADMSG;
  unsigned nonzero = 0;
  for (size_t i = 16; i < 48; i++) nonzero |= p[i];
  if (!nonzero) return -EINVAL;

  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  static const unsigned char purpose[] = "shaniu-device-identity";
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  mbedtls_x509_crt_init(&identity->certificate);
  mbedtls_pk_init(&identity->key);
  int ret = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                                  purpose, sizeof(purpose) - 1);
  if (ret == 0)
    ret = mbedtls_x509_crt_parse_der(&identity->certificate, p + 48, certificate_size);
  if (ret == 0 && identity->certificate.raw.len != certificate_size)
    ret = -EBADMSG;
  if (ret == 0)
    ret = mbedtls_pk_parse_key(&identity->key, p + 48 + certificate_size, key_size,
                                NULL, 0, mbedtls_ctr_drbg_random, &random);
  if (ret == 0)
    ret = mbedtls_pk_check_pair(&identity->certificate.pk, &identity->key,
                                 mbedtls_ctr_drbg_random, &random);
  /* A dual-purpose leaf is needed for pinned BLE TLS and Gateway mTLS.
   * Require explicit EKU, rather than treating absent EKU as unrestricted.
   */
  if (ret == 0 && (!mbedtls_pk_can_do(&identity->key, MBEDTLS_PK_ECDSA) ||
      identity->certificate.MBEDTLS_PRIVATE(ca_istrue) ||
      !(identity->certificate.MBEDTLS_PRIVATE(ext_types) & MBEDTLS_X509_EXT_EXTENDED_KEY_USAGE)))
    ret = -EPROTO;
  if (ret == 0)
    ret = mbedtls_x509_crt_check_extended_key_usage(&identity->certificate,
                  MBEDTLS_OID_SERVER_AUTH, MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH));
  if (ret == 0)
    ret = mbedtls_x509_crt_check_extended_key_usage(&identity->certificate,
                  MBEDTLS_OID_CLIENT_AUTH, MBEDTLS_OID_SIZE(MBEDTLS_OID_CLIENT_AUTH));
  if (ret == 0)
    ret = mbedtls_x509_crt_check_key_usage(&identity->certificate,
                                          MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
  if (ret == 0)
    {
      identity->record = malloc(size);
      if (identity->record == NULL) ret = -ENOMEM;
      else
        {
          memcpy(identity->record, p, size);
          memcpy(identity->secret, p + 16, 32);
          identity->size = size;
          identity->certificate_size = certificate_size;
          identity->key_size = key_size;
        }
    }
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  if (ret < 0) bkprov_identity_clear(identity);
  return ret;
}
