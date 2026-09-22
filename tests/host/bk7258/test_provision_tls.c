/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_tls.h"
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_pair.h"
#include "bk7258_provision_store.h"
#include "bk7258_control_pair.h"
#include <arch/chip/bk7258_wifi.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

struct queue_s { unsigned char data[4096]; size_t size; };
static struct queue_s inbound, outbound;
static uint32_t generation = 1;
static uint64_t now;
static bool congested;
static unsigned int fragments;
static struct bkprov_store_s pair_store;
static bool network_verified;
static int pair_commits;
static uint8_t saved_transaction[16];
static uint32_t scan_ticket;
static bool scan_ready;
static struct bk7258_wifi_scan_snapshot_s scan_snapshot;

int bk7258_wifi_scan_async(uint32_t timeout_ms, uint32_t *ticket)
{
  assert(timeout_ms == BK7258_WIFI_SCAN_DEFAULT_MS);
  *ticket = ++scan_ticket;
  return 0;
}
int bk7258_wifi_scan_snapshot_poll(uint32_t ticket,
                                    struct bk7258_wifi_scan_snapshot_s *out)
{
  if (ticket != scan_ticket) return -ESTALE;
  if (!scan_ready) return -EAGAIN;
  *out = scan_snapshot;
  scan_ready = false;
  return 0;
}
static int read_receipt(const uint8_t tx[16])
{ return memcmp(tx, saved_transaction, 16) == 0 ? 1 : -EINPROGRESS; }
int bkprov_storage_receipt(const uint8_t tx[16])
{ return read_receipt(tx); }
#define server pair.tls

static int trial_begin(void *context, const uint8_t *data, size_t size)
{ (void)context; assert(size == 6 && data[0] == 1 && data[5] == 6); return 0; }
static int trial_poll(void *context) { (void)context; return network_verified ? 1 : 0; }
static int trial_commit(void *context, const uint8_t tx[16], const uint8_t *data, size_t size)
{ (void)context; pair_commits++; return bkprov_store_commit(&pair_store, 0, tx, data, size); }
static void trial_abort(void *context) { (void)context; }
static const struct bkprov_claim_ops_s pair_ops =
{trial_begin, trial_poll, trial_commit, trial_abort};

static uint64_t clock_ms(void *unused) { (void)unused; return now; }
uint32_t bkprov_gatt_generation(void) { return generation; }

static int put(struct queue_s *q, const unsigned char *data, size_t size)
{
  size = size < 20 ? size : 20;
  if (q->size + size > sizeof(q->data)) return -EAGAIN;
  memcpy(q->data + q->size, data, size); q->size += size;
  return (int)size;
}

static int take(struct queue_s *q, unsigned char *data, size_t size)
{
  size = size < q->size ? size : q->size;
  if (!size) return -EAGAIN;
  memcpy(data, q->data, size);
  q->size -= size; memmove(q->data, q->data + size, q->size);
  return (int)size;
}

ssize_t bkprov_gatt_read(uint32_t gen, void *data, size_t size)
{ return gen == generation ? take(&inbound, data, size) : -ESTALE; }

ssize_t bkprov_gatt_send(uint32_t gen, const void *data, size_t size)
{
  assert(size <= 20);
  if (gen != generation) return -ESTALE;
  if (congested) return -ENOMEM;
  int ret = put(&outbound, data, size);
  if (ret > 0) fragments++;
  return ret;
}

static int client_send(void *ctx, const unsigned char *data, size_t size)
{
  (void)ctx; int ret = put(&inbound, data, size);
  return ret < 0 ? MBEDTLS_ERR_SSL_WANT_WRITE : ret;
}

static int client_recv(void *ctx, unsigned char *data, size_t size)
{
  (void)ctx; int ret = take(&outbound, data, size);
  return ret < 0 ? MBEDTLS_ERR_SSL_WANT_READ : ret;
}

static void assert_wiped(struct bkprov_tls_s *tls)
{
  const unsigned char *p = (const unsigned char *)tls;
  for (size_t i = 0; i < sizeof(*tls); i++) assert(p[i] == 0);
}

static void receive_status(struct bkprov_pair_s *pair, mbedtls_ssl_context *client,
                           unsigned int state)
{
  uint8_t response[40]; size_t size = 0;
  for (int i = 0;i<200 && size<sizeof(response);i++)
    {
      assert(bkprov_pair_step(pair) == 0);
      int ret = mbedtls_ssl_read(client, response+size, sizeof(response)-size);
      assert(ret>0 || ret == MBEDTLS_ERR_SSL_WANT_READ);
      if (ret>0) size += ret;
      now += 10;
    }
  assert(size == 40 && !memcmp(response, "SPV1", 4) && response[4] == 128);
  assert(response[32] == 0 && response[33] == 0 && response[34] == 0 && response[35] == state);
  assert(response[36] == 0 && response[37] == 0 && response[38] == 0 && response[39] == 0);
}

static void request(mbedtls_ssl_context *client, unsigned int type,
                     unsigned int sequence, const uint8_t *data, size_t size)
{
  uint8_t frame[1056]={'S', 'P', 'V', '1'};
  assert(size <= 1024 && sequence<256);
  frame[4] = type; frame[11] = sequence; frame[12] = 7;
  frame[30] = size>>8; frame[31] = size;
  if (size) memcpy(frame+32, data, size);
  assert(mbedtls_ssl_write(client, frame, 32+size) == (int)(32+size));
}

static void receive_scan(struct bkprov_pair_s *pair, mbedtls_ssl_context *client)
{
  uint8_t response[904]; size_t size = 0;
  for (int i = 0; i < 200 && size < 76; i++)
    {
      assert(bkprov_pair_step(pair) == 0);
      int ret = mbedtls_ssl_read(client, response + size, sizeof(response) - size);
      assert(ret > 0 || ret == MBEDTLS_ERR_SSL_WANT_READ);
      if (ret > 0) size += ret;
      now += 10;
    }
  assert(size == 76 && !memcmp(response, "SPV1", 4) && response[4] == 129);
  assert(response[11] == 1 && response[28] == 0 && response[29] == 0 &&
         response[30] == 0 && response[31] == 44);
  assert(response[32] == 0 && response[33] == 0 && response[34] == 0 && response[35] == 0);
  assert(response[36] == 1 && response[37] == 1 && response[40] == 4 &&
         (int8_t)response[41] == -44 && response[42] == 6 && response[43] == 8 &&
         !memcmp(response + 44, "test", 4));
}

static unsigned control_calls;
static int control_execute(void *context, enum bkcontrol_command_e command,
                           uint32_t value, struct bkcontrol_status_s *status)
{
  (void)context;
  assert(command == BKCONTROL_VOLUME && value == 73);
  control_calls++;
  status->flags = 8;
  status->volume = value;
  return 0;
}
static void control_handshake(struct bkcontrol_pair_s *control,
                              mbedtls_ssl_context *client,
                              mbedtls_x509_crt *cert, mbedtls_pk_context *key)
{
  const uint8_t owner[32] = {42};
  generation++;
  inbound.size = outbound.size = 0;
  congested = false;
  assert(mbedtls_ssl_session_reset(client) == 0);
  assert(bkcontrol_pair_start(control, generation, cert, key, owner,
                             clock_ms, NULL, control_execute, NULL) == 0);
  bool ready = false;
  for (int i = 0; i < 2500 && (!ready || !control->tls.established); i++)
    {
      assert(bkcontrol_pair_step(control) == 0);
      if (!ready)
        {
          int ret = mbedtls_ssl_handshake(client);
          assert(ret == 0 || ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);
          ready = ret == 0;
        }
      now += 10;
    }
  assert(ready && control->tls.established);
}
static void control_response(struct bkcontrol_pair_s *control,
                             mbedtls_ssl_context *client, uint8_t command,
                             uint8_t sequence, uint8_t volume)
{
  uint8_t response[40]; size_t size = 0;
  for (int i = 0; i < 200 && size < sizeof(response); i++)
    {
      assert(bkcontrol_pair_step(control) == 0);
      int ret = mbedtls_ssl_read(client, response + size, sizeof(response) - size);
      assert(ret > 0 || ret == MBEDTLS_ERR_SSL_WANT_READ);
      if (ret > 0) size += ret;
      now += 10;
    }
  assert(size == sizeof(response) && !memcmp(response, "SDC1", 4));
  assert(response[4] == 128 && response[7] == command && response[11] == sequence);
  assert(response[15] == 24 && response[19] == 0 && response[27] == volume);
}
static void control_encrypted_tests(mbedtls_ssl_context *client,
                                    mbedtls_x509_crt *cert, mbedtls_pk_context *key)
{
  struct bkcontrol_pair_s control = {0};
  uint8_t auth[48] = {'S', 'D', 'C', '1', 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 32, 42};
  uint8_t volume[20] = {'S', 'D', 'C', '1', 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 73};
  control_handshake(&control, client, cert, key);
  /* Split the plaintext header and key across independent TLS records. */
  for (size_t i = 0; i < sizeof(auth); i++)
    assert(mbedtls_ssl_write(client, auth + i, 1) == 1);
  control_response(&control, client, 1, 0, 255);
  assert(control.session.authenticated && control_calls == 0);
  assert(mbedtls_ssl_write(client, volume, sizeof(volume)) == sizeof(volume));
  congested = true;
  for (int i = 0; i < 80; i++)
    { assert(bkcontrol_pair_step(&control) == 0); now += 10; }
  assert(control_calls == 1);
  congested = false;
  control_response(&control, client, 4, 1, 73);
  assert(control_calls == 1); /* Backpressure never repeats the mutation. */
  assert(mbedtls_ssl_write(client, volume, sizeof(volume)) == sizeof(volume));
  int error = 0;
  for (int i = 0; i < 200 && !error; i++)
    { error = bkcontrol_pair_step(&control); now += 10; }
  assert(error == -EPROTO && control_calls == 1 && !control.tls.initialized);

  control_handshake(&control, client, cert, key);
  auth[16] ^= 1;
  assert(mbedtls_ssl_write(client, auth, sizeof(auth)) == sizeof(auth));
  error = 0;
  for (int i = 0; i < 200 && !error; i++)
    { error = bkcontrol_pair_step(&control); now += 10; }
  assert(error < 0 && control_calls == 1 && !control.tls.initialized);

  control_handshake(&control, client, cert, key);
  now += 10000;
  assert(bkcontrol_pair_step(&control) == -ETIMEDOUT);
  const unsigned char *bytes = (const unsigned char *)&control;
  for (size_t i = 0; i < sizeof(control); i++) assert(bytes[i] == 0);
}

/* Ciphertext pipe endpoint for the JVM production TLS/GATT client test.
 * Only synthetic owner key/state are used; no sockets or hardware are opened.
 */
static int control_pipe_peer(const char *certificate, const char *private_key)
{
  struct bkcontrol_pair_s control = {0};
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;
  mbedtls_ctr_drbg_context random;
  mbedtls_entropy_context entropy;
  const uint8_t owner[32] = {42};
  struct timespec time;
  int result = 0;
  mbedtls_x509_crt_init(&cert); mbedtls_pk_init(&key);
  mbedtls_ctr_drbg_init(&random); mbedtls_entropy_init(&entropy);
  assert(mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, NULL, 0) == 0);
  assert(mbedtls_x509_crt_parse_file(&cert, certificate) == 0);
  assert(mbedtls_pk_parse_keyfile(&key, private_key, NULL,
                                 mbedtls_ctr_drbg_random, &random) == 0);
  assert(fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) == 0);
  assert(fcntl(STDOUT_FILENO, F_SETFL, O_NONBLOCK) == 0);
  signal(SIGPIPE, SIG_IGN);
  clock_gettime(CLOCK_MONOTONIC, &time);
  now = (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
  assert(bkcontrol_pair_start(&control, generation, &cert, &key, owner,
                             clock_ms, NULL, control_execute, NULL) == 0);
  for (;;)
    {
      uint8_t bytes[20];
      ssize_t count = read(STDIN_FILENO, bytes, sizeof(bytes));
      if (count == 0) break;
      if (count < 0 && errno != EAGAIN && errno != EINTR) { result = 2; break; }
      if (count > 0 && put(&inbound, bytes, (size_t)count) != count) { result = 3; break; }
      clock_gettime(CLOCK_MONOTONIC, &time);
      now = (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
      if (bkcontrol_pair_step(&control) < 0) { result = 4; break; }
      if (outbound.size)
        {
          count = write(STDOUT_FILENO, outbound.data, outbound.size < 20 ? outbound.size : 20);
          if (count < 0 && errno != EAGAIN && errno != EINTR) { result = 5; break; }
          if (count > 0)
            { outbound.size -= count; memmove(outbound.data, outbound.data + count, outbound.size); }
        }
      struct timespec pause = {0, 1000000};
      nanosleep(&pause, NULL);
    }
  bkcontrol_pair_close(&control);
  mbedtls_pk_free(&key); mbedtls_x509_crt_free(&cert);
  mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
  return result;
}

int main(int argc, char **argv)
{
  if (argc == 4 && !strcmp(argv[1], "--control-peer"))
    return control_pipe_peer(argv[2], argv[3]);
  struct bkprov_pair_s pair = {0};
  mbedtls_ssl_context client;
  mbedtls_ssl_config config;
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;
  mbedtls_ctr_drbg_context random;
  mbedtls_entropy_context entropy;
  unsigned char message[1024], received[1024];
  size_t count;
  int ret;
  bool client_ready = false;
  assert(argc == 4 && bkprov_store_open(&pair_store, argv[3]) == 0);
  mbedtls_ssl_init(&client); mbedtls_ssl_config_init(&config);
  mbedtls_x509_crt_init(&cert); mbedtls_pk_init(&key);
  mbedtls_ctr_drbg_init(&random); mbedtls_entropy_init(&entropy);
  assert(mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                              NULL, 0) == 0);
  assert(mbedtls_x509_crt_parse_file(&cert, argv[1]) == 0);
  assert(mbedtls_pk_parse_keyfile(&key, argv[2], NULL,
                                 mbedtls_ctr_drbg_random, &random) == 0);
  assert(mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
  mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
  mbedtls_ssl_conf_ca_chain(&config, &cert, NULL);
  mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
  assert(mbedtls_ssl_setup(&client, &config) == 0);
  assert(mbedtls_ssl_set_hostname(&client, "localhost") == 0);
  mbedtls_ssl_set_bio(&client, NULL, client_send, client_recv, NULL);
  assert(bkprov_tls_start(&server, 2, &cert, &key, clock_ms, NULL) == -ESTALE);
  assert(bkprov_tls_start(&server, 1, &cert, &key, clock_ms, NULL) == 0);
  assert(bkprov_tls_queue(&server, "early", 5) == -EAGAIN);
  for (int i = 0; i < 2500 && (!client_ready || !server.established); i++)
    {
      congested = i < 30;
      ret = bkprov_tls_step(&server); assert(ret >= 0);
      if (!client_ready)
        {
          ret = mbedtls_ssl_handshake(&client);
          if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ &&
              ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            fprintf(stderr, "client handshake failed: %d\n", ret);
          assert(ret == 0 || ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);
          client_ready = ret == 0;
        }
      now += 10;
    }
  assert(client_ready && server.established && fragments > 20);
  assert(mbedtls_ssl_get_verify_result(&client) == 0);
  assert(strcmp(mbedtls_ssl_get_ciphersuite(&client),
                "TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256") == 0);
  for (size_t i = 0; i < sizeof(message); i++) message[i] = (unsigned char)i;
  assert(mbedtls_ssl_write(&client, message, sizeof(message)) == sizeof(message));
  count = 0;
  for (int i = 0; i < 100 && count < sizeof(received); i++)
    {
      ret = bkprov_tls_read(&server, received + count, sizeof(received) - count);
      assert(ret > 0 || ret == -EAGAIN);
      if (ret > 0) count += ret;
      now += 10;
    }
  assert(count == sizeof(message) && memcmp(message, received, count) == 0);
  assert(bkprov_tls_queue(&server, message, sizeof(message)) == 0);
  assert(bkprov_tls_queue(&server, message, 1) == -EAGAIN);
  count = 0;
  for (int i = 0; i < 450 && count < sizeof(received); i++)
    {
      congested = i < 20;
      assert(bkprov_tls_step(&server) >= 0);
      ret = mbedtls_ssl_read(&client, received + count, sizeof(received) - count);
      assert(ret > 0 || ret == MBEDTLS_ERR_SSL_WANT_READ);
      if (ret > 0) count += ret;
      now += 10;
    }
  assert(count == sizeof(message) && memcmp(message, received, count) == 0);
  assert(server.pending_size == 0);
  for (size_t i = 0; i < sizeof(server.pending); i++) assert(server.pending[i] == 0);
  /* A record must not be duplicated after successful fragments or WANT_WRITE. */
  assert(mbedtls_ssl_read(&client, received, sizeof(received)) == MBEDTLS_ERR_SSL_WANT_READ);
  assert(mbedtls_ssl_write(&client, message, sizeof(message)) == sizeof(message));
  inbound.data[inbound.size - 1] ^= 1;
  memset(received, 0, sizeof(received));
  assert(bkprov_tls_read(&server, received, sizeof(received)) == -ECONNRESET);
  assert_wiped(&server);

  /* A fresh encrypted connection must release queued plaintext when output
   * congestion lasts beyond the deadline. No same-generation TLS restart is
   * used: emulate disconnect/new subscription and clear the ATT stream.
   */
  generation++;
  inbound.size = outbound.size = 0;
  assert(mbedtls_ssl_session_reset(&client) == 0);
  const uint8_t proof[32]={42};
  assert(bkprov_pair_start(&pair, generation, &cert, &key, proof, true, false,
                           clock_ms, NULL, &pair_ops, NULL) == 0);
  client_ready = false;
  for (int i = 0; i < 2500 && (!client_ready || !server.established); i++)
    {
      assert(bkprov_tls_step(&server) >= 0);
      if (!client_ready)
        {
          ret = mbedtls_ssl_handshake(&client);
          assert(ret == 0 || ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);
          client_ready = ret == 0;
        }
      now += 10;
    }
  assert(client_ready && server.established);
  request(&client, 1, 0, proof, 32); receive_status(&pair, &client, BKPROV_LOCAL);
  assert(pair_commits == 0);
  assert(bkprov_pair_confirm(&pair, generation) == 0);
  receive_status(&pair, &client, BKPROV_READY);
  const uint8_t total[4]={0, 0, 0, 6}, chunk[10]={0, 0, 0, 0, 1, 2, 3, 4, 5, 6};
  /* Authenticated, unclaimed READY can start one read-only scan. Empty SSIDs
   * are suppressed, and result truncation records the source/sanitizer loss. */
  memset(&scan_snapshot, 0, sizeof(scan_snapshot));
  scan_snapshot.found = 26; scan_snapshot.returned = 2;
  scan_snapshot.aps[0].rssi = -44; scan_snapshot.aps[0].channel = 6;
  scan_snapshot.aps[0].security = 8; memcpy(scan_snapshot.aps[0].ssid, "test", 4);
  scan_ready = true;
  request(&client, 6, 1, NULL, 0); receive_scan(&pair, &client);
  request(&client, 2, 1, total, 4);
  for (int i = 0; i < 200; i++)
    { ret = bkprov_pair_step(&pair); now += 10; if (ret < 0) break; }
  assert(ret == -EPROTO && !pair.tls.initialized);
  bkprov_pair_close(&pair);
  struct bkprov_scan_result_s direct_scan;
  memset(&scan_snapshot, 0, sizeof(scan_snapshot));
  scan_snapshot.returned = 1;
  memcpy(scan_snapshot.aps[0].ssid, "owned", 5);
  scan_ready = true;
  assert(bkprov_scan_start() == 0); bkprov_scan_drain();
  assert(bkprov_scan_busy());
  assert(bkprov_scan_poll(&direct_scan) == 0);
  assert(direct_scan.status == 0 && direct_scan.count == 1);
  assert(!bkprov_scan_busy());
  assert(bkprov_scan_start() == 0); bkprov_scan_drain();
  assert(bkprov_scan_busy());
  assert(bkprov_scan_poll(&direct_scan) == -EAGAIN);
  bkprov_scan_close(); assert(bkprov_scan_busy()); scan_ready = true; bkprov_scan_drain();
  assert(!bkprov_scan_busy());
  memset(&scan_snapshot, 0, sizeof(scan_snapshot));
  scan_snapshot.status = -EIO; scan_snapshot.returned = 1;
  memcpy(scan_snapshot.aps[0].ssid, "bad", 3);
  scan_ready = true;
  assert(bkprov_scan_start() == 0 && bkprov_scan_poll(&direct_scan) == 0);
  assert(direct_scan.status == -EIO && direct_scan.count == 0 && !direct_scan.truncated);
  generation++;
  inbound.size = outbound.size = 0;
  assert(mbedtls_ssl_session_reset(&client) == 0);
  assert(bkprov_pair_start(&pair, generation, &cert, &key, proof, true, false,
                           clock_ms, NULL, &pair_ops, NULL) == 0);
  client_ready = false;
  for (int i = 0; i < 2500 && (!client_ready || !server.established); i++)
    {
      assert(bkprov_tls_step(&server) >= 0);
      if (!client_ready) { ret = mbedtls_ssl_handshake(&client);
        assert(ret == 0 || ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
        client_ready = ret == 0; }
      now += 10;
    }
  assert(client_ready && server.established);
  request(&client, 1, 0, proof, 32); receive_status(&pair, &client, BKPROV_LOCAL);
  assert(bkprov_pair_confirm(&pair, generation) == 0);
  receive_status(&pair, &client, BKPROV_READY);
  request(&client, 2, 1, total, 4); receive_status(&pair, &client, BKPROV_RECEIVING);
  request(&client, 3, 2, chunk, 10); receive_status(&pair, &client, BKPROV_RECEIVING);
  request(&client, 4, 3, NULL, 0); receive_status(&pair, &client, BKPROV_CHECKING);
  assert(pair_commits == 0);
  network_verified = true; receive_status(&pair, &client, BKPROV_COMMITTED);
  assert(pair_commits == 1);
  uint8_t selected[6], receipt[16]; size_t selected_size; uint64_t selected_revision;
  assert(bkprov_store_load(&pair_store, selected, sizeof(selected), &selected_size,
                           &selected_revision, receipt) == 0);
  assert(selected_size == 6 && selected_revision == 1 && receipt[0] == 7);
  assert(!memcmp(selected, chunk+4, 6));
  memcpy(saved_transaction, receipt, 16);
  for (int attempt = 0; attempt < 2; attempt++)
    {
      bkprov_pair_close(&pair);
      generation++;
      inbound.size = outbound.size = 0;
      assert(mbedtls_ssl_session_reset(&client) == 0);
      assert(bkprov_pair_start_recovery(&pair, generation, &cert, &key, proof, true,
                                        clock_ms, NULL, read_receipt) == 0);
      client_ready = false;
      for (int i = 0; i < 2500 && (!client_ready || !server.established); i++)
        {
          assert(bkprov_pair_step(&pair) == 0);
          if (!client_ready)
            {
              ret = mbedtls_ssl_handshake(&client);
              assert(ret == 0 || ret == MBEDTLS_ERR_SSL_WANT_READ ||
                     ret == MBEDTLS_ERR_SSL_WANT_WRITE);
              client_ready = ret == 0;
            }
          now += 10;
        }
      assert(client_ready && server.established);
      request(&client, 1, 0, proof, 32); receive_status(&pair, &client, BKPROV_LOCAL);
      assert(bkprov_pair_confirm(&pair, generation) == 0);
      receive_status(&pair, &client, BKPROV_READY);
      if (attempt == 0)
        {
          request(&client, 6, 1, NULL, 0);
          int failed = 0;
          for (int i = 0; i < 200 && !failed; i++)
            { failed = bkprov_pair_step(&pair); now += 10; }
          assert(failed == -EPROTO && !pair.tls.initialized);
        }
      else
        {
          request(&client, 5, 1, NULL, 0);
          receive_status(&pair, &client, BKPROV_COMMITTED);
        }
      assert(pair_commits == 1); /* Recovery never writes configuration. */
    }
  assert(bkprov_tls_queue(&server, message, sizeof(message)) == 0);
  congested = true;
  now += 4990;
  assert(bkprov_tls_step(&server) == 0 && server.pending_size == sizeof(message));
  now += 10;
  assert(bkprov_tls_step(&server) == -ETIMEDOUT);
  assert_wiped(&server);
  bkprov_pair_close(&pair);
  control_encrypted_tests(&client, &cert, &key);
  mbedtls_ssl_free(&client); mbedtls_ssl_config_free(&config);

  generation++;
  inbound.size = outbound.size = 0;
  assert(bkprov_tls_start(&server, generation, &cert, &key, clock_ms, NULL) == 0);
  now += 30000;
  assert(bkprov_tls_step(&server) == -ETIMEDOUT); assert_wiped(&server);
  generation++;
  assert(bkprov_tls_start(&server, generation, &cert, &key, clock_ms, NULL) == 0);
  generation++;
  assert(bkprov_tls_step(&server) == -ESTALE); assert_wiped(&server);
  assert(bkprov_tls_start(&server, generation, &cert, &key, clock_ms, NULL) == 0);
  now--;
  assert(bkprov_tls_step(&server) == -ETIMEDOUT); assert_wiped(&server);
  mbedtls_pk_free(&key); mbedtls_x509_crt_free(&cert);
  mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
  bkprov_pair_close(&pair);
  puts("BKPROV_TLS_HOST_PASS");
  return 0;
}
