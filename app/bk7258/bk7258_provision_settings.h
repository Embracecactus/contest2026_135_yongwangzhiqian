/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_SETTINGS_H
#define __APP_BK7258_PROVISION_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#define BKPROV_CONTROL_KEY_BYTES 32u

/* SCB3 appends a nonzero 32-byte owner control key after the SCB2 cloud record.
 * It is a secret in the same private configuration transaction, never status
 * or recovery-response data. The factory possession secret is separate.
 * SCB2 appends a validated CCF1 record to SCB1, with its length at byte 28.
 * Host and port must agree in both records; cloud bytes remain borrowed.
 * SCB1 is one complete configuration: SSID/PSK, Gateway hostname/address/port,
 * CA DER and owner-supplied UTC anchor. Device private keys never come from
 * the phone. Borrowed CA storage remains owned by the candidate transaction.
 */
struct bkprov_settings_s
{
  char ssid[33];
  char password[65];
  char host[128];
  uint8_t address[4];
  uint16_t port;
  uint64_t utc;
  const uint8_t *ca;
  size_t ca_size;
  const uint8_t *cloud;
  size_t cloud_size;
  const uint8_t *control_key;
};
int bkprov_settings_decode(struct bkprov_settings_s *settings,
                            const void *bundle, size_t size);
/* Build the existing BVC1 input using device-owned identity bytes. The voice
 * config parser then checks DER, key matching and hostname before network I/O.
 * Caller wipes output and settings after the voice owner has copied them.
 */
int bkprov_settings_voice(const struct bkprov_settings_s *settings,
                          const uint8_t *certificate, size_t certificate_size,
                          const uint8_t *key, size_t key_size,
                          void *output, size_t capacity, size_t *size);
#endif
