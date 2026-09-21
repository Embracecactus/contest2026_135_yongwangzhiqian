/****************************************************************************
 * app/bk7258/bk7258_provision_generate.c
 * SPDX-License-Identifier: Apache-2.0
 * Native device identity. Entropy comes from the configured hardware poll.
 ****************************************************************************/
#include <nuttx/config.h>
#ifdef CONFIG_BK7258_PROVISION_NATIVE
#include "bk7258_provision_identity.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/platform_util.h>

int bkprov_identity_generate(unsigned char **record, size_t *size)
{
  if (!record || !size) return -EINVAL;
  *record = NULL;
  *size = 0;
  unsigned char *scratch = calloc(1, 8192);
  if (!scratch) return -ENOMEM;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_pk_context key;
  mbedtls_x509write_cert cert;
  mbedtls_mpi serial;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  mbedtls_pk_init(&key);
  mbedtls_x509write_crt_init(&cert);
  mbedtls_mpi_init(&serial);
  static const unsigned char purpose[] = "shaniu-native-identity-v1";
  int ret = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                                  purpose, sizeof(purpose) - 1);
  if (!ret) ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (!ret) ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                     mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &random);
  if (!ret) ret = mbedtls_ctr_drbg_random(&random, scratch, 16);
  scratch[0] = (scratch[0] & 0x7f) | 1;
  if (!ret) ret = mbedtls_mpi_read_binary(&serial, scratch, 16);
  if (!ret) ret = mbedtls_x509write_crt_set_serial(&cert, &serial);
  mbedtls_x509write_crt_set_version(&cert, MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&cert, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&cert, &key);
  mbedtls_x509write_crt_set_issuer_key(&cert, &key);
  if (!ret) ret = mbedtls_x509write_crt_set_subject_name(&cert, "CN=shaniu-device");
  if (!ret) ret = mbedtls_x509write_crt_set_issuer_name(&cert, "CN=shaniu-device");
  /* A signed firmware policy, not an unset device RTC, owns this template.
   * The phone still enforces validity and the physically scanned exact pin.
   * Neither this template nor the self-signed leaf certifies manufacturer identity.
   */
  if (!ret) ret = mbedtls_x509write_crt_set_validity(&cert,
      CONFIG_BK7258_PROVISION_CERT_NOT_BEFORE, CONFIG_BK7258_PROVISION_CERT_NOT_AFTER);
  if (!ret) ret = mbedtls_x509write_crt_set_basic_constraints(&cert, 0, -1);
  if (!ret) ret = mbedtls_x509write_crt_set_key_usage(&cert, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
  unsigned char eku[32];
  unsigned char *end = eku + sizeof(eku);
  size_t length = 0;
  if (!ret)
    {
      int n = mbedtls_asn1_write_oid(&end, eku, MBEDTLS_OID_CLIENT_AUTH,
                                     MBEDTLS_OID_SIZE(MBEDTLS_OID_CLIENT_AUTH));
      if (n < 0) ret = n; else length += n;
    }
  if (!ret)
    {
      int n = mbedtls_asn1_write_oid(&end, eku, MBEDTLS_OID_SERVER_AUTH,
                                     MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH));
      if (n < 0) ret = n; else length += n;
    }
  if (!ret)
    {
      int n = mbedtls_asn1_write_len(&end, eku, length);
      if (n < 0) ret = n; else length += n;
    }
  if (!ret)
    {
      int n = mbedtls_asn1_write_tag(&end, eku, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
      if (n < 0) ret = n; else length += n;
    }
  if (!ret) ret = mbedtls_x509write_crt_set_extension(&cert,
      MBEDTLS_OID_EXTENDED_KEY_USAGE, MBEDTLS_OID_SIZE(MBEDTLS_OID_EXTENDED_KEY_USAGE),
      0, end, length);
  int cert_size = 0;
  int key_size = 0;
  if (!ret)
    {
      cert_size = mbedtls_x509write_crt_der(&cert, scratch, 4096,
                                            mbedtls_ctr_drbg_random, &random);
      if (cert_size <= 0) ret = cert_size ? cert_size : -EIO;
    }
  if (!ret)
    {
      key_size = mbedtls_pk_write_key_der(&key, scratch + 4096, 4096);
      if (key_size <= 0) ret = key_size ? key_size : -EIO;
    }
  if (!ret)
    {
      *size = 48u + cert_size + key_size;
      *record = calloc(1, *size);
      if (!*record) ret = -ENOMEM;
      else
        {
          unsigned char *p = *record;
          memcpy(p, "BPI2", 4);
          p[5] = 1;
          p[8] = cert_size >> 8; p[9] = cert_size;
          p[10] = key_size >> 8; p[11] = key_size;
          ret = mbedtls_ctr_drbg_random(&random, p + 16, 32);
          memcpy(p + 48, scratch + 4096 - cert_size, cert_size);
          memcpy(p + 48 + cert_size, scratch + 8192 - key_size, key_size);
        }
    }
  mbedtls_mpi_free(&serial);
  mbedtls_x509write_crt_free(&cert);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  mbedtls_platform_zeroize(scratch, 8192);
  free(scratch);
  if (ret && *record)
    {
      mbedtls_platform_zeroize(*record, *size);
      free(*record);
      *record = NULL;
    }
  if (ret) *size = 0;
  return ret;
}
#endif
