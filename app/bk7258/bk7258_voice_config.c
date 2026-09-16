/****************************************************************************
 * app/bk7258/bk7258_voice_config.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif

#include "bk7258_voice_config.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>

#define BKVOICE_CONFIG_HEADER_BYTES       32u
#define BKVOICE_CONFIG_MAX_DER_BYTES      4096u
#define BKVOICE_CONFIG_MIN_UTC            UINT64_C(1704067200)
#define BKVOICE_CONFIG_MAX_UTC            UINT64_C(4133980799)
#define BKVOICE_CONFIG_MAX_ANCHOR_MS      (UINT64_C(12) * 60u * 60u * 1000u)
#define BKVOICE_CONFIG_CLOCK_SKEW_SECONDS 5u

static uint16_t bkvoice_config_be16(const uint8_t *value)
{
  return ((uint16_t)value[0] << 8) | value[1];
}

static uint64_t bkvoice_config_be64(const uint8_t *value)
{
  uint64_t result = 0;
  unsigned int index;

  for (index = 0; index < 8; index++)
    {
      result = (result << 8) | value[index];
    }

  return result;
}

static bool bkvoice_config_der_item(const uint8_t *der, size_t size,
                                    uint8_t tag, size_t *header,
                                    size_t *content)
{
  size_t bytes;
  size_t length = 0;
  size_t index;

  if (size < 2 || der[0] != tag)
    {
      return false;
    }

  if ((der[1] & 0x80u) == 0)
    {
      length = der[1];
      bytes = 0;
    }
  else
    {
      bytes = der[1] & 0x7fu;
      if (bytes == 0 || bytes > sizeof(size_t) || size < 2 + bytes ||
          der[2] == 0)
        {
          return false;
        }

      for (index = 0; index < bytes; index++)
        {
          if (length > (SIZE_MAX >> 8))
            {
              return false;
            }

          length = (length << 8) | der[2 + index];
        }

      if (length < 128)
        {
          return false;
        }
    }

  *header = 2 + bytes;
  if (length > size - *header)
    {
      return false;
    }

  *content = length;
  return true;
}

static bool bkvoice_config_pkcs8_valid(const uint8_t *der, size_t size)
{
  size_t outer_header;
  size_t outer_content;
  size_t item_header;
  size_t item_content;
  size_t offset;

  if (!bkvoice_config_der_item(der, size, 0x30, &outer_header,
                                &outer_content) ||
      outer_header + outer_content != size)
    {
      return false;
    }

  offset = outer_header;
  if (!bkvoice_config_der_item(der + offset, size - offset, 0x02,
                                &item_header, &item_content))
    {
      return false;
    }

  offset += item_header + item_content;
  if (!bkvoice_config_der_item(der + offset, size - offset, 0x30,
                                &item_header, &item_content))
    {
      return false;
    }

  offset += item_header + item_content;
  return bkvoice_config_der_item(der + offset, size - offset, 0x04,
                                  &item_header, &item_content);
}

bool bkvoice_config_host_valid(const uint8_t *host, size_t length)
{
  size_t label = 0;
  size_t index;

  if (host == NULL || length == 0 || length > 127)
    {
      return false;
    }

  for (index = 0; index < length; index++)
    {
      uint8_t ch = host[index];
      bool alnum = (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9');

      if (ch == '.')
        {
          if (label == 0 || label > 63 || host[index - 1] == '-')
            {
              return false;
            }

          label = 0;
        }
      else if (!alnum && ch != '-')
        {
          return false;
        }
      else
        {
          if (label == 0 && ch == '-')
            {
              return false;
            }

          label++;
          if (label > 63)
            {
              return false;
            }
        }
    }

  return label != 0 && host[length - 1] != '-';
}

static bool bkvoice_config_peer_valid(const uint8_t *address)
{
  if ((address[0] == 0 && address[1] == 0 && address[2] == 0 &&
       address[3] == 0) ||
      (address[0] == 255 && address[1] == 255 && address[2] == 255 &&
       address[3] == 255) || address[0] == 127 ||
      (address[0] >= 224 && address[0] <= 239))
    {
      return false;
    }

  return true;
}

uint64_t bkvoice_config_now_ms(void *unused)
{
  struct timespec now;

  (void)unused;
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0 || now.tv_sec < 0)
    {
      return 0;
    }

  return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

void bkvoice_config_clear(struct bkvoice_config_s *config)
{
  if (config == NULL)
    {
      return;
    }

  mbedtls_x509_crt_free(&config->ca);
  mbedtls_x509_crt_free(&config->certificate);
  mbedtls_pk_free(&config->private_key);
  mbedtls_platform_zeroize(config, sizeof(*config));
}

static int bkvoice_config_parse(struct bkvoice_config_s *config,
                                 const void *blob, size_t size, bool set_time)
{
  const uint8_t *wire = blob;
  const uint8_t *host;
  const uint8_t *ca;
  const uint8_t *certificate;
  const uint8_t *key;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  struct timespec wall;
  uint16_t host_length;
  uint16_t port;
  uint16_t ca_length;
  uint16_t certificate_length;
  uint16_t key_length;
  uint64_t epoch;
  uint64_t monotonic;
  size_t expected;
  int ret = -EINVAL;
  static const uint8_t personalization[] = "bkvoice-config-pair";
  static const struct bkvoice_config_s cleared;

  if (config == NULL || wire == NULL || size < BKVOICE_CONFIG_HEADER_BYTES ||
      size > BKVOICE_CONFIG_MAX_BYTES || config->initialized)
    {
      return -EINVAL;
    }

  if (memcmp(config, &cleared, sizeof(*config)) != 0)
    {
      return -EBUSY;
    }

  if (memcmp(wire, "BVC1", 4) != 0 || bkvoice_config_be16(wire + 4) != 1 ||
      bkvoice_config_be16(wire + 6) != 0 ||
      bkvoice_config_be16(wire + 30) != 0)
    {
      return -EPROTO;
    }

  epoch = bkvoice_config_be64(wire + 8);
  host_length = bkvoice_config_be16(wire + 16);
  port = bkvoice_config_be16(wire + 18);
  ca_length = bkvoice_config_be16(wire + 24);
  certificate_length = bkvoice_config_be16(wire + 26);
  key_length = bkvoice_config_be16(wire + 28);
  expected = BKVOICE_CONFIG_HEADER_BYTES + (size_t)host_length + ca_length +
             certificate_length + key_length;
  if (expected != size || host_length == 0 || host_length > 127 || port == 0 ||
      ca_length == 0 || certificate_length == 0 || key_length == 0 ||
      ca_length > BKVOICE_CONFIG_MAX_DER_BYTES ||
      certificate_length > BKVOICE_CONFIG_MAX_DER_BYTES ||
      key_length > BKVOICE_CONFIG_MAX_DER_BYTES || epoch < BKVOICE_CONFIG_MIN_UTC ||
      epoch > BKVOICE_CONFIG_MAX_UTC || !bkvoice_config_peer_valid(wire + 20))
    {
      return -EINVAL;
    }

  host = wire + BKVOICE_CONFIG_HEADER_BYTES;
  ca = host + host_length;
  certificate = ca + ca_length;
  key = certificate + certificate_length;
  if (!bkvoice_config_host_valid(host, host_length))
    {
      return -EINVAL;
    }

  if (!bkvoice_config_pkcs8_valid(key, key_length))
    {
      return -EPROTO;
    }

  mbedtls_x509_crt_init(&config->ca);
  mbedtls_x509_crt_init(&config->certificate);
  mbedtls_pk_init(&config->private_key);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);

  ret = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                              personalization, sizeof(personalization) - 1u);
  if (ret == 0)
    {
      ret = mbedtls_x509_crt_parse_der(&config->ca, ca, ca_length);
      if (ret == 0 && config->ca.MBEDTLS_PRIVATE(ca_istrue) == 0)
        {
          ret = -EPROTO;
        }
    }

  if (ret == 0)
    {
      ret = mbedtls_x509_crt_parse_der(&config->certificate, certificate,
                                       certificate_length);
    }

  if (ret == 0)
    {
      ret = mbedtls_pk_parse_key(&config->private_key, key, key_length,
                                 NULL, 0, mbedtls_ctr_drbg_random, &random);
    }

  if (ret == 0)
    {
      ret = mbedtls_pk_check_pair(&config->certificate.pk,
                                  &config->private_key,
                                  mbedtls_ctr_drbg_random, &random);
    }

  if (ret == 0)
    {
      monotonic = bkvoice_config_now_ms(NULL);
      if (monotonic == 0)
        {
          ret = -EIO;
        }
    }

  if (ret == 0 && set_time)
    {
      wall.tv_sec = (time_t)epoch;
      wall.tv_nsec = 0;
      if (clock_settime(CLOCK_REALTIME, &wall) < 0)
        {
          ret = -errno;
        }
    }

  if (ret == 0)
    {
      memcpy(config->host, host, host_length);
      config->host[host_length] = '\0';
      memcpy(&config->peer_address.s_addr, wire + 20, 4);
      config->port = port;
      config->utc_anchor_seconds = epoch;
      config->monotonic_anchor_ms = monotonic;
      config->initialized = true;
    }

  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  if (ret != 0)
    {
      bkvoice_config_clear(config);
      return ret < 0 ? ret : -EIO;
    }

  return 0;
}

int bkvoice_config_load(struct bkvoice_config_s *config,
                        const void *blob, size_t size)
{
  return bkvoice_config_parse(config, blob, size, true);
}

int bkvoice_config_validate(const void *blob, size_t size)
{
  struct bkvoice_config_s candidate = {0};
  int ret = bkvoice_config_parse(&candidate, blob, size, false);
  bkvoice_config_clear(&candidate);
  return ret;
}

int bkvoice_config_trusted_time(void *context)
{
  struct bkvoice_config_s *config = context;
  struct timespec wall;
  uint64_t monotonic;
  uint64_t elapsed;
  uint64_t expected;
  uint64_t actual;

  if (config == NULL || !config->initialized ||
      config->monotonic_anchor_ms == 0 ||
      clock_gettime(CLOCK_REALTIME, &wall) < 0 || wall.tv_sec < 0)
    {
      return -EACCES;
    }

  monotonic = bkvoice_config_now_ms(NULL);
  if (monotonic < config->monotonic_anchor_ms)
    {
      return -EACCES;
    }

  elapsed = monotonic - config->monotonic_anchor_ms;
  if (elapsed > BKVOICE_CONFIG_MAX_ANCHOR_MS)
    {
      return -EACCES;
    }

  expected = config->utc_anchor_seconds + elapsed / 1000u;
  actual = (uint64_t)wall.tv_sec;
  if ((actual > expected ? actual - expected : expected - actual) >
      BKVOICE_CONFIG_CLOCK_SKEW_SECONDS)
    {
      return -EACCES;
    }

  return 0;
}
