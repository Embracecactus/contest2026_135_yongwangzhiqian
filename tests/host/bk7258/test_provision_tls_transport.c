/* SPDX-License-Identifier: Apache-2.0 */
/* The real BIO functions are included to observe their driver boundary. TLS
 * handshakes and ciphertext integrity are covered by test_provision_tls.c.
 */
#include "bk7258_provision_tls.c"
#include <assert.h>
#include <stdio.h>

static uint32_t epoch = 7;
static uint64_t tick = 100;
static ssize_t result;
static size_t offered;
static unsigned int calls;
static bool change_epoch;

uint32_t bkprov_gatt_generation(void) { return 99; }
ssize_t bkprov_gatt_read(uint32_t gen, void *data, size_t size)
{ (void)gen; (void)data; (void)size; assert(false); return -EIO; }
ssize_t bkprov_gatt_send(uint32_t gen, const void *data, size_t size)
{ (void)gen; (void)data; (void)size; assert(false); return -EIO; }

static uint64_t clock_ms(void *context)
{ (void)context; return tick; }
static uint32_t generation(void *context)
{ assert(context == &epoch); return epoch; }
static ssize_t receive(void *context, uint32_t expected, void *data, size_t size)
{
  assert(context == &epoch && expected == epoch);
  (void)data;
  offered = size;
  calls++;
  if (change_epoch) epoch++;
  return result;
}
static ssize_t transmit(void *context, uint32_t expected,
                        const void *data, size_t size)
{ return receive(context, expected, (void *)data, size); }

int main(void)
{
  struct bkprov_tls_s tls = {0};
  struct bkprov_tls_transport_s transport =
    { &epoch, generation, receive, transmit, 64, 0 };
  unsigned char data[128] = {0};
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;

  /* Rejected descriptors must not touch certificates, RNG or the transport. */
  assert(bkprov_tls_start_transport(&tls, 7, &cert, &key, clock_ms,
                                    NULL, NULL) == -EINVAL);
  transport.max_send = 0;
  assert(bkprov_tls_start_transport(&tls, 7, &cert, &key, clock_ms,
                                    NULL, &transport) == -EINVAL);
  transport.max_send = 1025;
  assert(bkprov_tls_start_transport(&tls, 7, &cert, &key, clock_ms,
                                    NULL, &transport) == -EINVAL);
  transport.max_send = 64;
  transport.read = NULL;
  assert(bkprov_tls_start_transport(&tls, 7, &cert, &key, clock_ms,
                                    NULL, &transport) == -EINVAL);
  transport.read = receive;
  assert(bkprov_tls_start_transport(&tls, 8, &cert, &key, clock_ms,
                                    NULL, &transport) == -ESTALE);
  assert(!tls.initialized && calls == 0);

  tls.transport = transport;
  tls.generation = epoch;
  tls.now_ms = clock_ms;
  tls.started = tls.last_now = tick;
  result = 13;
  assert(send_cipher(&tls, data, sizeof(data)) == 13 && offered == 64);
  assert(recv_cipher(&tls, data, sizeof(data)) == 13 && offered == 128);
  result = 65;
  assert(send_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_SEND_FAILED);
  result = 129;
  assert(recv_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_RECV_FAILED);
  result = -EAGAIN;
  assert(send_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_SSL_WANT_WRITE);
  assert(recv_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_SSL_WANT_READ);
  result = -ENOMEM;
  assert(send_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_SSL_WANT_WRITE);
  result = 0;
  assert(send_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_SEND_FAILED);
  assert(recv_cipher(&tls, data, sizeof(data)) == 0);

  calls = 0;
  epoch++;
  assert(send_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_SEND_FAILED);
  assert(recv_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_RECV_FAILED);
  assert(calls == 0);
  tls.generation = epoch;
  change_epoch = true;
  result = 1;
  assert(send_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_SEND_FAILED);
  tls.generation = epoch;
  assert(recv_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_RECV_FAILED);
  change_epoch = false;
  tls.generation = epoch;
  tls.transport.send_interval_ms = 10;
  tls.next_send = 0;
  assert(send_cipher(&tls, data, sizeof(data)) == 1);
  calls = 0;
  tick += 9;
  assert(send_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_SSL_WANT_WRITE);
  assert(calls == 0);
  tick++;
  assert(send_cipher(&tls, data, sizeof(data)) == 1);
  tick = 0;
  assert(send_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_SEND_FAILED);
  assert(recv_cipher(&tls, data, sizeof(data)) == MBEDTLS_ERR_NET_RECV_FAILED);
  puts("TLS transport admission, BIO counts, generation and pacing: PASS");
  return 0;
}
