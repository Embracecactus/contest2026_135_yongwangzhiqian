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
static int control_starts, control_steps;
static int control_execute(void *p, enum bkcontrol_command_e command,
                            uint32_t value, struct bkcontrol_status_s *status)
{ (void)p; (void)command; (void)value; (void)status; return 0; }
int bkcontrol_pair_start(struct bkcontrol_pair_s *p, uint32_t gen,
                         mbedtls_x509_crt *crt, mbedtls_pk_context *pk,
                         const uint8_t proof[32], uint64_t (*clock)(void *),
                         void *clock_context, bkcontrol_execute_t execute, void *context)
{
  assert(crt == &certificate && pk == &key && proof[0] == 99);
  assert(execute == control_execute && context == &control_steps);
  assert(clock(clock_context) == now);
  p->tls.initialized = true; p->tls.generation = gen;
  control_starts++; return 0;
}
int bkcontrol_pair_step(struct bkcontrol_pair_s *p)
{ control_steps++; return p->tls.generation == generation ? 0 : -ESTALE; }
void bkcontrol_pair_close(struct bkcontrol_pair_s *p) { memset(p,0,sizeof(*p)); }

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
  assert(local && clock(clock_context) == now && receipt == bkprov_storage_receipt);
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
int main(void)
{
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
  (void)sample(1, false, true);
  assert(!window && !bkprov_owner_busy() && bkprov_owner_error() == -ETIMEDOUT);

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
