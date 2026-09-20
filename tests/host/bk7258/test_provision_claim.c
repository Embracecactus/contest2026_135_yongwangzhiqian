/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_claim.h"
#include "bk7258_provision_store.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static struct bkprov_claim_s claim;
static struct bkprov_store_s store;
static const uint8_t secret[32] = {42}, transaction[16] = {7};
static const uint8_t bundle[] = {1, 2, 3, 4, 5, 6};
static int begun, aborted, commits, network_result;
static int zero_write, fail_sync, fail_rename;
static uint64_t revision;
static bool async_commit;
static uint8_t retained[sizeof(bundle)], retained_transaction[16];

ssize_t __real_write(int fd, const void *data, size_t size);
int __real_fsync(int fd);
int __real_rename(const char *from, const char *to);
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
  if (zero_write) return 0;
  return __real_write(fd, data, size < 7 ? size : 7);
}
int __wrap_fsync(int fd)
{
  if (fail_sync) { errno = EIO; return -1; }
  return __real_fsync(fd);
}
int __wrap_rename(const char *from, const char *to)
{
  if (fail_rename) { errno = EIO; return -1; }
  return __real_rename(from, to);
}
static int begin(void *ctx, const uint8_t *data, size_t size)
{
  (void)ctx;
  assert(size == sizeof(bundle) && memcmp(data, bundle, size) == 0);
  begun++; return 0;
}
static int poll_network(void *ctx) { (void)ctx; return network_result; }
static int commit(void *ctx, const uint8_t tx[16], const uint8_t *data, size_t size)
{
  (void)ctx; commits++;
  if (async_commit)
    {
      memcpy(retained, data, size);
      memcpy(retained_transaction, tx, 16);
      return -EAGAIN;
    }
  int ret = bkprov_store_commit(&store, revision, tx, data, size);
  if (ret == 0) revision++;
  return ret;
}
static void abort_trial(void *ctx) { (void)ctx; aborted++; }
static const struct bkprov_claim_ops_s ops =
{begin, poll_network, commit, abort_trial};

static void opened(void)
{
  bkprov_claim_close(&claim);
  assert(bkprov_claim_open(&claim, 1, secret, false, false, 0, &ops, NULL) == -EACCES);
  assert(bkprov_claim_open(&claim, 1, secret, true, true, 0, &ops, NULL) == -EACCES);
  assert(bkprov_claim_open(&claim, 1, secret, true, false, 0, &ops, NULL) == 0);
}
static void authenticated(void)
{
  opened();
  assert(bkprov_claim_auth(&claim, 1, transaction, secret) == 0);
  assert(bkprov_claim_begin(&claim, 1, sizeof(bundle)) == -EACCES);
  assert(bkprov_claim_confirm(&claim, 2) == -EACCES);
  assert(bkprov_claim_confirm(&claim, 1) == 0);
}
static void checking(void)
{
  authenticated();
  assert(bkprov_claim_begin(&claim, 1, sizeof(bundle)) == 0);
  assert(bkprov_claim_data(&claim, 2, 0, bundle, 3) == 0);
  assert(bkprov_claim_data(&claim, 3, 3, bundle+3, 3) == 0);
  assert(bkprov_claim_apply(&claim, 4) == 0);
  assert(claim.state == BKPROV_CHECKING);
}
static void selected(uint64_t expected)
{
  uint8_t data[sizeof(bundle)], tx[16]; size_t size; uint64_t actual;
  assert(bkprov_store_load(&store, data, sizeof(data), &size, &actual, tx) == 0);
  assert(actual == expected && size == sizeof(bundle) && !memcmp(data, bundle, size));
  assert(!memcmp(tx, transaction, 16));
}
static void wiped(void)
{
  for (size_t i = 0;i<sizeof(claim.bundle);i++) assert(claim.bundle[i] == 0);
  for (size_t i = 0;i<sizeof(claim.secret);i++) assert(claim.secret[i] == 0);
}
int main(int argc, char **argv)
{
  uint8_t bad[32] = {0}, data[sizeof(bundle)]; size_t size; uint64_t rev;
  assert(argc == 2 && bkprov_store_open(&store, argv[1]) == 0);
  opened();
  assert(bkprov_claim_begin(&claim, 1, 6) == -EACCES);
  assert(bkprov_claim_auth(&claim, 1, transaction, bad) == -EACCES);
  assert(bkprov_claim_auth(&claim, 1, transaction, secret) == -EACCES);
  assert(begun == 0 && commits == 0); wiped();
  checking();
  assert(bkprov_claim_step(&claim, 1, 100) == 0 && commits == 0);
  assert(bkprov_store_load(&store, data, sizeof(data), &size, &rev, NULL) == -ENOENT);
  network_result = 1;
  assert(bkprov_claim_step(&claim, 1, 200) == 0);
  assert(claim.state == BKPROV_COMMITTED && commits == 1 && aborted == 0);
  selected(1); wiped();
  assert(bkprov_claim_step(&claim, 1, 201) == 0 && commits == 1);

  checking(); network_result = -EACCES;
  assert(bkprov_claim_step(&claim, 1, 100) == -EACCES);
  assert(aborted == 1 && commits == 1); selected(1); wiped();
  checking(); network_result = 1; zero_write = 1;
  assert(bkprov_claim_step(&claim, 1, 100) == -EIO);
  zero_write = 0; assert(aborted == 2); selected(1); wiped();
  checking(); fail_sync = 1;
  assert(bkprov_claim_step(&claim, 1, 100) == -EIO);
  fail_sync = 0; assert(aborted == 3); selected(1); wiped();
  checking(); fail_rename = 1;
  assert(bkprov_claim_step(&claim, 1, 100) == -EINPROGRESS);
  fail_rename = 0;
  assert(claim.state == BKPROV_UNCERTAIN && aborted == 3); selected(1); wiped();
  /* Stale staging from the uncertain attempt is ignored on reopen. */
  struct bkprov_store_s recovered;
  assert(bkprov_store_open(&recovered, argv[1]) == 0);
  assert(bkprov_store_load(&recovered, data, sizeof(data), &size, &rev, NULL) == 0 && rev == 1);
  assert(bkprov_store_commit(&store, 0, transaction, bundle, sizeof(bundle)) == -ESTALE);

  checking();
  assert(bkprov_claim_step(&claim, 2, 100) == -ESTALE);
  assert(aborted == 4); selected(1); wiped();
  checking();
  assert(bkprov_claim_step(&claim, 1, 120000) == -ETIMEDOUT);
  assert(aborted == 5); selected(1); wiped();
  checking(); bkprov_claim_close(&claim);
  assert(aborted == 6 && claim.state == BKPROV_CLOSED); selected(1); wiped();
  authenticated();
  assert(bkprov_claim_begin(&claim, 1, 6) == 0);
  assert(bkprov_claim_data(&claim, 2, 0, bundle, 3) == 0);
  assert(bkprov_claim_data(&claim, 2, 0, bundle, 3) == -EPROTO);
  selected(1); wiped();
  checking(); async_commit = true;
  assert(bkprov_claim_step(&claim, 1, 100) == 0 && claim.commit_pending);
  selected(1);
  assert(bkprov_claim_step(&claim, 2, 101) == -ESTALE);
  assert(claim.state == BKPROV_UNCERTAIN && aborted == 6); wiped();
  /* The storage worker owns its copy after BLE disconnect. A late success
   * must remain recoverable by its persisted transaction receipt.
   */
  assert(bkprov_store_commit(&store, 1, retained_transaction, retained, sizeof(retained)) == 0);
  selected(2);
  /* Hash failure is never treated as an unclaimed / empty store. */
  int fd = open(store.active, O_WRONLY);
  assert(fd >= 0 && pwrite(fd, bad, 1, 65) == 1 && close(fd) == 0);
  assert(bkprov_store_load(&store, data, sizeof(data), &size, &rev, NULL) == -EBADMSG);
  assert(bkprov_store_commit(&store, 0, transaction, bundle, sizeof(bundle)) == -EBADMSG);
  bkprov_claim_close(&claim);
  puts("BKPROV_CLAIM_STORE_HOST_PASS");
  return 0;
}
