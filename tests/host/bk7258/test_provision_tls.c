/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_tls.h"
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_pair.h"
#include "bk7258_provision_store.h"
#include "bk7258_control_pair.h"
#include "bk7258_control_serial.h"
#include "bk7258_pc_grants.h"
#include "bk7258_pc_control.h"
#include <sys/stat.h>
#include <arch/chip/bk7258_wifi.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "bk7258_display_job_control.h"

void test_pack_native_start(void);
void test_pack_native_finish(const char *expected);
static bool resource_peer;
static struct bkpack_control_s resource_job;

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

/* A second byte transport deliberately has no GATT generation dependency. */
static uint32_t stream_generation = 7;
static uint32_t stream_epoch(void *context)
{
  assert(context == &stream_generation);
  return stream_generation;
}

static ssize_t stream_read(void *context, uint32_t epoch, void *data,
                           size_t size)
{
  assert(context == &stream_generation && epoch == stream_generation);
  return take(&inbound, data, size);
}

static ssize_t stream_send(void *context, uint32_t epoch, const void *data,
                           size_t size)
{
  assert(context == &stream_generation && epoch == stream_generation);
  assert(size <= 64);
  if (congested) return -EAGAIN;
  int ret = put(&outbound, data, size);
  if (ret > 0) fragments++;
  return ret;
}

#include "bk7258_pc_usb.h"
static struct bkpc_usb_s pc_usb;
static bool serial_wire;

/* Preserve the first unexpected return at every handshake site, including
 * recovery. Reporting only the initial handshake lost the S66 error domain.
 */
static int handshake_checked(mbedtls_ssl_context *client, int line)
{
  int ret = mbedtls_ssl_handshake(client);
  if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ &&
      ret != MBEDTLS_ERR_SSL_WANT_WRITE)
    {
      fprintf(stderr,
              "TLS_HOST_FIRST_ERROR line=%d ret=%d verify=%lu now=%llu "
              "stream=%d serial=%d\n",
              line, ret, (unsigned long)mbedtls_ssl_get_verify_result(client),
              (unsigned long long)now, getenv("SHANIU_TLS_STREAM") != NULL,
              getenv("SHANIU_TLS_SERIAL") != NULL);
      fflush(stderr);
    }
  return ret;
}
void test_serial_peer_open(void);
void test_serial_peer_close(void);
int test_serial_peer_send(const void *, size_t);
int test_serial_peer_recv(void *, size_t);

static int client_send(void *ctx, const unsigned char *data, size_t size)
{
  (void)ctx; int ret = serial_wire ? test_serial_peer_send(data, size) :
                                  put(&inbound, data, size);
  return ret < 0 ? MBEDTLS_ERR_SSL_WANT_WRITE : ret;
}

static int client_recv(void *ctx, unsigned char *data, size_t size)
{
  (void)ctx; int ret = serial_wire ? test_serial_peer_recv(data, size) :
                                  take(&outbound, data, size);
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

static struct bkpc_grants_s pc_grants;
static int pc_sync_fault;
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  struct stat st;
  assert(fstat(fd, &st) == 0);
  if ((pc_sync_fault == 1 && S_ISREG(st.st_mode)) ||
      (pc_sync_fault == 2 && S_ISDIR(st.st_mode)))
    { errno = EIO; return -1; }
  return __real_fsync(fd);
}
#define pc_control pc_usb.lease
static bool pc_guarded;
static unsigned pc_reads, pc_writes;
static int pc_execute(void *context, enum bkcontrol_command_e command,
                       uint32_t value, struct bkcontrol_status_s *status)
{
  assert(context == &pc_reads && value == 0);
  assert(command == BKCONTROL_STATUS || command == BKCONTROL_INFO);
  pc_reads++;
  status->flags = 1;
  if (command == BKCONTROL_INFO)
    {
      status->device_info = (struct bkcontrol_device_info_s){0, 7, 14, 123, 661};
    }
  return 0;
}
#include "bk7258_pc_tasks.h"
static struct bkpc_tasks_s pc_tasks;
static int pc_config(void *context, enum bkcontrol_command_e command,
                      uint32_t kind, uint32_t offset, const uint8_t *record,
                      size_t size, struct bkcontrol_status_s *status)
{
  assert(context == &pc_reads);
  if (kind == BKCONTROL_CONFIG_PC_TASK)
    return bkpc_tasks_control(&pc_tasks, command, offset, record, size, status, now);
  if (kind == BKCONTROL_CONFIG_DEFAULT_SELECTION)
    {
      /* Permission boundary only. Never fabricate native save success. */
      assert(command == BKCONTROL_CONFIG_READ || command == BKCONTROL_CONFIG_BEGIN);
      return -ENOTSUP;
    }
  if (kind == BKCONTROL_CONFIG_RESOURCE_JOB)
    {
      if (resource_peer)
        {
          if (!resource_job.bound)
            {
              uint8_t epoch[16];
              assert(mbedtls_ctr_drbg_random(&pc_control.pair->tls.random,epoch,16)==0);
              int ret=bkpack_control_bind(&resource_job,pc_control.binding,
                       pc_control.revision,pc_control.client,epoch);
              if(ret<0)return ret;
            }
          if(command==BKCONTROL_CONFIG_READ)
            return bkpack_control_read(&resource_job,offset,record,size,status,now);
          if(size<64||size>4160)return -EINVAL;
          if(command==BKCONTROL_CONFIG_BEGIN)return 0;
          if(command!=BKCONTROL_CONFIG_APPLY)return -ENOTSUP;
          return bkpack_control_apply(&resource_job,record,size,now);
        }
      /* Guard boundary only: native job integration has its own real worker
       * fixture. Never return a fabricated installation success here. */
      assert(command == BKCONTROL_CONFIG_READ && record && size == 16);
      return -ENOTSUP;
    }
  assert(offset == 0);
  assert(kind == BKCONTROL_CONFIG_FOCUS || kind == BKCONTROL_CONFIG_EXPRESSION_TRIAL ||
         kind == BKCONTROL_CONFIG_EYE_PACK);
  if (command == BKCONTROL_CONFIG_READ)
    { pc_reads++; status->config_total = 4; memcpy(status->config_chunk, "TEST", 4); }
  else if (command == BKCONTROL_CONFIG_APPLY)
    { assert(record && size == 4 && !memcmp(record, "TEST", 4)); pc_writes++; }
  else assert(command == BKCONTROL_CONFIG_BEGIN && size == 4);
  return 0;
}
static uint64_t pc_binding = 1;
static int pc_source_error;
static int pc_snapshot(void *context, uint64_t *binding,
                         struct bkprov_pc_snapshot_s *view)
{
  assert(context == &pc_grants);
  memset(view, 0, sizeof(*view));
  *binding = 0;
  if (pc_source_error) return pc_source_error;
  int ret = bkpc_grants_snapshot(context, &view->revision, view->client,
                                  &view->capabilities);
  if (!ret && view->capabilities)
    ret = bkpc_grants_key(context, view->revision, view->key, &view->capabilities);
  if (!ret) *binding = pc_binding;
  return ret;
}
static const struct bkpc_source_s pc_source = { &pc_grants, pc_snapshot };

static int control_step(struct bkcontrol_pair_s *pair)
{
  return pc_guarded ? (serial_wire ? bkpc_usb_step(&pc_usb) :
    bkpc_control_step(&pc_control)) : bkcontrol_pair_step(pair);
}
static unsigned control_calls;
static unsigned control_cancels;
static int control_execute(void *context, enum bkcontrol_command_e command,
                           uint32_t value, struct bkcontrol_status_s *status)
{
  (void)context;
  if (command == BKCONTROL_CANCEL)
    {
      control_cancels++;
      status->flags = 2; /* Accepted cancellation, still busy: not drained. */
      return 0;
    }
  assert(command == BKCONTROL_VOLUME && value == 73);
  control_calls++;
  status->flags = 8;
  status->volume = value;
  return 0;
}
static void control_handshake_on(struct bkcontrol_pair_s *control,
                              mbedtls_ssl_context *client,
                              mbedtls_x509_crt *cert, mbedtls_pk_context *key,
                              bool independent)
{
  uint8_t owner[32] = {42};
  assert(bkpc_usb_close(&pc_usb) == 0);
  test_serial_peer_close();
  serial_wire = independent && getenv("SHANIU_TLS_SERIAL") != NULL;
  generation++;
  inbound.size = outbound.size = 0;
  congested = false;
  assert(mbedtls_ssl_session_reset(client) == 0);
  if (independent)
    {
      struct bkprov_tls_transport_s transport =
        { &stream_generation, stream_epoch, stream_read, stream_send, 64, 0 };
      uint32_t granted;
      if (!pc_guarded)
        {
          assert(bkpc_grants_key(&pc_grants, 1, owner, &granted) == 0);
          assert(granted == 3);
        } /* Actual private store supplies this principal. */
      stream_generation++;
      uint32_t selected_generation = stream_generation;
      if (serial_wire)
        {
          test_serial_peer_open();
          if (!pc_guarded)
            {
              assert(bkcontrol_serial_open(&pc_usb.serial, &transport) == 0);
              selected_generation = transport.generation(transport.context);
            }
        }
      if (pc_guarded)
        {
          struct bkpc_source_s loaned = pc_source;
          if (serial_wire)
            {
              uint32_t previous = pc_usb.serial.epoch;
              assert(bkpc_usb_open(&pc_usb, control, &loaned, cert, key,
                       clock_ms, NULL, pc_execute, pc_config, &pc_reads)==0);
              assert(pc_usb.serial.opened && pc_usb.serial.epoch == previous+1);
              assert(bkpc_usb_open(&pc_usb, control, &loaned, cert, key,
                       clock_ms, NULL, pc_execute, pc_config, &pc_reads)==-EBUSY);
            }
          else
            assert(bkpc_control_start(&pc_control, control, &loaned,
                       selected_generation, cert, key, clock_ms, NULL,
                       pc_execute, pc_config, &pc_reads, &transport) == 0);
          memset(&loaned, 0, sizeof(loaned));
        }
      else
        assert(bkcontrol_pair_start_transport(control, selected_generation, cert,
                 key, owner, clock_ms, NULL, control_execute, NULL,
                 &transport) == 0);
      memset(&transport, 0, sizeof(transport));
      generation++;
    }
  else
    {
      assert(bkcontrol_pair_start(control, generation, cert, key, owner,
                                 clock_ms, NULL, control_execute, NULL) == 0);
    }
  memset(owner, 0, sizeof(owner));
  bool ready = false;
  for (int i = 0; i < 2500 && (!ready || !control->tls.established); i++)
    {
      assert(control_step(control) == 0);
      if (!ready)
        {
          int ret = mbedtls_ssl_handshake(client);
          if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ &&
              ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
              fprintf(stderr,
                      "CONTROL_HANDSHAKE_ERROR ret=%d verify=%u "
                      "independent=%d serial=%d pc=%d iteration=%d "
                      "now=%llu server=%d inbound=%zu outbound=%zu\n",
                      ret, (unsigned)mbedtls_ssl_get_verify_result(client),
                      independent, serial_wire, pc_guarded, i,
                      (unsigned long long)now, control->tls.established,
                      inbound.size, outbound.size);
            }
          assert(ret == 0 || ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);
          ready = ret == 0;
        }
      now += 10;
    }
  assert(ready && control->tls.established);
}
static void control_handshake(struct bkcontrol_pair_s *control,
                              mbedtls_ssl_context *client,
                              mbedtls_x509_crt *cert, mbedtls_pk_context *key)
{
  control_handshake_on(control, client, cert, key, false);
}

static void control_response(struct bkcontrol_pair_s *control,
                             mbedtls_ssl_context *client, uint8_t command,
                             uint8_t sequence, uint8_t volume)
{
  uint8_t response[40]; size_t size = 0;
  for (int i = 0; i < 200 && size < sizeof(response); i++)
    {
      assert(control_step(control) == 0);
      int ret = mbedtls_ssl_read(client, response + size, sizeof(response) - size);
      assert(ret > 0 || ret == MBEDTLS_ERR_SSL_WANT_READ);
      if (ret > 0) size += ret;
      now += 10;
    }
  assert(size == sizeof(response) && !memcmp(response, "SDC1", 4));
  assert(response[4] == 128 && response[7] == command && response[11] == sequence);
  assert(response[15] == 24 && response[19] == 0 && response[27] == volume);
  if (command == 3)
    {
      assert(response[23] == 2);
      for (size_t i = 32; i < 36; i++) assert(response[i] == 255);
    }
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
    { assert(control_step(&control) == 0); now += 10; }
  assert(control_calls == 1);
  congested = false;
  control_response(&control, client, 4, 1, 73);
  assert(control_calls == 1); /* Backpressure never repeats the mutation. */
  assert(mbedtls_ssl_write(client, volume, sizeof(volume)) == sizeof(volume));
  int error = 0;
  for (int i = 0; i < 200 && !error; i++)
    { error = control_step(&control); now += 10; }
  assert(error == -EPROTO && control_calls == 1 && !control.tls.initialized);

  control_handshake(&control, client, cert, key);
  auth[16] ^= 1;
  assert(mbedtls_ssl_write(client, auth, sizeof(auth)) == sizeof(auth));
  error = 0;
  for (int i = 0; i < 200 && !error; i++)
    { error = control_step(&control); now += 10; }
  assert(error < 0 && control_calls == 1 && !control.tls.initialized);

  control_handshake(&control, client, cert, key);
  now += 10000;
  assert(control_step(&control) == -ETIMEDOUT);
  const unsigned char *bytes = (const unsigned char *)&control;
  for (size_t i = 0; i < sizeof(control); i++) assert(bytes[i] == 0);
}

/* Real TLS + control parser/session; only the endpoint action is observed.
 * These tests do not grant/store/revoke a real PC credential or use USB.
 */
static void control_terminal(struct bkcontrol_pair_s *control, int expected)
{
  int error = 0;
  for (int i = 0; i < 200 && error == 0; i++)
    {
      error = control_step(control);
      now += 10;
    }
  assert(error == expected);
  const unsigned char *bytes = (const unsigned char *)control;
  for (size_t i = 0; i < sizeof(*control); i++) assert(bytes[i] == 0);
  assert(control_step(control) == -ENOTCONN);
  if (pc_guarded && serial_wire) assert(!pc_usb.serial.opened);
}

static void control_stream_tests(mbedtls_ssl_context *client,
                                 mbedtls_x509_crt *cert,
                                 mbedtls_pk_context *key)
{
  struct bkcontrol_pair_s control = {0};
  uint8_t auth[48] = {'S', 'D', 'C', '1', 0, 0, 0, 1, 0, 0, 0, 0,
                      0, 0, 0, 32, 84};
  uint8_t volume[20] = {'S', 'D', 'C', '1', 0, 0, 0, 4, 0, 0, 0, 1,
                        0, 0, 0, 4, 0, 0, 0, 73};
  const uint8_t spv1[16] = {'S', 'P', 'V', '1'};
  const uint8_t cancel[16] = {'S', 'D', 'C', '1', 0, 0, 0, 3,
                               0, 0, 0, 2, 0, 0, 0, 0};
  const uint8_t oversized[16] = {'S', 'D', 'C', '1', 0, 0, 0, 17,
                                  0, 0, 0, 1, 0, 0, 2, 1};
  unsigned before = control_calls;

  struct bkprov_tls_transport_s transport =
    { &stream_generation, stream_epoch, stream_read, stream_send, 64, 0 };
  uint8_t principal[32] = {84};
  assert(bkcontrol_pair_start_transport(&control, stream_generation, cert,
           key, principal, clock_ms, NULL, control_execute, NULL, NULL) == -EINVAL);
  assert(control_step(&control) == -ENOTCONN);
  assert(bkcontrol_pair_start_transport(&control, stream_generation + 1,
           cert, key, principal, clock_ms, NULL, control_execute, NULL,
           &transport) == -ESTALE);
  const unsigned char *closed = (const unsigned char *)&control;
  for (size_t i = 0; i < sizeof(control); i++) assert(closed[i] == 0);

  /* TLS alone cannot authorize a command. */
  control_handshake_on(&control, client, cert, key, true);
  volume[11] = 0;
  assert(mbedtls_ssl_write(client, volume, sizeof(volume)) == sizeof(volume));
  control_terminal(&control, -EPROTO);
  assert(control_calls == before);
  volume[11] = 1;

  /* A valid phone credential does not authenticate the separate principal. */
  control_handshake_on(&control, client, cert, key, true);
  auth[16] = 42;
  assert(mbedtls_ssl_write(client, auth, sizeof(auth)) == sizeof(auth));
  control_terminal(&control, -EACCES);
  assert(control_calls == before);
  auth[16] = 84;

  control_handshake_on(&control, client, cert, key, true);
  assert(mbedtls_ssl_write(client, spv1, sizeof(spv1)) == sizeof(spv1));
  control_terminal(&control, -EPROTO);
  assert(control_calls == before);

  /* The existing SDC1 sequence and one-response-at-a-time contract applies. */
  control_handshake_on(&control, client, cert, key, true);
  for (size_t i = 0; i < sizeof(auth); i++)
    assert(mbedtls_ssl_write(client, auth + i, 1) == 1);
  control_response(&control, client, 1, 0, 255);
  assert(control_calls == before);
  assert(mbedtls_ssl_write(client, volume, sizeof(volume)) == sizeof(volume));
  congested = true;
  for (int i = 0; i < 50; i++)
    { assert(control_step(&control) == 0); now += 10; }
  assert(control_calls == before + 1);
  congested = false;
  control_response(&control, client, 4, 1, 73);
  assert(control_calls == before + 1);
  unsigned previous_cancels = control_cancels;
  assert(mbedtls_ssl_write(client, cancel, sizeof(cancel)) == sizeof(cancel));
  control_response(&control, client, 3, 2, 255);
  assert(control_cancels == previous_cancels + 1);
  assert(mbedtls_ssl_write(client, volume, sizeof(volume)) == sizeof(volume));
  control_terminal(&control, -EPROTO);
  assert(control_calls == before + 1);

  /* Revocation is supplied by the owning transport, not a BLE epoch. Queued
   * plaintext from the revoked connection must never reach an endpoint.
   */
  control_handshake_on(&control, client, cert, key, true);
  assert(mbedtls_ssl_write(client, auth, sizeof(auth)) == sizeof(auth));
  control_response(&control, client, 1, 0, 255);
  assert(mbedtls_ssl_write(client, volume, sizeof(volume)) == sizeof(volume));
  if (serial_wire) assert(bkcontrol_serial_close(&pc_usb.serial) == 0);
  else stream_generation++;
  control_terminal(&control, -ESTALE);
  assert(control_calls == before + 1);

  control_handshake_on(&control, client, cert, key, true);
  assert(mbedtls_ssl_write(client, auth, sizeof(auth)) == sizeof(auth));
  control_response(&control, client, 1, 0, 255);
  assert(mbedtls_ssl_write(client, oversized, sizeof(oversized)) == sizeof(oversized));
  control_terminal(&control, -EPROTO);
  assert(control_calls == before + 1);

  control_handshake_on(&control, client, cert, key, true);
  now += 10000;
  control_terminal(&control, -ETIMEDOUT);
  assert(control_calls == before + 1);

  /* Existing phone-only SPV1 scan remains available with its own possession
   * proof. This is a real demultiplex/claim path, not a successful mock.
   */
  const uint8_t proof[32] = {9};
  control_handshake(&control, client, cert, key);
  memcpy(control.scan_secret, proof, sizeof(proof));
  request(client, 1, 0, proof, sizeof(proof));
  for (int i = 0; i < 100 && control.scan == NULL; i++)
    { assert(control_step(&control) == 0); now += 10; }
  assert(control.scan != NULL);
  receive_status(control.scan, client, BKPROV_READY);
  assert(control_calls == before + 1);
  bkcontrol_pair_close(&control);
}

/* Real PC lease + store + TLS + SDC1; service callbacks are independent
 * side-effect observers, not replacement authorization/transfer machines.
 */
static uint8_t pc_response[40];
static void pc_exchange(struct bkcontrol_pair_s *control, mbedtls_ssl_context *client,
                         uint32_t command, uint32_t sequence,
                         const uint8_t *payload, size_t size, int expected)
{
  uint8_t frame[528] = {'S', 'D', 'C', '1'}, response[40];
  assert(size <= sizeof(frame) - 16);
  for (unsigned int i = 0; i < 4; i++)
    {
      frame[4+i] = command >> (24-i*8);
      frame[8+i] = sequence >> (24-i*8);
      frame[12+i] = size >> (24-i*8);
    }
  if (size) memcpy(frame + 16, payload, size);
  assert(mbedtls_ssl_write(client, frame, 16 + size) == (int)(16 + size));
  size_t received = 0;
  for (unsigned int i = 0; i < 200 && received < sizeof(response); i++)
    {
      int step_result = control_step(control);
      if (step_result != 0)
        fprintf(stderr, "PC_STEP_ERROR command=%u sequence=%u ret=%d now=%llu\n",
                command, sequence, step_result, (unsigned long long)now);
      assert(step_result == 0);
      int ret = mbedtls_ssl_read(client, response + received, sizeof(response) - received);
      assert(ret > 0 || ret == MBEDTLS_ERR_SSL_WANT_READ);
      if (ret > 0) received += ret;
      now += 10;
    }
  assert(received == 40 && !memcmp(response, "SDC1", 4));
  uint32_t actual = (uint32_t)response[16]<<24 | (uint32_t)response[17]<<16 |
                    (uint32_t)response[18]<<8 | response[19];
  if (actual != (uint32_t)expected)
    fprintf(stderr,"PC_RESPONSE_ERROR command=%u sequence=%u actual=%d expected=%d\n",
            command,sequence,(int32_t)actual,expected);
  assert(actual == (uint32_t)expected);
  memcpy(pc_response, response, sizeof(pc_response));
}
static void pc_change_result(uint32_t caps, int expected)
{
  uint64_t revision; uint32_t old; uint8_t id[16], tx[16] = {0};
  const uint8_t client[16] = {7}, key[32] = {84};
  assert(bkpc_grants_snapshot(&pc_grants, &revision, id, &old) == 0);
  assert(revision < 200); tx[0] = (uint8_t)(revision + 100);
  assert(bkpc_grants_set(&pc_grants, revision, tx, caps ? client : NULL,
                         caps ? key : NULL, caps) == expected);
}
static void pc_change(uint32_t caps)
{
  pc_change_result(caps, 0);
}
static void pc_owner_lifecycle_tests(mbedtls_x509_crt *cert, mbedtls_pk_context *key)
{
  if (!getenv("SHANIU_TLS_SERIAL")) return;
  struct bkpc_usb_owner_s owner={0};
  const struct bkpc_usb_config_s cfg={&pc_source,cert,key,clock_ms,NULL,
                                     pc_execute,pc_config,&pc_reads};
  test_serial_peer_open();
  assert(bkpc_usb_owner_step(&owner,&cfg,false,true)==0);
  assert(!owner.pair && !owner.usb.serial.opened);
  assert(bkpc_usb_owner_step(&owner,&cfg,true,false)==0);
  assert(!owner.pair && !owner.usb.serial.opened);
  assert(bkpc_usb_owner_step(&owner,&cfg,true,true)==0);
  assert(owner.pair && owner.pair->tls.initialized && owner.usb.serial.opened);
  uint32_t epoch=owner.usb.serial.epoch;
  assert(bkpc_usb_owner_step(&owner,&cfg,true,true)==0);
  assert(owner.usb.serial.epoch==epoch);
  pc_source_error=-EAGAIN;
  assert(bkpc_usb_owner_step(&owner,&cfg,true,true)==-EAGAIN);
  assert(!owner.pair && !owner.usb.serial.opened);
  pc_source_error=0;
  assert(bkpc_usb_owner_step(&owner,&cfg,true,true)==-EAGAIN);
  assert(!owner.pair && owner.usb.serial.epoch==epoch);
  now+=1000;
  assert(bkpc_usb_owner_step(&owner,&cfg,true,true)==0);
  assert(owner.pair && owner.usb.serial.epoch==epoch+1);
  assert(bkpc_usb_owner_step(&owner,NULL,false,false)==0);
  assert(!owner.pair && !owner.usb.serial.opened);
  assert(bkpc_usb_owner_stop(&owner)==0);
  now--;
  assert(bkpc_usb_owner_step(&owner,&cfg,true,true)==-ETIMEDOUT);
  assert(!owner.pair && !owner.usb.serial.opened);
  now+=2000;
  test_serial_peer_close();
  assert(bkpc_usb_owner_step(&owner,&cfg,true,true)<0);
  assert(!owner.pair && !owner.usb.serial.opened);
  assert(bkpc_usb_owner_stop(&owner)==0);
}

static void pc_guard_tests(mbedtls_ssl_context *client,
                            mbedtls_x509_crt *cert, mbedtls_pk_context *key)
{
  struct bkcontrol_pair_s control = {0};
  const uint8_t auth[32] = {84}, volume[4] = {0,0,0,73};
  const uint8_t focus[4] = {0,10,0,0}, eye[4] = {0,5,0,0};
  const uint8_t begin[8] = {0,0,0,10,0,0,0,4};
  const uint8_t denied_kinds[] = {1,2,3,4,6,7,8,9,12,13,14,15};
  pc_owner_lifecycle_tests(cert,key);
  pc_guarded = true;
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  uint32_t seq = 1;
  pc_exchange(&control, client, 2, seq++, NULL, 0, 0);
  pc_exchange(&control, client, 9, seq++, NULL, 0, 0);
  pc_exchange(&control, client, 4, seq++, volume, 4, -EACCES);
  pc_exchange(&control, client, 3, seq++, NULL, 0, -EACCES);
  pc_exchange(&control, client, 6, seq++, NULL, 0, -EACCES);
  for (size_t i = 0; i < sizeof(denied_kinds); i++)
    {
      uint8_t read[4] = {0,denied_kinds[i],0,0};
      uint8_t write[8] = {0,0,0,denied_kinds[i],0,0,0,4};
      pc_exchange(&control, client, 15, seq++, read, 4, -EACCES);
      pc_exchange(&control, client, 16, seq++, write, 8, -EACCES);
    }
  assert(pc_reads == 2 && pc_writes == 0);
  pc_exchange(&control, client, 15, seq++, focus, 4, 0);
  pc_exchange(&control, client, 15, seq++, eye, 4, 0);
  const uint8_t resource_query[20] = {0,16,0,0,1};
  pc_exchange(&control, client, 15, seq++, resource_query, 20, -ENOTSUP);
  const uint8_t selection_query[20]={0,17,0,0,1};
  const uint8_t selection_begin[8]={0,0,0,17,0,0,0,96};
  pc_exchange(&control,client,15,seq++,selection_query,20,-ENOTSUP);
  pc_exchange(&control,client,16,seq++,selection_begin,8,-ENOTSUP);
  pc_exchange(&control, client, 16, seq++, begin, 8, 0);
  pc_exchange(&control, client, 17, seq++, (const uint8_t *)"TEST", 4, 0);
  pc_exchange(&control, client, 18, seq++, NULL, 0, 0);
  assert(pc_reads == 4 && pc_writes == 1);
  const uint8_t ota[4] = {0,0,0,44};
  pc_exchange(&control, client, 10, seq++, ota, 4, -ENOTSUP);
  /* Revoke while a valid application record remains queued in encrypted I/O. */
  uint8_t queued[20] = {'S','D','C','1',0,0,0,15,0,0,0,0,0,0,0,4,0,10,0,0};
  queued[11] = seq;
  assert(mbedtls_ssl_write(client, queued, sizeof(queued)) == sizeof(queued));
  pc_change(0);
  control_terminal(&control, -ESTALE);
  assert(pc_reads == 4 && pc_writes == 1);
  assert(!pc_control.open);
  /* Explicit regrant cannot resume or replay the old session. */
  pc_change(BKPC_CAP_DIAGNOSTICS);
  assert(control_step(&control) == -ENOTCONN);
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  pc_exchange(&control, client, 15, 1, focus, 4, -EACCES);
  pc_exchange(&control, client, 16, 2, begin, 8, -EACCES);
  pc_exchange(&control, client, 15, 3, resource_query, 20, -EACCES);
  pc_exchange(&control,client,15,4,selection_query,20,-EACCES);
  pc_exchange(&control,client,16,5,selection_begin,8,-EACCES);
  assert(pc_reads == 4 && pc_writes == 1);
  pc_change(BKPC_CAP_SCENES); /* Changing scope also invalidates current AUTH. */
  control_terminal(&control, -ESTALE);
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  pc_exchange(&control,client,15,1,selection_query,20,-EACCES);
  pc_exchange(&control,client,16,2,selection_begin,8,-EACCES);
  pc_exchange(&control, client, 16, 3, begin, 8, 0);
  pc_exchange(&control, client, 17, 4, (const uint8_t *)"TEST", 4, 0);
  pc_change(0); /* In-flight staging is destroyed, not applied on reconnect. */
  control_terminal(&control, -ESTALE);
  assert(pc_writes == 1);
  /* An already accepted action is not undone/repeated when its ACK is lost. */
  pc_change(BKPC_CAP_SCENES);
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  pc_exchange(&control, client, 16, 1, begin, 8, 0);
  pc_exchange(&control, client, 17, 2, (const uint8_t *)"TEST", 4, 0);
  const uint8_t apply[16] = {'S','D','C','1',0,0,0,18,0,0,0,3,0,0,0,0};
  assert(mbedtls_ssl_write(client, apply, sizeof(apply)) == sizeof(apply));
  for (int i = 0; i < 200 && pc_writes == 1; i++)
    { assert(control_step(&control) == 0); now += 10; }
  assert(pc_writes == 2 && control.report);
  pc_change(0);
  control_terminal(&control, -ESTALE);
  assert(pc_writes == 2);
  struct bkprov_tls_transport_s transport =
    { &stream_generation, stream_epoch, stream_read, stream_send, 64, 0 };
  assert(bkpc_control_start(&pc_control, &control, &pc_source,
             stream_generation, cert, key, clock_ms, NULL,
             pc_execute, pc_config, &pc_reads, &transport) == -EACCES);
  assert(!pc_control.open && !control.tls.initialized);
  if (serial_wire)
    {
      assert(bkpc_usb_open(&pc_usb,&control,&pc_source,cert,key,clock_ms,NULL,
                           pc_execute,pc_config,&pc_reads)==-EACCES);
      assert(!pc_usb.serial.opened && !pc_control.open && !control.tls.initialized);
      assert(bkpc_usb_close(&pc_usb)==0 && bkpc_usb_close(&pc_usb)==0);
    }
  /* A failed revoke is not a successful revoke. Unknown durability instead
   * closes the active connection and forbids a fresh AUTH in this process. */
  pc_change(BKPC_CAP_SCENES);
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  pc_change(BKPC_CAP_SCENES); /* Same key and scope, a new authorization revision. */
  control_terminal(&control, -ESTALE);
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  /* Same persisted grant under a new primary configuration is a new lease. */
  pc_binding++;
  control_terminal(&control, -ESTALE);
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  /* A worker publication interval is unavailable, never stale authorization. */
  pc_source_error = -EAGAIN;
  control_terminal(&control, -EAGAIN);
  pc_source_error = 0;
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  pc_change(BKPC_CAP_TASKS);
  control_terminal(&control, -ESTALE);
  control_handshake_on(&control, client, cert, key, true);
  pc_exchange(&control, client, 1, 0, auth, 32, 0);
  bkpc_tasks_bind(&pc_tasks, pc_binding, 1);
  uint8_t task[40] = {'P','T','E','1',0,0,0,1,7};
  task[31]=1; task[34]=0x27; task[35]=0x10;
  const uint8_t task_begin[8]={0,0,0,15,0,0,0,40};
  const uint8_t task_read[4]={0,15,0,0};
  uint32_t task_sequence=1;
  for (int event=0;event<3;event++)
    {
      task[7]=event==0?1:event==1?3:2;
      task[31]=(uint8_t)(event+1);
      task[39]=event==0?0:100;
      pc_exchange(&control,client,16,task_sequence++,task_begin,8,0);
      pc_exchange(&control,client,17,task_sequence++,task,40,0);
      pc_exchange(&control,client,18,task_sequence++,NULL,0,event==2?-EALREADY:0);
      pc_exchange(&control,client,15,task_sequence++,task_read,4,0);
      assert(!memcmp(pc_response+24,"PTS1",4));
      assert(pc_response[31]==(event==0?1:3));
    }
  assert(bkcontrol_session_quiesce(&control.session)==0);
  pc_exchange(&control,client,15,task_sequence++,task_read,4,0);
  pc_exchange(&control,client,16,task_sequence++,task_begin,8,-EBUSY);
  pc_sync_fault = 1;
  pc_change_result(0, -EIO);
  pc_sync_fault = 0;
  pc_exchange(&control, client, 2, task_sequence, NULL, 0, 0);
  assert(pc_reads == 5);
  pc_sync_fault = 2;
  pc_change_result(0, -EINPROGRESS);
  pc_sync_fault = 0;
  control_terminal(&control, -EINPROGRESS);
  assert(bkpc_control_start(&pc_control, &control, &pc_source,
             stream_generation, cert, key, clock_ms, NULL,
             pc_execute, pc_config, &pc_reads, &transport) == -EINPROGRESS);
  assert(pc_writes == 2 && !pc_control.open);
  if (serial_wire)
    {
      /* External descriptor loss: real close reports EBADF. The lifecycle
       * cannot turn uncertain ownership into successful close or reopen. */
      struct bkpc_usb_s failed={0};
      assert(bkcontrol_serial_open(&failed.serial,&transport)==0);
      assert(close(failed.serial.fd)==0);
      assert(bkpc_usb_close(&failed)==-EBADF);
      assert(bkpc_usb_close(&failed)==-EBADF);
      assert(bkpc_usb_open(&failed,&control,&pc_source,cert,key,clock_ms,NULL,
                           pc_execute,pc_config,&pc_reads)==-EBADF);
      assert(!failed.serial.opened && !control.tls.initialized);
    }
  pc_guarded = false;
}

/* External ciphertext pipe / Unix-socket peer for production TLS clients.
 * Only synthetic owner state and local host transport are used; no hardware.
 */
static int control_pipe_peer(const char *certificate, const char *private_key,
                             const char *pc_root, const char *socket_path,
                             int connections)
{
  int listener=-1, wire_in=STDIN_FILENO, wire_out=STDOUT_FILENO;
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
  if(socket_path)
    {
      struct sockaddr_un address={.sun_family=AF_UNIX};
      assert(strlen(socket_path)<sizeof(address.sun_path));
      strcpy(address.sun_path,socket_path);
      listener=socket(AF_UNIX,SOCK_STREAM,0);
      assert(listener>=0 && bind(listener,(struct sockaddr *)&address,sizeof(address))==0);
      assert(listen(listener,2)==0);
      test_pack_native_start();
    }
  for(int connection=0;connection<connections;connection++)
    {
      if(socket_path)
        {
          wire_in=accept(listener,NULL,NULL);wire_out=wire_in;
          assert(wire_in>=0 && fcntl(wire_in,F_SETFL,O_NONBLOCK)==0);
          memset(&inbound,0,sizeof(inbound));memset(&outbound,0,sizeof(outbound));
          stream_generation++;
        }
  clock_gettime(CLOCK_MONOTONIC, &time);
  now = (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
  if (pc_root)
    {
      const uint8_t pc[32] = {84}, client[16] = {7}, transaction[16] = {99};
      struct bkprov_tls_transport_s transport =
        {&stream_generation, stream_epoch, stream_read, stream_send, 64, 0};
      if(connection==0)
        {
          assert(bkpc_grants_open(&pc_grants, pc_root, owner) == 0);
          assert(bkpc_grants_set(&pc_grants, 0, transaction, client, pc, 7) == 0);
        }
      bkpc_tasks_bind(&pc_tasks, pc_binding, 1);
      pc_guarded = true;
      assert(bkpc_control_start(&pc_control, &control, &pc_source,
                               stream_generation, &cert, &key, clock_ms, NULL,
                               pc_execute, pc_config, &pc_reads,
                               &transport) == 0);
    }
  else
    {
      assert(bkcontrol_pair_start(&control, generation, &cert, &key, owner,
                                 clock_ms, NULL, control_execute, NULL) == 0);
    }
  for (;;)
    {
      uint8_t bytes[20];
      ssize_t count = read(wire_in, bytes, sizeof(bytes));
      if (count == 0) break;
      if (count < 0 && errno != EAGAIN && errno != EINTR) { result = 2; break; }
      if (count > 0 && put(&inbound, bytes, (size_t)count) != count) { result = 3; break; }
      clock_gettime(CLOCK_MONOTONIC, &time);
      now = (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
      if (control_step(&control) < 0) { result = 4; break; }
      if (outbound.size)
        {
          count = write(wire_out, outbound.data, outbound.size < 20 ? outbound.size : 20);
          if (count < 0 && errno != EAGAIN && errno != EINTR) { result = 5; break; }
          if (count > 0)
            { outbound.size -= count; memmove(outbound.data, outbound.data + count, outbound.size); }
        }
      struct timespec pause = {0, 1000000};
      nanosleep(&pause, NULL);
    }
  if (pc_guarded) bkpc_control_close(&pc_control);
  else bkcontrol_pair_close(&control);
      if(socket_path)assert(close(wire_in)==0);
      if(result)break;
    }
  if(socket_path)
    {
      char expected[512];snprintf(expected,sizeof(expected),"%s/expected.bkep",pc_root);
      test_pack_native_finish(expected);
      assert(close(listener)==0 && unlink(socket_path)==0);
      /* A rejected principal closes its stream before any resource command. */
      if(result==4 && !resource_job.bound)result=0;
    }
  mbedtls_pk_free(&key); mbedtls_x509_crt_free(&cert);
  mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
  return result;
}

int main(int argc, char **argv)
{
  if (argc == 4 && !strcmp(argv[1], "--control-peer"))
    return control_pipe_peer(argv[2], argv[3], NULL, NULL, 1);
  if (argc == 5 && !strcmp(argv[1], "--pc-peer"))
    return control_pipe_peer(argv[2], argv[3], argv[4], NULL, 1);
  if(argc==7 && !strcmp(argv[1],"--pc-resource-peer"))
    {
      int connections=atoi(argv[6]);assert(connections==1||connections==2);
      resource_peer=true;
      return control_pipe_peer(argv[2],argv[3],argv[4],argv[5],connections);
    }
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
  if (getenv("SHANIU_TLS_STREAM"))
    {
      struct bkprov_tls_transport_s transport =
        { &stream_generation, stream_epoch, stream_read, stream_send, 64, 0 };
      assert(bkprov_tls_start_transport(&server, stream_generation, &cert,
               &key, clock_ms, NULL, &transport) == 0);
      /* Start copies the callbacks/configuration, not this stack descriptor.
       * A BLE disconnect cannot revoke the independent stream's generation.
       */
      memset(&transport, 0, sizeof(transport));
      generation++;
    }
  else
    {
      assert(bkprov_tls_start(&server, 1, &cert, &key, clock_ms, NULL) == 0);
    }
  assert(bkprov_tls_queue(&server, "early", 5) == -EAGAIN);
  for (int i = 0; i < 2500 && (!client_ready || !server.established); i++)
    {
      congested = i < 30;
      ret = bkprov_tls_step(&server); assert(ret >= 0);
      if (!client_ready)
        {
          ret = handshake_checked(&client, __LINE__);
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
  if (getenv("SHANIU_TLS_STREAM"))
    {
      struct bkprov_tls_transport_s transport =
        { &stream_generation, stream_epoch, stream_read, stream_send, 64, 0 };
      stream_generation++;
      assert(bkprov_tls_start_transport(&server, stream_generation, &cert,
               &key, clock_ms, NULL, &transport) == 0);
      stream_generation++;
      assert(bkprov_tls_step(&server) == -ESTALE);
      assert_wiped(&server);
    }

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
          ret = handshake_checked(&client, __LINE__);
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
      if (!client_ready) { ret = handshake_checked(&client, __LINE__);
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
              ret = handshake_checked(&client, __LINE__);
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
  if (getenv("SHANIU_TLS_STREAM"))
    {
      char root[256];
      const uint8_t phone[32] = {42}, pc[32] = {84}, client_id[16] = {7};
      const uint8_t transaction[16] = {99};
      assert(snprintf(root, sizeof(root), "%s/pc", argv[3]) < (int)sizeof(root));
      assert(mkdir(root, 0700) == 0);
      assert(bkpc_grants_open(&pc_grants, root, phone) == 0);
      assert(bkpc_grants_set(&pc_grants, 0, transaction, client_id, pc, 3) == 0);
      control_stream_tests(&client, &cert, &key);
      pc_guard_tests(&client, &cert, &key);
    }
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
