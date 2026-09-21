/* SPDX-License-Identifier: Apache-2.0
 *
 * Device-owned identity generation: one EC P-256 key, one self-signed leaf and
 * the existing BPI1 record.
 */
#include "bk7258_provision_keygen.h"

#include <errno.h>
#include <string.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>

#if defined(MBEDTLS_X509_CRT_WRITE_C) && defined(MBEDTLS_PK_WRITE_C) && \
    defined(MBEDTLS_ECP_C)

#define BKPROV_KEYGEN_CERT_MAX 4096u
#define BKPROV_KEYGEN_KEY_MAX  4096u

static int keygen_serial(mbedtls_ctr_drbg_context *random, uint8_t serial[16])
{
  int ret = mbedtls_ctr_drbg_random(random, serial, 16u);

  if (ret != 0)
    {
      return ret;
    }

  /* A zero or negative serial is not a legal DER integer value. */
  serial[0] = (uint8_t)((serial[0] & 0x7fu) | 0x40u);
  return 0;
}

int bkprov_identity_generate(void *record, size_t capacity, size_t *size)
{
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_pk_context key;
  mbedtls_x509write_cert certificate;
  mbedtls_asn1_sequence usage;
  mbedtls_asn1_buf server_auth;
  mbedtls_asn1_buf client_auth;
  uint8_t secret[32];
  uint8_t serial[16];
  uint8_t certificate_der[BKPROV_KEYGEN_CERT_MAX];
  uint8_t key_der[BKPROV_KEYGEN_KEY_MAX + 64u];
  uint8_t *output = record;
  size_t certificate_size;
  size_t key_size;
  int ret;

  static const unsigned char purpose[] = "shaniu-device-identity";

  if (record == NULL || size == NULL || capacity < 64u)
    {
      return -EINVAL;
    }

  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  mbedtls_pk_init(&key);
  mbedtls_x509write_crt_init(&certificate);

  ret = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                              purpose, sizeof(purpose) - 1u);
  if (ret != 0)
    {
      /* No entropy, no identity. A predictable key must never be generated,
       * so this is an error rather than a fallback.
       */
      ret = -EIO;
      goto out;
    }

  ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (ret != 0)
    {
      ret = -ENOSYS;
      goto out;
    }

  ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                            mbedtls_ctr_drbg_random, &random);
  if (ret != 0)
    {
      ret = -EIO;
      goto out;
    }

  ret = keygen_serial(&random, serial);
  if (ret != 0)
    {
      ret = -EIO;
      goto out;
    }

  /* The record's possession secret is device-owned and never published: the
   * claim code carries a per-window secret, and the legacy activation-file
   * path simply cannot exist for a device that never exported this one.
   */
  ret = mbedtls_ctr_drbg_random(&random, secret, sizeof(secret));
  if (ret != 0)
    {
      ret = -EIO;
      goto out;
    }

  ret = mbedtls_x509write_crt_set_subject_name(&certificate,
                                               "CN=shaniu-device");
  if (ret == 0)
    {
      ret = mbedtls_x509write_crt_set_issuer_name(&certificate,
                                                  "CN=shaniu-device");
    }

  if (ret != 0)
    {
      ret = -EINVAL;
      goto out;
    }

  mbedtls_x509write_crt_set_version(&certificate, MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&certificate, &key);
  mbedtls_x509write_crt_set_issuer_key(&certificate, &key);

  ret = mbedtls_x509write_crt_set_serial_raw(&certificate, serial,
                                             sizeof(serial));
  if (ret == 0)
    {
      ret = mbedtls_x509write_crt_set_validity(&certificate,
                                               BKPROV_IDENTITY_NOT_BEFORE,
                                               BKPROV_IDENTITY_NOT_AFTER);
    }

  if (ret == 0)
    {
      /* Not a CA, and the leaf may only sign: exactly what the identity loader
       * and the pinned provisioning trust manager require.
       */
      ret = mbedtls_x509write_crt_set_basic_constraints(&certificate, 0, 0);
    }

  if (ret == 0)
    {
      ret = mbedtls_x509write_crt_set_key_usage(&certificate,
                                                MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    }

  if (ret == 0)
    {
      server_auth.tag = MBEDTLS_ASN1_OID;
      server_auth.p = (unsigned char *)MBEDTLS_OID_SERVER_AUTH;
      server_auth.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
      client_auth.tag = MBEDTLS_ASN1_OID;
      client_auth.p = (unsigned char *)MBEDTLS_OID_CLIENT_AUTH;
      client_auth.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_CLIENT_AUTH);
      usage.buf = server_auth;
      usage.next = NULL;

      /* mbedtls consumes a linked list; the client entry is chained here so the
       * leaf carries both purposes the claim channel and Gateway mTLS need.
       */
      mbedtls_asn1_sequence client;
      client.buf = client_auth;
      client.next = NULL;
      usage.next = &client;

      ret = mbedtls_x509write_crt_set_ext_key_usage(&certificate, &usage);
    }

  if (ret != 0)
    {
      ret = -EINVAL;
      goto out;
    }

  ret = mbedtls_x509write_crt_der(&certificate, certificate_der,
                                  sizeof(certificate_der),
                                  mbedtls_ctr_drbg_random, &random);
  if (ret < 0)
    {
      ret = -ENOSPC;
      goto out;
    }

  certificate_size = (size_t)ret;
  ret = mbedtls_pk_write_key_der(&key, key_der, sizeof(key_der));
  if (ret < 0)
    {
      ret = -ENOSPC;
      goto out;
    }

  key_size = (size_t)ret;
  if (certificate_size > 4096u || key_size > 4096u ||
      48u + certificate_size + key_size > capacity)
    {
      ret = -ENOSPC;
      goto out;
    }

  memset(output, 0, 48u);
  memcpy(output, "BPI1", 4u);
  output[5] = 1u;
  output[8] = (uint8_t)(certificate_size >> 8);
  output[9] = (uint8_t)certificate_size;
  output[10] = (uint8_t)(key_size >> 8);
  output[11] = (uint8_t)key_size;
  memcpy(output + 16, secret, sizeof(secret));
  memcpy(output + 48u, certificate_der, certificate_size);
  /* mbedtls writes the key DER at the end of the buffer. */
  memcpy(output + 48u + certificate_size, key_der + sizeof(key_der) - key_size,
         key_size);
  *size = 48u + certificate_size + key_size;
  ret = 0;

out:
  mbedtls_platform_zeroize(secret, sizeof(secret));
  mbedtls_platform_zeroize(serial, sizeof(serial));
  mbedtls_platform_zeroize(certificate_der, sizeof(certificate_der));
  mbedtls_platform_zeroize(key_der, sizeof(key_der));
  mbedtls_x509write_crt_free(&certificate);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  return ret;
}

#else /* key or certificate writer is disabled in this build */

int bkprov_identity_generate(void *record, size_t capacity, size_t *size)
{
  (void)record;
  (void)capacity;
  (void)size;
  return -ENOSYS;
}

#endif
