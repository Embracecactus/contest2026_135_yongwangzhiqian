/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_control_ota_request.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define BKCONTROL_OTA_HEADER_SIZE 44u

static unsigned int bkcontrol_ota_be16(const uint8_t *value)
{
  return ((unsigned int)value[0] << 8) | value[1];
}

/* The URL is copied as text, so accept only printable ASCII without URI
 * components that could change the requested catalog after validation.
 */
static bool bkcontrol_ota_url_byte_invalid(uint8_t value)
{
  return value <= 32u || value >= 127u || value == '\\' || value == '@' ||
         value == '?' || value == '#';
}

static bool bkcontrol_ota_hostname_valid(const char *hostname, size_t length)
{
  size_t label_length = 0;
  size_t index;

  if (length == 0 || length > 253 || hostname[0] == '-')
    {
      return false;
    }

  for (index = 0; index < length; index++)
    {
      unsigned char value = (unsigned char)hostname[index];

      if (value == '.')
        {
          if (label_length == 0 || hostname[index - 1] == '-')
            {
              return false;
            }

          label_length = 0;
          continue;
        }

      if (value != '-' && !(value >= 'a' && value <= 'z') &&
          !(value >= 'A' && value <= 'Z') && !(value >= '0' && value <= '9'))
        {
          return false;
        }

      if (label_length == 0 && value == '-')
        {
          return false;
        }

      if (++label_length > 63)
        {
          return false;
        }
    }

  return label_length != 0 && hostname[length - 1] != '-';
}

static bool bkcontrol_ota_url_valid(const char *url, size_t length)
{
  static const char catalog_suffix[] = "/catalog.json";
  const size_t suffix_length = sizeof(catalog_suffix) - 1u;
  const char *authority;
  const char *path;
  const char *port_separator;
  size_t authority_length;
  size_t hostname_length;
  unsigned int port = 443u;
  size_t index;

  if (length < 8u + suffix_length || memcmp(url, "https://", 8u) != 0 ||
      memcmp(url + length - suffix_length, catalog_suffix, suffix_length) != 0)
    {
      return false;
    }

  authority = url + 8u;
  authority_length = length - 8u;
  path = memchr(authority, '/', authority_length);
  if (path == NULL || path == authority)
    {
      return false;
    }

  port_separator = memchr(authority, ':', (size_t)(path - authority));
  hostname_length = (size_t)((port_separator == NULL ? path : port_separator) -
                             authority);
  if (!bkcontrol_ota_hostname_valid(authority, hostname_length))
    {
      return false;
    }

  if (port_separator == NULL)
    {
      return true;
    }

  if (port_separator + 1 == path)
    {
      return false;
    }

  port = 0;
  for (index = 0; port_separator + 1u + index < path; index++)
    {
      unsigned char value = (unsigned char)port_separator[1u + index];

      if (value < '0' || value > '9' ||
          port > (65535u - (unsigned int)(value - '0')) / 10u)
        {
          return false;
        }

      port = port * 10u + (unsigned int)(value - '0');
    }

  return port != 0;
}

static bool bkcontrol_ota_ipv4_valid(const uint8_t *ipv4)
{
  if ((ipv4[0] == 0 && ipv4[1] == 0 && ipv4[2] == 0 && ipv4[3] == 0) ||
      ipv4[0] == 127 || ipv4[0] >= 224)
    {
      return false;
    }

  return true;
}

static bool bkcontrol_ota_digest_nonzero(const uint8_t *digest)
{
  size_t index;

  for (index = 0; index < 32u; index++)
    {
      if (digest[index] != 0)
        {
          return true;
        }
    }

  return false;
}

static bool bkcontrol_ota_pem_bytes_valid(const uint8_t *pem, size_t length)
{
  size_t index;

  for (index = 0; index < length; index++)
    {
      uint8_t value = pem[index];

      if (value == 0 ||
          ((value < 32u || value > 126u) && value != '\r' && value != '\n'))
        {
          return false;
        }
    }

  return true;
}

int bkcontrol_ota_request_parse(const uint8_t *record, size_t size,
                                struct bkcontrol_ota_request_s *out)
{
  unsigned int url_length;
  unsigned int ca_length;
  size_t expected_size;
  size_t index;

  if (out == NULL)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));

  if (record == NULL || size < BKCONTROL_OTA_HEADER_SIZE ||
      size > BKCONTROL_OTA_RECORD_MAX || memcmp(record, "SOU1", 4u) != 0)
    {
      return -EINVAL;
    }

  url_length = bkcontrol_ota_be16(record + 4u);
  ca_length = bkcontrol_ota_be16(record + 6u);
  expected_size = BKCONTROL_OTA_HEADER_SIZE + (size_t)url_length +
                  (size_t)ca_length;
  if (url_length == 0 || url_length > BKCONTROL_OTA_URL_MAX ||
      ca_length == 0 || ca_length > BKCONTROL_OTA_CA_MAX || size != expected_size ||
      !bkcontrol_ota_ipv4_valid(record + 8u) ||
      !bkcontrol_ota_digest_nonzero(record + 12u))
    {
      return -EINVAL;
    }

  for (index = 0; index < url_length; index++)
    {
      if (bkcontrol_ota_url_byte_invalid(record[BKCONTROL_OTA_HEADER_SIZE + index]))
        {
          return -EINVAL;
        }
    }

  if (!bkcontrol_ota_pem_bytes_valid(record + BKCONTROL_OTA_HEADER_SIZE +
                                      url_length, ca_length))
    {
      return -EINVAL;
    }

  memcpy(out->ipv4, record + 8u, sizeof(out->ipv4));
  memcpy(out->catalog_sha256, record + 12u, sizeof(out->catalog_sha256));
  memcpy(out->url, record + BKCONTROL_OTA_HEADER_SIZE, url_length);
  memcpy(out->ca_pem, record + BKCONTROL_OTA_HEADER_SIZE + url_length, ca_length);

  if (!bkcontrol_ota_url_valid(out->url, url_length) ||
      strstr(out->ca_pem, "-----BEGIN CERTIFICATE-----") == NULL ||
      strstr(out->ca_pem, "-----END CERTIFICATE-----") == NULL)
    {
      memset(out, 0, sizeof(*out));
      return -EINVAL;
    }

  return 0;
}
