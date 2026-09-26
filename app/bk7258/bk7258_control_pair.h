/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CONTROL_PAIR_H
#define __APP_BK7258_CONTROL_PAIR_H
#include "bk7258_control_session.h"
#include "bk7258_provision_tls.h"

/* Shared TLS implementation, with exactly one TLS owner per connection.
 * The original GATT start additionally permits a first SPV1 frame to enter
 * the possession/recovery flow borrowing that TLS. SDC1 always requires its
 * separately supplied control credential; possession is not control AUTH.
 */
struct bkcontrol_pair_s
{
  struct bkprov_tls_s tls;
  struct bkprov_pair_s *scan;
  uint8_t scan_secret[32];
  const struct bkprov_claim_ops_s *rebind_ops;
  void *rebind_context;
  struct bkcontrol_session_s session;
  uint8_t input[BKCONTROL_REQUEST_MAX];
  uint8_t response[BKCONTROL_RESPONSE_SIZE];
  size_t received;
  size_t expected;
  uint64_t authentication_started;
  bool authenticating;
  bool report;
  bool provisioning_allowed;
};
int bkcontrol_pair_start(struct bkcontrol_pair_s *, uint32_t generation,
                         mbedtls_x509_crt *, mbedtls_pk_context *,
                         const uint8_t owner_key[32], uint64_t (*now_ms)(void *),
                         void *clock_context, bkcontrol_execute_t, void *);
/* SDC1-only transport. The trusted owner supplies this principal's credential
 * and callbacks; this API neither grants credentials nor reads the phone key.
 * SPV1 possession/recovery is never enabled here. Context/transport ownership
 * follows bkprov_tls_start_transport. Revocation requires closing the session
 * or invalidating its transport generation, including already-authenticated
 * connections. Configure optional OTA/config handlers before packet handling.
 */
int bkcontrol_pair_start_transport(struct bkcontrol_pair_s *,
                                   uint32_t generation, mbedtls_x509_crt *,
                                   mbedtls_pk_context *, const uint8_t key[32],
                                   uint64_t (*now_ms)(void *), void *clock_context,
                                   bkcontrol_execute_t, void *context,
                                   const struct bkprov_tls_transport_s *);
/* Serialized AP owner call. Crypto may run here, never in Host callbacks.
 * Negative result is terminal; owner must close/disconnect the transport.
 * Existing TLS limits apply: handshake 30s, session 120s, write 5s. Control-key
 * authentication is additionally limited to 10s after TLS establishes.
 */
int bkcontrol_pair_step(struct bkcontrol_pair_s *);
void bkcontrol_pair_close(struct bkcontrol_pair_s *);
#endif
