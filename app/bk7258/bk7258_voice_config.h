/****************************************************************************
 * app/bk7258/bk7258_voice_config.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Volatile operator provisioning for the BKVoice WSS client.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_CONFIG_H
#define __APP_BK7258_BK7258_VOICE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <netinet/in.h>

#define BKVOICE_CONFIG_MAX_BYTES 8192u

/* A caller initializes this object with {0}, loads it only while clear, and
 * clears it after the owner has disconnected all users of these contexts.
 */

struct bkvoice_config_s
{
  mbedtls_x509_crt ca;
  mbedtls_x509_crt certificate;
  mbedtls_pk_context private_key;
  char host[128];
  struct in_addr peer_address;
  uint64_t utc_anchor_seconds;
  uint64_t monotonic_anchor_ms;
  uint16_t port;
  bool initialized;
};

int bkvoice_config_load(struct bkvoice_config_s *config,
                        const void *blob, size_t size);
bool bkvoice_config_host_valid(const uint8_t *host, size_t length);
/* Complete structural/key validation without changing the system clock or
 * current runtime. Used before a provisioning transaction changes Wi-Fi.
 */
int bkvoice_config_validate(const void *blob, size_t size);
void bkvoice_config_clear(struct bkvoice_config_s *config);
uint64_t bkvoice_config_now_ms(void *unused);
int bkvoice_config_trusted_time(void *config);

#endif /* __APP_BK7258_BK7258_VOICE_CONFIG_H */
