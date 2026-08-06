/* Host/OpenSSL adapter for the portable N17 Manifest C parser. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>

#include "boot_n17_manifest_core.h"

struct host_context_s
{
  const uint8_t *public_xy;
};

static int host_sha256(void *arg, const uint8_t *data, size_t len,
                       uint8_t digest[32])
{
  unsigned int digest_len = 0;

  (void)arg;
  if (EVP_Digest(data, len, digest, &digest_len, EVP_sha256(), NULL) != 1 ||
      digest_len != 32)
    {
      return -1;
    }

  return 0;
}

static int host_verify_signature(void *arg, uint32_t key_id,
                                 const uint8_t signed_data[448],
                                 const uint8_t signature[64])
{
  struct host_context_s *context = arg;
  uint8_t encoded_point[65];
  uint8_t digest[32];
  unsigned int digest_len = 0;
  const EC_GROUP *group;
  EC_KEY *key = NULL;
  EC_POINT *point = NULL;
  ECDSA_SIG *decoded = NULL;
  BIGNUM *r = NULL;
  BIGNUM *s = NULL;
  int result = -1;

  if (context == NULL || context->public_xy == NULL || key_id != 1)
    {
      return -1;
    }

  key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (key == NULL)
    {
      goto out;
    }

  group = EC_KEY_get0_group(key);
  point = EC_POINT_new(group);
  if (point == NULL)
    {
      goto out;
    }

  encoded_point[0] = 0x04;
  memcpy(encoded_point + 1, context->public_xy, 64);
  if (EC_POINT_oct2point(group, point, encoded_point,
                        sizeof(encoded_point), NULL) != 1 ||
      EC_KEY_set_public_key(key, point) != 1 || EC_KEY_check_key(key) != 1)
    {
      goto out;
    }

  r = BN_bin2bn(signature, 32, NULL);
  s = BN_bin2bn(signature + 32, 32, NULL);
  decoded = ECDSA_SIG_new();
  if (r == NULL || s == NULL || decoded == NULL ||
      ECDSA_SIG_set0(decoded, r, s) != 1)
    {
      goto out;
    }

  r = NULL;
  s = NULL;
  if (EVP_Digest(signed_data, 448, digest, &digest_len,
                 EVP_sha256(), NULL) != 1 || digest_len != 32)
    {
      goto out;
    }

  result = ECDSA_do_verify(digest, sizeof(digest), decoded, key) == 1 ? 0 : -1;

out:
  BN_free(r);
  BN_free(s);
  ECDSA_SIG_free(decoded);
  EC_POINT_free(point);
  EC_KEY_free(key);
  return result;
}

int bk7258_n17_manifest_host_verify(
  const uint8_t *manifest, size_t manifest_size, const uint8_t public_xy[64],
  uint64_t minimum_security_counter, uint8_t signed_sha256[32])
{
  struct host_context_s context;
  struct bk7258_boot_n17_manifest_ops_s ops;
  struct bk7258_boot_n17_manifest_info_s info;
  int ret;

  if (public_xy == NULL || signed_sha256 == NULL)
    {
      return -1;
    }

  context.public_xy = public_xy;
  ops.arg = &context;
  ops.sha256 = host_sha256;
  ops.verify_signature = host_verify_signature;
  ret = bk7258_boot_n17_manifest_verify(
    manifest, manifest_size, minimum_security_counter, &ops, &info);
  if (ret == 0)
    {
      memcpy(signed_sha256, info.signed_sha256, 32);
    }

  return ret;
}
