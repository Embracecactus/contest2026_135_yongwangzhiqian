/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_owner.h"
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_storage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/platform_util.h>

static uint64_t now = 1;
static uint32_t epoch = 1, generation;
static int opens, closes, starts, recoveries, confirmations;
static int store_status = -ENOENT, pair_error, radio_error;
static bool window, pending_disconnect;
static enum bkprov_claim_state_e next_state = BKPROV_AUTH;
static mbedtls_x509_crt certificate;
static mbedtls_pk_context key;
static uint8_t secret[32] = {1};
static int control_starts, control_steps, control_pair_error;
static bool protocol_peer, authenticate_peer;
static unsigned int executed;
static struct bkcontrol_pair_s *peer;
static enum bkcontrol_command_e peer_command = BKCONTROL_STATUS;
static int peer_reply;
static uint32_t sequence;
static void put32(uint8_t *p, uint32_t value)
{ p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value; }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int wire(enum bkcontrol_command_e command, const uint8_t *data, size_t size)
{
  uint8_t request[48] = {0}, response[40];
  memcpy(request, "SDC1", 4); put32(request + 4, command);
  put32(request + 8, sequence); put32(request + 12, size);
  if (size) memcpy(request + 16, data, size);
  int ret = bkcontrol_session_packet(&peer->session, request, 16 + size, response);
  if (ret < 0) return ret;
  assert(get32(response + 8) == sequence++);
  peer_reply = (int32_t)get32(response + 16);
  return 0;
}
static int control_execute(void *p, enum bkcontrol_command_e command,
                            uint32_t value, struct bkcontrol_status_s *status)
{ (void)p; (void)command; (void)value; (void)status; executed++; return 0; }
int bkcontrol_pair_start(struct bkcontrol_pair_s *p, uint32_t gen,
                         mbedtls_x509_crt *crt, mbedtls_pk_context *pk,
                         const uint8_t proof[32], uint64_t (*clock)(void *),
                         void *clock_context, bkcontrol_execute_t execute, void *context)
{
  assert(crt == &certificate && pk == &key && proof[0] == 99);
  assert(execute == control_execute && context == &control_steps);
  assert(clock(clock_context) == now);
  p->tls.initialized = true; p->tls.generation = gen;
  if (protocol_peer)
    {
      peer = p;
      assert(bkcontrol_session_open(&p->session, proof, execute, context) == 0);
      if (authenticate_peer) assert(wire(BKCONTROL_AUTH, proof, 32) == 0);
    }
  control_starts++; return 0;
}
int bkcontrol_pair_step(struct bkcontrol_pair_s *p)
{
  control_steps++;
  if (p->tls.generation != generation) return -ESTALE;
  if (protocol_peer && authenticate_peer)
    {
      uint8_t volume[4]; put32(volume, 50);
      return wire(peer_command, peer_command == BKCONTROL_VOLUME ? volume : NULL,
                  peer_command == BKCONTROL_VOLUME ? 4 : 0);
    }
  return control_pair_error;
}
void bkcontrol_pair_close(struct bkcontrol_pair_s *p) { memset(p, 0, sizeof(*p)); }

void mbedtls_platform_zeroize(void *p, size_t n) { memset(p, 0, n); }
int bkprov_gatt_poll(void) { return radio_error; }
bool bkprov_gatt_open(void) { return window; }
bool bkprov_gatt_idle(void) { return !window && !pending_disconnect; }
uint32_t bkprov_gatt_generation(void) { return generation; }
int bkprov_gatt_window(bool open)
{
  window = open;
  if (open) opens++;
  else { closes++; generation = 0; }
  return radio_error;
}
int bkprov_storage_snapshot(void *out, size_t cap, size_t *size,
                             uint64_t *revision, uint8_t transaction[16])
{
  assert(out && cap == BKPROV_BUNDLE_MAX);
  if (store_status == 0)
    { memset(out, 42, cap); *size = 1; *revision = 1; memset(transaction, 0, 16); }
  return store_status;
}
int bkprov_storage_receipt(const uint8_t transaction[16])
{ (void)transaction; return 1; }
int bkprov_storage_reset_receipt(const uint8_t transaction[16])
{ (void)transaction; return BKPROV_STORAGE_RESET_RECEIPT_ABSENT; }
static int begin(void *c, const uint8_t *b, size_t n)
{ (void)c; (void)b; (void)n; return 0; }
static int poll_trial(void *c) { (void)c; return 0; }
static int commit(void *c, const uint8_t t[16], const uint8_t *b, size_t n)
{ (void)c; (void)t; (void)b; (void)n; return 0; }
static void abort_trial(void *c) { (void)c; }
static const struct bkprov_claim_ops_s ops = {begin, poll_trial, commit, abort_trial};
int bkprov_pair_start(struct bkprov_pair_s *p, uint32_t gen,
                      mbedtls_x509_crt *crt, mbedtls_pk_context *pk,
                      const uint8_t proof[32], bool local, bool claimed,
                      uint64_t (*clock)(void *), void *clock_context,
                      const struct bkprov_claim_ops_s *trial, void *context)
{
  assert(crt == &certificate && pk == &key && !memcmp(proof, secret, 32));
  assert(local && !claimed && trial == &ops && context == &ops);
  assert(clock(clock_context) == now);
  for (size_t i = 0; i < sizeof(p->claim.bundle); i++) assert(p->claim.bundle[i] == 0);
  p->tls.initialized = true;
  p->tls.generation = gen;
  p->claim.state = BKPROV_AUTH;
  starts++;
  return 0;
}
int bkprov_pair_start_recovery(struct bkprov_pair_s *p, uint32_t gen,
                               mbedtls_x509_crt *crt, mbedtls_pk_context *pk,
                               const uint8_t proof[32], bool local,
                               uint64_t (*clock)(void *), void *clock_context,
                               int (*receipt)(const uint8_t[16]))
{
  assert(crt == &certificate && pk == &key && !memcmp(proof, secret, 32));
  assert(local && clock(clock_context) == now && receipt != NULL);
  uint8_t transaction[16] = {0};
  assert(receipt(transaction) == 1);
  p->tls.initialized = true;
  p->tls.generation = gen;
  p->claim.state = BKPROV_AUTH;
  p->recovery = true;
  recoveries++;
  return 0;
}
int bkprov_pair_step(struct bkprov_pair_s *p)
{
  if (p->tls.generation != generation) return -ESTALE;
  p->claim.state = next_state;
  return pair_error;
}
int bkprov_pair_confirm(struct bkprov_pair_s *p, uint32_t gen)
{
  assert(p->claim.state == BKPROV_LOCAL && p->tls.generation == gen);
  p->claim.state = BKPROV_READY;
  confirmations++;
  return 0;
}
void bkprov_pair_close(struct bkprov_pair_s *p) { memset(p, 0, sizeof(*p)); }
static bool sample(unsigned elapsed, bool down, bool idle)
{
  now += elapsed;
  return bkprov_owner_step(now, epoch, false, down, idle);
}
static void gesture(unsigned hold)
{
  (void)sample(20, false, true);
  (void)sample(20, true, true);
  (void)sample(hold, false, true);
}
#ifdef TEST_OWNER_LIBRARY
void test_owner_open(void)
{
  protocol_peer = authenticate_peer = true;
  assert(bkprov_owner_bind(&certificate, &key, secret, &ops, (void *)&ops) == 0);
  uint8_t owner_key[32] = {99};
  assert(bkprov_owner_control(owner_key, control_execute, &control_steps) == 0);
  (void)sample(1, false, true);
  generation = 1;
  (void)sample(1, false, true);
  assert(window && peer->session.authenticated);
}
bool test_owner_window(void) { return window; }
unsigned int test_owner_executed(void) { return executed; }
void test_owner_write(void) { peer_command = BKCONTROL_VOLUME; }
int test_owner_reply(void) { return peer_reply; }
#define main test_owner_legacy_main
#endif

static void test_power_queries(const char *variant)
{
  protocol_peer = true;
  authenticate_peer = strcmp(variant, "unauthenticated") != 0;
  assert(bkprov_owner_bind(&certificate, &key, secret, &ops, (void *)&ops) == 0);
  uint8_t owner_key[32] = {99};
  assert(bkprov_owner_control(owner_key, control_execute, &control_steps) == 0);
  (void)sample(1, false, true);
  generation = 1;
  (void)sample(1, false, true);
  assert(window);
  assert(bkprov_owner_prepare_stop(++now) == 0);
  if (!authenticate_peer)
    {
      assert(!window && executed == 0);
      (void)sample(100, false, true);
      assert(!window);
    }
  else
    {
      assert(window && peer_reply == 0);
      unsigned int before = executed;
      peer_command = BKCONTROL_VOLUME;
      assert(bkprov_owner_prepare_stop(++now) == 0);
      assert(peer_reply == -EBUSY && executed == before);
      peer_command = BKCONTROL_STATUS;
      for (int i = 0; i < 20; i++) assert(bkprov_owner_prepare_stop(++now) == 0);
      assert(executed == before + 20 && window);
      if (!strcmp(variant, "invalid-sequence"))
        {
          sequence++;
          (void)bkprov_owner_prepare_stop(++now);
          assert(!window && bkprov_owner_error() == -EPROTO);
        }
      else
        {
          pending_disconnect = true;
          assert(bkprov_owner_quiesce(true) == -EAGAIN);
          assert(!window);
          assert(bkprov_owner_prepare_stop(++now) == -EAGAIN);
          pending_disconnect = false;
          assert(bkprov_owner_prepare_stop(++now) == 0);
          assert(!window);
        }
    }
  puts("CONTRACT_PASS");
}

int main(int argc, char **argv)
{
  if (argc == 2) { test_power_queries(argv[1]); return 0; }
  assert(argc == 1);
  (void)sample(1, false, true);
  gesture(9000); assert(opens == 0); /* No identity, no radio window. */
  assert(bkprov_owner_bind(&certificate, &key, secret, &ops, (void *)&ops) == 0);
  /* Initial discovery has no PTT/link/epoch gate. */
  assert(!sample(1, true, false) && opens == 0); /* Voice is not idle. */
  assert(sample(1, true, true));
  assert(opens == 1 && bkprov_owner_busy() && starts == 0);
  assert(bkprov_owner_unbind() == -EBUSY);
  generation = 1;
  (void)sample(20, false, true); assert(starts == 1 && confirmations == 0);
  next_state = BKPROV_LOCAL;
  (void)sample(20, false, true); assert(confirmations == 1);
  next_state = BKPROV_READY;
  (void)sample(120000, false, true);
  assert(!bkprov_owner_busy() && bkprov_owner_error() == -ETIMEDOUT);

  /* Existing/corrupt/unavailable snapshots never become unclaimed, and each
   * failed proof backs off for five seconds. */
  int old_opens = opens;
  store_status = 0;
  (void)sample(5000, false, true); assert(opens == old_opens && bkprov_owner_error() == -EACCES);
  (void)sample(100, false, true); assert(opens == old_opens);
  store_status = -EIO;
  (void)sample(5000, false, true); assert(opens == old_opens && bkprov_owner_error() == -EIO);
  store_status = -EAGAIN;
  (void)sample(5000, false, true); assert(opens == old_opens && bkprov_owner_error() == -EAGAIN);
  store_status = -EPROTO;
  (void)sample(5000, false, true); assert(opens == old_opens && bkprov_owner_error() == -EPROTO);
  store_status = -ENOENT;
  (void)sample(5000, false, true); assert(opens == old_opens + 1);
  generation = 2; next_state = BKPROV_AUTH;
  (void)sample(20, false, true); assert(starts == 2);
  epoch++; (void)sample(20, false, true); assert(bkprov_owner_busy());

  /* Actual GATT closure, clock rollback, and TLS errors remain terminal. */
  window = false;
  (void)sample(20, false, true);
  assert(!bkprov_owner_busy() && bkprov_owner_error() == -ENOTCONN);
  (void)sample(5000, false, true); assert(window);
  now--;
  (void)bkprov_owner_step(now, epoch, false, false, true);
  assert(!bkprov_owner_busy());

  (void)sample(5000, false, true); generation = 3;
  pair_error = -EPROTO;
  (void)sample(20, false, true);
  assert(!window && bkprov_owner_error() == -EPROTO);
  pair_error = 0;
  (void)sample(5000, false, true); generation = 4;
  (void)sample(20, false, true);
  generation = 5;
  (void)sample(20, false, true);
  assert(!window && bkprov_owner_error() == -ESTALE);

  /* The legacy receipt path remains physical and does not auto-confirm. */
  store_status = 0;
  (void)sample(5000, false, true);
  gesture(8000); assert(window);
  generation = 6; next_state = BKPROV_LOCAL;
  (void)sample(20, false, true); assert(recoveries == 1 && confirmations == 1);
  next_state = BKPROV_READY;
  (void)sample(120000, false, true); assert(!bkprov_owner_busy());

  assert(bkprov_owner_unbind() == 0);
  assert(bkprov_owner_bind(&certificate, &key, secret, &ops, (void *)&ops) == 0);
  uint8_t owner_key[32] = {99};
  assert(bkprov_owner_control(owner_key, control_execute, &control_steps) == 0);
  owner_key[0] = 0; /* Owner retains an independent committed-key copy. */
  store_status = 0;
  assert(!sample(1, false, true));
  assert(window && bkprov_owner_busy() && !bkprov_owner_pairing());
  /* An unconnected control window still expires.  A valid generation that
   * arrives after the old advertising deadline starts its own TLS window. */
  (void)sample(120000, false, true);
  assert(!window && !bkprov_owner_busy() && bkprov_owner_error() == -ETIMEDOUT);
  (void)sample(5000, false, true); assert(window && bkprov_owner_busy());
  (void)sample(119990, false, true); assert(window && control_starts == 0);
  generation = 7;
  assert(!sample(20, false, false));
  assert(control_starts == 1 && control_steps == 1 && !bkprov_owner_pairing());
  assert(!sample(119999, false, true) && window); /* No input/link requirement. */
  /* The owner window no longer expires an active control session; its own
   * pair idle deadline reports the timeout and must close the window. */
  control_pair_error = -ETIMEDOUT;
  (void)sample(1, false, true);
  assert(!window && !bkprov_owner_busy() && bkprov_owner_error() == -ETIMEDOUT);
  control_pair_error = 0;

  /* Existing disconnect, GATT polling, and TLS-generation cleanup remain terminal. */
  (void)sample(5000, false, true); generation = 8;
  assert(!sample(20, false, true) && window);
  window = false;
  (void)sample(20, false, true);
  assert(!window && bkprov_owner_error() == -ETIMEDOUT);
  (void)sample(5000, false, true); generation = 9;
  assert(!sample(20, false, true) && window);
  assert(bkprov_owner_unbind() == -EBUSY);
  radio_error = -EIO;
  (void)sample(20, false, true); assert(!window && bkprov_owner_error() == -EIO);
  radio_error = 0;
  (void)sample(5000, false, true); generation = 10;
  assert(!sample(20, false, true) && window);
  generation = 11;
  (void)sample(20, false, true); assert(!window && bkprov_owner_error() == -ESTALE);
  assert(bkprov_owner_control(NULL, NULL, NULL) == 0);
  assert(bkprov_owner_unbind() == 0);
  puts("BKPROV_OWNER_PASS: automatic discovery, proof gates, lifecycle");
  return 0;
}
