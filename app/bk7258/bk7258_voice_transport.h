/****************************************************************************
 * app/bk7258/bk7258_voice_transport.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * App-private, transport-medium-neutral BKVoice byte-stream provider seam.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_TRANSPORT_H
#define __APP_BK7258_BK7258_VOICE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* A successful open_verified() means that DNS/TCP/TLS has completed and the
 * peer certificate chain, the exact host name and trusted wall-clock time
 * have all been verified.  It also applies the deployment's client
 * credential through an opaque provider/handle when that deployment requires
 * one.  Implementations must fail closed; plaintext or verification-bypass
 * modes are outside this interface.
 *
 * send()/recv() obey the absolute deadline passed by the session owner and
 * may each have one concurrent caller.  interrupt() wakes either caller but
 * does not release the stream.  close() is called only after both callers
 * have joined; a negative close result means the owner may retry it.
 *
 * random() supplies cryptographically strong bytes.  sha1() is used only for
 * RFC 6455 Sec-WebSocket-Accept validation; the target provider can bind it
 * to the already selected TLS crypto implementation.
 */

struct bkvoice_wss_tls_ops_s
{
  int (*open_verified)(void *context, const char *host, uint16_t port,
                       uint64_t deadline_ms);
  ssize_t (*send)(void *context, const uint8_t *buffer, size_t bytes,
                  uint64_t deadline_ms);
  ssize_t (*recv)(void *context, uint8_t *buffer, size_t bytes,
                  uint64_t deadline_ms);
  int (*interrupt)(void *context);
  int (*close)(void *context);
  int (*random)(void *context, uint8_t *buffer, size_t bytes);
  int (*sha1)(void *context, const uint8_t *buffer, size_t bytes,
              uint8_t digest[20]);
};

struct bkvoice_transport_ops_s
{
  int (*open)(void *context, uint64_t deadline_ms);
  ssize_t (*send)(void *context, const uint8_t *buffer, size_t bytes,
                  uint64_t deadline_ms);
  ssize_t (*recv)(void *context, uint8_t *buffer, size_t bytes,
                  uint64_t deadline_ms);
  int (*interrupt)(void *context);
  int (*close)(void *context);
};

struct bkvoice_transport_snapshot_s
{
  uint32_t tx_bytes;
  uint32_t rx_bytes;
  int last_error;
  bool initialized;
  bool opened;
};

struct bkvoice_transport_s
{
  struct bkvoice_transport_ops_s ops;
  void *context;
  volatile uint32_t tx_bytes;
  volatile uint32_t rx_bytes;
  volatile int last_error;
  volatile bool opened;
  bool initialized;
};

/* open()/close() are serialized by the session owner.  At most one send and
 * one recv may run concurrently; the provider must support that full-duplex
 * pair.  interrupt() may be called from a third owner to wake either I/O.
 * close() is called only after those I/O callers have joined.  A failed
 * close keeps the transport open so the owner can retry safely.
 */

int bkvoice_transport_initialize(
  struct bkvoice_transport_s *transport,
  const struct bkvoice_transport_ops_s *ops, void *context);
int bkvoice_transport_open(struct bkvoice_transport_s *transport,
                           uint64_t deadline_ms);
int bkvoice_transport_send_all(struct bkvoice_transport_s *transport,
                               const uint8_t *buffer, size_t bytes,
                               uint64_t deadline_ms);
int bkvoice_transport_recv_exact(struct bkvoice_transport_s *transport,
                                 uint8_t *buffer, size_t bytes,
                                 uint64_t deadline_ms);
int bkvoice_transport_interrupt(struct bkvoice_transport_s *transport);
int bkvoice_transport_close(struct bkvoice_transport_s *transport);
void bkvoice_transport_snapshot(
  const struct bkvoice_transport_s *transport,
  struct bkvoice_transport_snapshot_s *snapshot);

#endif /* __APP_BK7258_BK7258_VOICE_TRANSPORT_H */
