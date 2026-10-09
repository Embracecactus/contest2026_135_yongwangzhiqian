/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_TLS_H
#define __APP_BK7258_PROVISION_TLS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

/* Nonblocking byte transport. The descriptor is copied at start; context is
 * borrowed until close. Each callback must atomically reject a stale expected
 * generation, consume/queue a positive byte count only once, and never report
 * more bytes than offered. read: zero means EOF, -EAGAIN means no input.
 * send: -EAGAIN/-ENOMEM mean backpressure; zero is an error. No callback may
 * reenter TLS. max_send is 1..1024; pacing is transport policy, not a timeout.
 * Connection generation must not be reused while old work can still arrive.
 */
struct bkprov_tls_transport_s
{
  void *context;
  uint32_t (*generation)(void *context);
  ssize_t (*read)(void *context, uint32_t generation, void *data, size_t size);
  ssize_t (*send)(void *context, uint32_t generation, const void *data,
                  size_t size);
  size_t max_send;
  uint32_t send_interval_ms;
};

/* One serialized product worker owns this object. Certificate/key are borrowed
 * exclusively until close; start never loads a file or creates an identity.
 * A verified TLS channel is NOT possession proof or permission to configure.
 */
struct bkprov_tls_s
{
  struct bkprov_tls_transport_s transport;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config config;
  mbedtls_ctr_drbg_context random;
  mbedtls_entropy_context entropy;
  uint64_t (*now_ms)(void *context);
  void *clock_context;
  uint64_t started;
  uint64_t last_now;
  uint64_t next_send;
  uint64_t write_started;
  uint64_t control_activity;
  uint32_t generation;
  size_t pending_size;
  unsigned char pending[1024];
  bool initialized;
  bool established;
  bool control;
};

/* Zero-initialize before first use. All calls are nonblocking except crypto.
 * step: 0 handshake/output pending, 1 ready to read/queue, negative terminal.
 * queue copies one <=1024-byte application message; step retains that exact
 * buffer across TLS WANT_WRITE retries. read returns -EAGAIN when idle.
 * Any terminal error closes and wipes the session. Owner closes the transport
 * window/disconnects too; it must not reopen TLS on the same generation.
 */
int bkprov_tls_start(struct bkprov_tls_s *tls, uint32_t generation,
                     mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                     uint64_t (*now_ms)(void *), void *clock_context);
/* Explicit transport does not authorize a peer or select application services.
 * The original start wrapper retains GATT's 20-byte/10-ms pacing.
 */
int bkprov_tls_start_transport(struct bkprov_tls_s *tls, uint32_t generation,
                              mbedtls_x509_crt *certificate,
                              mbedtls_pk_context *key,
                              uint64_t (*now_ms)(void *), void *clock_context,
                              const struct bkprov_tls_transport_s *transport);
int bkprov_tls_step(struct bkprov_tls_s *tls);
int bkprov_tls_queue(struct bkprov_tls_s *tls, const void *data, size_t size);
ssize_t bkprov_tls_read(struct bkprov_tls_s *tls, void *data, size_t size);
/* Provisioning has a non-extendable 120s lifetime.  A control owner may
 * promote only after its authenticated AUTH packet succeeds; later calls to
 * touch require a validated authenticated control packet and set its 120s
 * idle deadline.  Neither raw GATT/TLS input nor a pending write refreshes it.
 */
int bkprov_tls_promote_control(struct bkprov_tls_s *tls);
int bkprov_tls_touch_control(struct bkprov_tls_s *tls);
void bkprov_tls_close(struct bkprov_tls_s *tls);
#endif
