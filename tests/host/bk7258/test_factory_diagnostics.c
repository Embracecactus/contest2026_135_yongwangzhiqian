/* SPDX-License-Identifier: Apache-2.0 */
/* Factory diagnostics contract.  The only substituted input is monotonic time;
 * the actual transient source implementation and PC snapshot are linked. */

#include <assert.h>
#include <errno.h>
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
static uint64_t clock_ms;

int __wrap_clock_gettime(clockid_t clock, struct timespec *value)
{
  assert(clock == CLOCK_MONOTONIC);
  value->tv_sec = clock_ms / 1000u;
  value->tv_nsec = (clock_ms % 1000u) * 1000000u;
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
  assert(bkfactory_diagnostics_status(now, &flags, &remaining) == 0);
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
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 1000) == -EACCES);
  assert(bkfactory_diagnostics_gate(true, certificate, 1000) == 0);
  record[0] = 'X';
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 1000) == -EPROTO);
  record[0] = 'B';
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 1000) == 0);
  snapshot_ok(1000);
  assert(bkfactory_diagnostics_status(1000, &flags, &remaining) == 0);
  assert(remaining == BKFACTORY_DIAGNOSTICS_TTL_MS);
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 1001) == -EALREADY);
}

static void expiry_case(void)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 7;
  uint32_t flags = 0;
  uint32_t remaining = 0;
  fixture();
  clock_ms = 1000;
  assert(bkfactory_diagnostics_gate(true, certificate, 1000) == 0);
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 1000) == 0);
  assert(bkfactory_diagnostics_status(1000 + BKFACTORY_DIAGNOSTICS_TTL_MS - 1,
                                     &flags, &remaining) == 0);
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
  assert(bkfactory_diagnostics_status(clock_ms, &flags, &remaining) ==
         -ETIMEDOUT);
  assert(!(flags & BKFACTORY_DIAGNOSTICS_ACTIVE));
  assert(flags & BKFACTORY_DIAGNOSTICS_USED);
  assert(remaining == 0);
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 999999) == -EPERM);
}

static void revoke_case(void)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 7;
  fixture();
  clock_ms = 2000;
  assert(bkfactory_diagnostics_gate(true, certificate, 2000) == 0);
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 2000) == 0);
  assert(bkfactory_diagnostics_revoke(2001) == 0);
  assert(bkfactory_diagnostics_revoke(2002) == 0);
  memset(&view, 0xa5, sizeof(view));
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == -EACCES);
  assert(binding == 0);
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 2003) == -EPERM);
}

static void owner_case(void)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 0;
  fixture();
  clock_ms = 3000;
  assert(bkfactory_diagnostics_gate(true, certificate, 3000) == 0);
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 3000) == 0);
  assert(bkfactory_diagnostics_gate(false, NULL, 3001) == 0);
  memset(&view, 0xa5, sizeof(view));
  assert(bkfactory_diagnostics_snapshot(NULL, &binding, &view) == -EACCES);
  assert(binding == 0);
  for (size_t i = 0; i < sizeof(view); i++) assert(((uint8_t *)&view)[i] == 0);
  assert(bkfactory_diagnostics_gate(true, certificate, 3002) == 0);
  assert(bkfactory_diagnostics_enable(record, sizeof(record), 3002) == -EPERM);
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "enable")) enable_case();
  else if (!strcmp(argv[1], "expiry")) expiry_case();
  else if (!strcmp(argv[1], "revoke")) revoke_case();
  else if (!strcmp(argv[1], "owner")) owner_case();
  else return 2;
  puts("CONTRACT_PASS");
  return 0;
}
