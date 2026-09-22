/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_control_pair.h"
#include "bk7258_provision_pair.h"
#include "bk7258_provision_storage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static uint64_t clock_ms;
static uint8_t wire[128], output[40];
static size_t used, offset, fragment = 1;
static int queue_wait, step_error, start_error;
static unsigned executions, queued, reads, promotions, touches;
static uint64_t now(void *p) { (void)p; return clock_ms; }
void mbedtls_platform_zeroize(void *p, size_t n)
{ volatile unsigned char *b = p; while (n--) *b++=0; }
int bkprov_tls_start(struct bkprov_tls_s *t, uint32_t g, mbedtls_x509_crt *c,
                    mbedtls_pk_context *k, uint64_t (*n)(void *), void *p)
{
  (void)c; (void)k;
  if (start_error) return start_error;
  t->initialized = true; t->generation = g; t->now_ms = n; t->clock_context = p;
  return 0;
}
void bkprov_tls_close(struct bkprov_tls_s *t) { memset(t, 0, sizeof(*t)); }
int bkprov_tls_step(struct bkprov_tls_s *t)
{
  if (step_error) return step_error;
  t->established = true;
  if (t->pending_size) { t->pending_size = 0; return 0; }
  return 1;
}
int bkprov_tls_queue(struct bkprov_tls_s *t, const void *p, size_t n)
{
  if (queue_wait) { queue_wait--; return -EAGAIN; }
  assert(n == 40); memcpy(output, p, n); queued++; t->pending_size = n; return 0;
}
ssize_t bkprov_tls_read(struct bkprov_tls_s *t, void *p, size_t n)
{
  (void)t; reads++;
  if (offset == used) return -EAGAIN;
  if (n>fragment) n = fragment;
  if (n>used-offset) n = used-offset;
  memcpy(p, wire+offset, n); offset += n; return n;
}
int bkprov_tls_promote_control(struct bkprov_tls_s *t)
{
  assert(t->established && !t->control);
  t->control = true; promotions++; return 0;
}
int bkprov_tls_touch_control(struct bkprov_tls_s *t)
{
  assert(t->established && t->control);
  touches++; return 0;
}
int bkprov_pair_step(struct bkprov_pair_s *p)
{ (void)p; assert(!"unexpected scan step"); return -ENOSYS; }
int bkprov_pair_attach_scan(struct bkprov_pair_s *p,
                            struct bkprov_tls_s *t, const uint8_t secret[32])
{ (void)p; (void)t; (void)secret; assert(!"unexpected scan attach"); return -ENOSYS; }
void bkprov_pair_close(struct bkprov_pair_s *p)
{ (void)p; assert(!"unexpected scan close"); }
int bkprov_storage_receipt(const uint8_t transaction[16])
{ (void)transaction; assert(!"unexpected receipt lookup"); return -ENOSYS; }
static int execute(void *p, enum bkcontrol_command_e command, uint32_t value,
                   struct bkcontrol_status_s *status)
{
  (void)p; (void)value; (void)status;
  assert(command == BKCONTROL_STATUS); executions++; return 0;
}
static void put(uint8_t *p, uint32_t n)
{ p[0] = n>>24; p[1] = n>>16; p[2] = n>>8; p[3] = n; }
static void start(struct bkcontrol_pair_s *pair)
{
  uint8_t secret[32]; memset(secret, 42, 32);
  memset(wire, 0, sizeof(wire)); used = 64; offset = 0;
  memcpy(wire, "SDC1", 4); put(wire+4, 1); put(wire+12, 32);
  memset(wire+16, 42, 32);
  memcpy(wire+48, "SDC1", 4); put(wire+52, 2); put(wire+56, 1);
  assert(bkcontrol_pair_start(pair, 7, NULL, NULL, secret, now, NULL, execute, NULL) == 0);
  memset(secret, 0, 32);
}
int main(void)
{
  for (fragment = 1; fragment <= 48; fragment++)
    {
      struct bkcontrol_pair_s pair={0};
      executions = queued = reads = promotions = touches = 0; start(&pair);
      /* AUTH then STATUS supplied together, delivered in varying fragments. */
      for (unsigned i = 0; !pair.report && i<100; i++)
        assert(bkcontrol_pair_step(&pair) == 0);
      assert(pair.report && pair.session.authenticated && executions == 0);
      queue_wait = 3;
      unsigned before = reads;
      for (unsigned i = 0;i<3;i++) assert(bkcontrol_pair_step(&pair) == 0);
      assert(reads == before && queued == 0 && executions == 0);
      for (unsigned i = 0; queued<2 && i<120; i++) assert(bkcontrol_pair_step(&pair) == 0);
      assert(queued == 2 && executions == 1 && promotions == 1 && touches == 1 &&
             output[7] == 2 && output[11] == 1);
      for (unsigned i = 0;i<5;i++) assert(bkcontrol_pair_step(&pair) == 0);
      assert(executions == 1);
      bkcontrol_pair_close(&pair);
      assert(!pair.tls.initialized && !pair.session.open);
    }
  struct bkcontrol_pair_s pair={0};
  start(&pair); used = 0;
  assert(bkcontrol_pair_step(&pair) == 0);
  clock_ms = 10000;
  assert(bkcontrol_pair_step(&pair) == -ETIMEDOUT && !pair.session.open);
  start(&pair); step_error = -ESTALE;
  assert(bkcontrol_pair_step(&pair) == -ESTALE && !pair.tls.initialized);
  step_error = 0; start(&pair); put(wire+12, 33);
  assert(bkcontrol_pair_step(&pair) == -EPROTO && !pair.session.open);
  start_error = -EIO;
  uint8_t secret[32]; memset(secret, 42, 32);
  assert(bkcontrol_pair_start(&pair, 7, NULL, NULL, secret, now, NULL, execute, NULL) == -EIO);
  assert(!pair.session.open);
  for (unsigned i = 0;i<32;i++) assert(pair.session.secret[i] == 0);
  puts("PASS: control TLS adapter fragmentation, backpressure, auth deadline and teardown");
  return 0;
}
