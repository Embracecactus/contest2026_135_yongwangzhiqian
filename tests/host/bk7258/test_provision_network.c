/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_network.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_storage.h"
#include "bk7258_voice_config.h"
#include "bk7258_provision_time.h"
#include <arch/chip/bk7258_wifi.h>
#include <mbedtls/platform_util.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int validation, starts, loads, connects, clears, cancels, finishes, commits;
static int connect_error, ready_status, wifi_status, storage_status = -EAGAIN, clock_writes;
static bool available = true, completion, finish_commit, cloud_candidate;
static int cloud_loads;
static uint32_t ticket;
static int time_status = -EAGAIN;
static const uint8_t candidate[] = {42};
static uint8_t transaction[16] = {1};
void mbedtls_platform_zeroize(void *p, size_t n) { memset(p, 0, n); }
int bkprov_time_get(uint64_t minimum, uint64_t *utc)
{ (void)minimum;*utc = 1800000000;return time_status; }
int bkvoice_config_validate(const void *p, size_t n) { assert(p && n == 1); return validation; }
uint64_t bkvoice_config_now_ms(void *context) { (void)context; return 1000; }
int __wrap_clock_settime(clockid_t id, const struct timespec *value)
{ assert(id == CLOCK_REALTIME && value); clock_writes++; return 0; }
int bkprov_settings_decode(struct bkprov_settings_s *s, const void *p, size_t n)
{ assert(n == 1 && *(const uint8_t *)p == 42); strcpy(s->ssid, "test");strcpy(s->password, "test-only-password");if (cloud_candidate){s->cloud = p;s->cloud_size = n;}return 0; }
int bkprov_settings_voice(const struct bkprov_settings_s *s, const uint8_t *cert, size_t cn,
                          const uint8_t *key, size_t kn, void *out, size_t cap, size_t *n)
{ assert(s && cert && key && cn == 1 && kn == 1 && cap >= 1); *(uint8_t *)out = 42;*n = 1;return 0; }
int bk7258_wifi_trial_start(const char *ssid, const char *password, uint32_t timeout, uint32_t *lease)
{ assert(!strcmp(ssid, "test") && !strcmp(password, "test-only-password") && timeout == 30000); starts++;*lease=++ticket;completion = false;return 0; }
int bk7258_wifi_connect_poll(uint32_t t, struct bk7258_wifi_result_s *r)
{ assert(t == ticket);if (!completion)return -EAGAIN;completion = false;r->status = wifi_status;return 0; }
int bk7258_wifi_connect_cancel(uint32_t t) { assert(t == ticket);cancels++;return 0; }
int bk7258_wifi_trial_finish(uint32_t lease, bool commit, uint32_t *t)
{ assert(lease>0);finishes++;finish_commit = commit;*t=++ticket;completion = false;return 0; }
int bkprov_storage_commit(uint64_t revision, const uint8_t tx[16], const void *b, size_t n)
{ assert(revision == 0 && tx[0] == 1);if (n != 1 || *(const uint8_t *)b != 42)return -EINVAL;commits++;return storage_status; }
static bool voice_available(void *c) { (void)c;return available; }
static int voice_load(void *c, const void *p, size_t n)
{ (void)c;assert(n == 1 && *(const uint8_t *)p == 42);loads++;return 0; }
static int voice_connect(void *c) { (void)c;connects++;return connect_error; }
static int voice_ready(void *c) { (void)c;return ready_status; }
static int voice_clear(void *c) { (void)c;clears++;return 0; }
static int cloud_load(void *c, const void *trust, size_t tn, const void *cloud, size_t cn)
{ assert(cn == 1 && *(const uint8_t *)cloud == 42);cloud_loads++;return voice_load(c, trust, tn); }
static const struct bkprov_voice_ops_s cloud_voice =
{voice_available, voice_load, voice_connect, voice_ready, voice_clear, cloud_load};
static const struct bkprov_voice_ops_s voice = {voice_available, voice_load, voice_connect, voice_ready, voice_clear, NULL};
static void complete(int status) { completion = true;wifi_status = status;bkprov_network_step(); }
static void verified(const struct bkprov_claim_ops_s *ops)
{
  ready_status = 0;
  assert(ops->begin(NULL, candidate, sizeof(candidate)) == 0);
  complete(0);assert(ops->poll(NULL) == 0);
  ready_status = 1;bkprov_network_step();assert(ops->poll(NULL) == 1);
}
int main(void)
{
  uint8_t identity_bytes[50] = {0};
  struct bkprov_identity_s identity = {.record = identity_bytes, .certificate_size = 1, .key_size = 1};
  const struct bkprov_claim_ops_s *ops = bkprov_network_ops();
  assert(bkprov_network_bind(&identity, &voice, NULL) == 0);
  available = false;assert(ops->begin(NULL, candidate, 1) == -EBUSY && starts == 0);available = true;
  validation = -EBADMSG;assert(ops->begin(NULL, candidate, 1) == -EBADMSG && starts == 0);validation = 0;
  verified(ops);
  assert(bkprov_network_unbind() == -EBUSY);
  assert(ops->commit(NULL, transaction, candidate, 1) == -EAGAIN && commits == 1);
  ops->abort(NULL); /* BLE goes away while the independent store owns publication. */
  assert(bkprov_network_busy() && clears == 0 && finishes == 0);
  storage_status = 0;bkprov_network_step();assert(finish_commit && bkprov_network_busy());
  assert(ops->commit(NULL, transaction, candidate, 1) == 0);
  const uint8_t changed[]={43};assert(ops->commit(NULL, transaction, changed, 1) == -EINVAL);
  complete(0);assert(!bkprov_network_busy() && clears == 0);

  int old_loads = loads, old_commits = commits;
  assert(ops->begin(NULL, candidate, 1) == 0);complete(-EACCES);
  assert(!finish_commit && loads == old_loads && commits == old_commits);
  complete(0);assert(!bkprov_network_busy() && ops->poll(NULL) == -EACCES);

  connect_error = -EACCES;
  assert(ops->begin(NULL, candidate, 1) == 0);complete(0);
  assert(!finish_commit && clears == 1 && clock_writes == 1);
  complete(0);assert(!bkprov_network_busy());connect_error = 0;

  assert(ops->begin(NULL, candidate, 1) == 0);ops->abort(NULL);
  assert(cancels == 1 && bkprov_network_busy());old_loads = loads;
  complete(0);assert(loads == old_loads && !finish_commit);
  complete(0);assert(!bkprov_network_busy());

  verified(ops);storage_status = -EIO;
  assert(ops->commit(NULL, transaction, candidate, 1) == -EIO);
  bkprov_network_step();assert(!finish_commit);
  complete(0);assert(!bkprov_network_busy());

  ready_status = 0;assert(ops->begin(NULL, candidate, 1) == 0);complete(0);
  ready_status = -ENOTCONN;bkprov_network_step();assert(!finish_commit);
  complete(0);assert(!bkprov_network_busy() && ops->poll(NULL) == -ENOTCONN);

  old_loads = loads;old_commits = commits;ready_status = 0;
  assert(bkprov_network_restore(candidate, 1) == 0);complete(0);
  assert(loads == old_loads && bkprov_network_busy());
  time_status = 0;bkprov_network_step();assert(loads == old_loads+1);
  ready_status = 1;bkprov_network_step();assert(finish_commit);
  complete(0);assert(!bkprov_network_busy() && commits == old_commits);

  old_loads = loads;time_status = -ETIMEDOUT;
  assert(bkprov_network_restore(candidate, 1) == 0);complete(0);
  assert(!finish_commit && loads == old_loads);
  complete(0);assert(!bkprov_network_busy() && ops->poll(NULL) == -ETIMEDOUT);
  assert(commits == old_commits);
  assert(ops->commit(NULL, NULL, candidate, 1) == -EINVAL);
  time_status = 0;

  cloud_candidate = true;
  int old_starts = starts;
  assert(ops->begin(NULL, candidate, 1) == -ENOTSUP && starts == old_starts);
  assert(bkprov_network_bind(&identity, &cloud_voice, NULL) == 0);
  ready_status = 0;
  assert(ops->begin(NULL, candidate, 1) == 0);complete(0);
  assert(cloud_loads == 1);
  ready_status = -EACCES;bkprov_network_step();assert(!finish_commit);
  complete(0);assert(!bkprov_network_busy() && ops->poll(NULL) == -EACCES);
  cloud_candidate = false;
  assert(bkprov_network_bind(&identity, &voice, NULL) == 0);

  verified(ops);storage_status = -EINPROGRESS;
  int previous_finishes = finishes;
  assert(ops->commit(NULL, transaction, candidate, 1) == -EINPROGRESS);
  ops->abort(NULL);bkprov_network_step();
  assert(finishes == previous_finishes && bkprov_network_busy());
  assert(bkprov_network_unbind() == -EBUSY);
  puts("BKPROV_NETWORK_PASS: verified publication, cancellation and rollback quarantine");
  return 0;
}
