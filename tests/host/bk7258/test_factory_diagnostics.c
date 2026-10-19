/* SPDX-License-Identifier: Apache-2.0 */
/* Factory diagnostics contract.  The only substituted input is monotonic time;
 * the actual transient source implementation and PC snapshot are linked. */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bk7258_factory_diagnostics.h"
#include "bk7258_pc_grants.h"

static uint8_t record[BKFACTORY_DIAGNOSTICS_RECORD_SIZE] =
{
  'B', 'K', 'D', '1',
};
static uint8_t certificate[32];
static atomic_uint_fast64_t clock_ms;
static sem_t lock_pending;
static sem_t lock_resume;
static _Thread_local bool delay_lock;

int __real_pthread_mutex_lock(pthread_mutex_t *mutex);

int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
  if (delay_lock)
    {
      delay_lock = false;
      assert(sem_post(&lock_pending) == 0);
      assert(sem_wait(&lock_resume) == 0);
    }

  return __real_pthread_mutex_lock(mutex);
}

int __wrap_clock_gettime(clockid_t clock, struct timespec *value)
{
  uint64_t now = atomic_load(&clock_ms);
  assert(clock == CLOCK_MONOTONIC);
  value->tv_sec = now / 1000u;
  value->tv_nsec = (now % 1000u) * 1000000u;
  return 0;
}

static void fixture(void)
{
  for (unsigned int i = 0; i < 16; i++) record[4 + i] = i + 1;
  for (unsigned int i = 0; i < 32; i++)
    {
      record[20 + i] = 0x80u + i;
      certificate[i] = 0x40u + i;
    }
}

static void snapshot_ok(uint64_t now)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 0;
  uint32_t flags = 0;
  uint32_t remaining = 0;
  clock_ms = now;
  memset(&view, 0xa5, sizeof(view));
  assert(bkfactory_diagnostics_status(&flags, &remaining) == 0);
  assert((flags & (BKFACTORY_DIAGNOSTICS_ELIGIBLE |
                   BKFACTORY_DIAGNOSTICS_ACTIVE)) ==
         (BKFACTORY_DIAGNOSTICS_ELIGIBLE |
          BKFACTORY_DIAGNOSTICS_ACTIVE));
  assert(remaining > 0);
  memset(&view, 0, sizeof(view));
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == 0);
  assert(binding != 0 && view.revision != 0);
  assert(view.capabilities == BKPC_CAP_DIAGNOSTICS);
  assert(!memcmp(view.client, record + 4, 16));
  assert(!memcmp(view.key, record + 20, 32));
  for (uint32_t offset = 0; offset < 32; offset += 4)
    {
      uint32_t word = 0;
      assert(bkfactory_diagnostics_certificate(offset, &word) == 0);
      assert(word == ((uint32_t)certificate[offset] << 24 |
                      (uint32_t)certificate[offset + 1] << 16 |
                      (uint32_t)certificate[offset + 2] << 8 |
                      certificate[offset + 3]));
    }
}

static void enable_case(void)
{
  uint32_t flags = 0;
  uint32_t remaining = 0;
  fixture();
  clock_ms = 1000;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == -EACCES);
  clock_ms = 1000;
  assert(bkfactory_diagnostics_gate(true, certificate) == 0);
  record[0] = 'X';
  clock_ms = 1000;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == -EPROTO);
  record[0] = 'B';
  clock_ms = 1000;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == 0);
  snapshot_ok(1000);
  assert(bkfactory_diagnostics_status(&flags, &remaining) == 0);
  assert(remaining == BKFACTORY_DIAGNOSTICS_TTL_MS);
  clock_ms = 1001;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == -EALREADY);
}

static void expiry_case(void)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 7;
  uint32_t flags = 0;
  uint32_t remaining = 0;
  fixture();
  clock_ms = 1000;
  clock_ms = 1000;
  assert(bkfactory_diagnostics_gate(true, certificate) == 0);
  clock_ms = 1000;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == 0);
  clock_ms = 1000 + BKFACTORY_DIAGNOSTICS_TTL_MS - 1;
  assert(bkfactory_diagnostics_status(&flags, &remaining) == 0);
  assert(flags & BKFACTORY_DIAGNOSTICS_ACTIVE);
  assert(remaining == 1);
  /* An already-open TLS/SDC1 session revalidates only through snapshot().
   * Deadline enforcement therefore cannot depend on a product-loop status
   * poll or a CP-provided timestamp. */
  clock_ms = 1000 + BKFACTORY_DIAGNOSTICS_TTL_MS;
  memset(&view, 0xa5, sizeof(view));
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == -EACCES);
  assert(binding == 0);
  for (size_t i = 0; i < sizeof(view); i++) assert(((uint8_t *)&view)[i] == 0);
  assert(bkfactory_diagnostics_status(&flags, &remaining) ==
         -ETIMEDOUT);
  assert(!(flags & BKFACTORY_DIAGNOSTICS_ACTIVE));
  assert(flags & BKFACTORY_DIAGNOSTICS_USED);
  assert(remaining == 0);
  clock_ms = 999999;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == -EPERM);
}

static void revoke_case(void)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 7;
  fixture();
  clock_ms = 2000;
  clock_ms = 2000;
  assert(bkfactory_diagnostics_gate(true, certificate) == 0);
  clock_ms = 2000;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == 0);
  clock_ms = 2001;
  assert(bkfactory_diagnostics_revoke() == 0);
  clock_ms = 2002;
  assert(bkfactory_diagnostics_revoke() == 0);
  memset(&view, 0xa5, sizeof(view));
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == -EACCES);
  assert(binding == 0);
  clock_ms = 2003;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == -EPERM);
}

static void owner_case(void)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 0;
  fixture();
  clock_ms = 3000;
  clock_ms = 3000;
  assert(bkfactory_diagnostics_gate(true, certificate) == 0);
  clock_ms = 3000;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == 0);
  clock_ms = 3001;
  assert(bkfactory_diagnostics_gate(false, NULL) == 0);
  memset(&view, 0xa5, sizeof(view));
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == -EACCES);
  assert(binding == 0);
  for (size_t i = 0; i < sizeof(view); i++) assert(((uint8_t *)&view)[i] == 0);
  clock_ms = 3002;
  assert(bkfactory_diagnostics_gate(true, certificate) == 0);
  clock_ms = 3002;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == -EPERM);
}

static void *delayed_snapshot(void *context)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding;
  int *result = context;

  delay_lock = true;
  *result = bkfactory_diagnostics_snapshot(NULL, &binding, &view);
  return NULL;
}

static void concurrent_case(void)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding;
  pthread_t thread;
  int result = -1;

  fixture();
  clock_ms = 4000;
  clock_ms = 4000;
  assert(bkfactory_diagnostics_gate(true, certificate) == 0);
  clock_ms = 4000;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == 0);
  assert(sem_init(&lock_pending, 0, 0) == 0);
  assert(sem_init(&lock_resume, 0, 0) == 0);
  clock_ms = 5000;
  assert(pthread_create(&thread, NULL, delayed_snapshot, &result) == 0);
  assert(sem_wait(&lock_pending) == 0);

  /* The second caller completes while the first waits before the same lock.
   * A monotonically increasing clock must not revoke the shared principal. */

  clock_ms = 5003;
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == 0);
  assert(sem_post(&lock_resume) == 0);
  assert(pthread_join(thread, NULL) == 0);
  fprintf(stderr, "interleaving clock=5000->5003 delayed_result=%d\n", result);
  assert(result == 0);
  snapshot_ok(5003);
  assert(sem_destroy(&lock_pending) == 0);
  assert(sem_destroy(&lock_resume) == 0);
}

static void backwards_case(void)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 7;

  fixture();
  clock_ms = 4000;
  clock_ms = 4000;
  assert(bkfactory_diagnostics_gate(true, certificate) == 0);
  clock_ms = 4000;
  assert(bkfactory_diagnostics_enable(record, sizeof(record)) == 0);
  snapshot_ok(5003);
  clock_ms = 5000;
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == -EACCES);
  assert(binding == 0);
  clock_ms = 5004;
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == -EACCES);
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "enable")) enable_case();
  else if (!strcmp(argv[1], "expiry")) expiry_case();
  else if (!strcmp(argv[1], "revoke")) revoke_case();
  else if (!strcmp(argv[1], "owner")) owner_case();
  else if (!strcmp(argv[1], "concurrent")) concurrent_case();
  else if (!strcmp(argv[1], "backwards")) backwards_case();
  else return 2;
  puts("CONTRACT_PASS");
  return 0;
}
