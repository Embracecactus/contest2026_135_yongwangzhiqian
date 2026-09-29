/* SPDX-License-Identifier: Apache-2.0 */
/* Engineering control contract; product key policy is the behavioral oracle. */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bk7258_control_session.h"
#include "bk7258_engineering_test.h"
#include "bk7258_product_keys.h"

static struct bkvoice_product_keys_s g_keys;
static uint32_t g_epoch;
static uint32_t g_key_sequence;
static unsigned int g_key_events;
static unsigned int g_power_intents;
static uint64_t g_now = 10000;
static uint32_t g_power_state;
static int32_t g_power_error;

static void put32(uint8_t *p, uint32_t value)
{
  p[0] = value >> 24;
  p[1] = value >> 16;
  p[2] = value >> 8;
  p[3] = value;
}

static uint32_t get32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t now_ms(void *unused)
{
  (void)unused;
  return g_now;
}

static int key_begin(void *unused, uint32_t session, uint64_t now)
{
  bool power = false;

  (void)unused;
  if (session == 0)
    {
      return -EINVAL;
    }

  g_key_sequence = 0;
  bkvoice_product_keys_reset(&g_keys, ++g_epoch);
  (void)bkvoice_product_keys_step(&g_keys, g_epoch, 0, now, &power);
  assert(!power);
  return 0;
}

static int key_event(void *unused, uint32_t session, uint32_t sequence,
                     uint32_t pressed, uint64_t now, bool *power_accepted)
{
  bool power = false;

  (void)unused;
  if (session == 0 || sequence != g_key_sequence + 1 || power_accepted == NULL)
    {
      return -ESTALE;
    }

  g_key_sequence = sequence;
  g_key_events++;
  (void)bkvoice_product_keys_step(&g_keys, g_epoch, pressed, now, &power);
  if (power)
    {
      g_power_intents++;
    }

  *power_accepted = power;

  return 0;
}

static int key_end(void *unused, uint32_t session)
{
  (void)unused;
  if (session == 0)
    {
      return -EINVAL;
    }

  bkvoice_product_keys_reset(&g_keys, ++g_epoch);
  return 0;
}

static int power_status(void *unused, uint32_t *state, int32_t *error)
{
  (void)unused;
  *state = g_power_state;
  *error = g_power_error;
  return 0;
}

static const struct bkengtest_ops_s g_ops =
{
  .now_ms = now_ms,
  .key_begin = key_begin,
  .key_event = key_event,
  .key_end = key_end,
  .power_status = power_status,
};

static struct bkengtest_s g_test;

static int apply(uint32_t operation, uint32_t session, uint32_t sequence,
                 uint32_t value, uint32_t elapsed_ms, uint32_t flags)
{
  uint8_t record[BKENGTEST_RECORD_SIZE] = { 'B', 'K', 'T', '1' };
  struct bkcontrol_status_s status;

  put32(record + 4, BKENGTEST_VERSION);
  put32(record + 8, operation);
  put32(record + 12, session);
  put32(record + 16, sequence);
  put32(record + 20, value);
  put32(record + 24, elapsed_ms);
  put32(record + 28, flags);
  memset(&status, 0, sizeof(status));
  assert(bkengtest_control(&g_test, &g_ops, NULL,
                          BKCONTROL_CONFIG_BEGIN, 0, NULL,
                          sizeof(record), &status) == 0);
  return bkengtest_control(&g_test, &g_ops, NULL,
                           BKCONTROL_CONFIG_APPLY, 0, record,
                           sizeof(record), &status);
}

static void start(uint32_t session, uint32_t mode)
{
  assert(apply(BKENGTEST_OP_SESSION, session, 1, mode, 0, 0) == 0);
}

static void release_case(uint32_t held_ms, bool expected)
{
  start(7, BKENGTEST_PM_BLOCKED);
  assert(apply(BKENGTEST_OP_KEY, 7, 2,
               BKVOICE_PRODUCT_KEY_POWER, 0, 0) == 0);
  /* Advancing the engineering clock emits no held KEY1 message. */
  assert(apply(BKENGTEST_OP_ADVANCE, 7, 3, 0, held_ms, 0) == 0);
  assert(apply(BKENGTEST_OP_KEY, 7, 4, 0, held_ms, 0) == 0);
  assert(g_key_events == 2);
  assert(g_power_intents == (expected ? 1u : 0u));
}

static void status_case(void)
{
  uint8_t wire[BKENGTEST_STATUS_SIZE];
  struct bkcontrol_status_s status;

  start(17, BKENGTEST_PM_PENDING);
  g_power_state = 2u | 256u;
  g_power_error = -EINPROGRESS;
  for (uint32_t offset = 0; offset < sizeof(wire); offset += 16)
    {
      memset(&status, 0, sizeof(status));
      assert(bkengtest_control(&g_test, &g_ops, NULL,
                              BKCONTROL_CONFIG_READ, offset, NULL, 0,
                              &status) == 0);
      assert(status.config_total == sizeof(wire));
      memcpy(wire + offset, status.config_chunk, 16);
    }

  assert(!memcmp(wire, "BKS1", 4));
  assert(get32(wire + 4) == BKENGTEST_VERSION);
  assert(get32(wire + 8) & BKENGTEST_STATUS_ENABLED);
  assert(get32(wire + 12) == 17);
  assert(get32(wire + 28) == BKENGTEST_PM_PENDING);
  assert(get32(wire + 40) == g_power_state);
  assert((int32_t)get32(wire + 44) == g_power_error);
}

static void expiry_case(void)
{
  uint8_t wire[BKENGTEST_STATUS_SIZE];
  struct bkcontrol_status_s status;

  start(19, BKENGTEST_PM_BLOCKED);
  bkengtest_step(&g_test, &g_ops, NULL,
                 g_now + BKENGTEST_SESSION_IDLE_MS - 1u);
  memset(&status, 0, sizeof(status));
  assert(bkengtest_control(&g_test, &g_ops, NULL,
                          BKCONTROL_CONFIG_READ, 0, NULL, 0,
                          &status) == 0);
  assert(get32(status.config_chunk + 8) & BKENGTEST_STATUS_ACTIVE);

  bkengtest_step(&g_test, &g_ops, NULL,
                 g_now + BKENGTEST_SESSION_IDLE_MS);
  for (uint32_t offset = 0; offset < sizeof(wire); offset += 16)
    {
      memset(&status, 0, sizeof(status));
      assert(bkengtest_control(&g_test, &g_ops, NULL,
                              BKCONTROL_CONFIG_READ, offset, NULL, 0,
                              &status) == 0);
      memcpy(wire + offset, status.config_chunk, 16);
    }

  assert(!(get32(wire + 8) & BKENGTEST_STATUS_ACTIVE));
  assert(get32(wire + 8) & BKENGTEST_STATUS_EXPIRED);
  assert((int32_t)get32(wire + 48) == -ETIMEDOUT);
}

static void sequence_case(void)
{
  start(9, BKENGTEST_PM_BLOCKED);
  assert(apply(BKENGTEST_OP_KEY, 9, 2,
               BKVOICE_PRODUCT_KEY_POWER, 0, 0) == 0);
  unsigned int before = g_key_events;
  assert(apply(BKENGTEST_OP_KEY, 9, 2, 0, 1, 0) == -ESTALE);
  assert(apply(BKENGTEST_OP_KEY, 10, 3, 0, 1, 0) == -ESTALE);
  assert(g_key_events == before);
}

static void session_ownership_case(void)
{
  start(21, BKENGTEST_PM_PENDING);
  assert(apply(BKENGTEST_OP_KEY, 21, 2,
               BKVOICE_PRODUCT_KEY_POWER, 0, 0) == 0);
  assert(apply(BKENGTEST_OP_ADVANCE, 21, 3, 0, 3000, 0) == 0);
  assert(apply(BKENGTEST_OP_KEY, 21, 4, 0, 3000, 0) == 0);
  assert(apply(BKENGTEST_OP_SESSION, 22, 1,
               BKENGTEST_PM_DECLINED, 0, 0) == -EBUSY);
  assert(bkengtest_pm_request(&g_test) == 0);
  assert(bkengtest_pm_status(&g_test) == 1);

  assert(apply(BKENGTEST_OP_END, 21, 5, 0, 3000, 0) == 0);
  g_power_state = 2u | 256u;
  g_power_error = -EINPROGRESS;
  assert(apply(BKENGTEST_OP_SESSION, 22, 1,
               BKENGTEST_PM_DECLINED, 0, 0) == -EBUSY);
}

static void disconnect_case(void)
{
  uint8_t wire[BKENGTEST_STATUS_SIZE];
  struct bkcontrol_status_s status;

  start(41, BKENGTEST_PM_BLOCKED);
  assert(apply(BKENGTEST_OP_KEY, 41, 2,
               BKVOICE_PRODUCT_KEY_POWER, 0, 0) == 0);
  assert(apply(BKENGTEST_OP_KEY, 41, 3, 0, 2999, 0) == 0);
  assert(bkengtest_disconnect(&g_test, &g_ops, NULL) == 0);
  memset(&status, 0, sizeof(status));
  assert(bkengtest_control(&g_test, &g_ops, NULL,
                          BKCONTROL_CONFIG_READ, 0, NULL, 0,
                          &status) == 0);
  assert(!(get32(status.config_chunk + 8) & BKENGTEST_STATUS_ACTIVE));

  start(42, BKENGTEST_PM_LATE_ACK);
  assert(apply(BKENGTEST_OP_KEY, 42, 2,
               BKVOICE_PRODUCT_KEY_POWER, 0, 0) == 0);
  assert(apply(BKENGTEST_OP_KEY, 42, 3, 0, 3000, 0) == 0);
  assert(bkengtest_disconnect(&g_test, &g_ops, NULL) == 1);
  for (uint32_t offset = 0; offset < sizeof(wire); offset += 16)
    {
      memset(&status, 0, sizeof(status));
      assert(bkengtest_control(&g_test, &g_ops, NULL,
                              BKCONTROL_CONFIG_READ, offset, NULL, 0,
                              &status) == 0);
      memcpy(wire + offset, status.config_chunk, 16);
    }

  assert(get32(wire + 8) & BKENGTEST_STATUS_ACTIVE);
  assert(get32(wire + 8) & BKENGTEST_STATUS_POWER_INTENT);
  assert(get32(wire + 12) == 42);
  assert(get32(wire + 28) == BKENGTEST_PM_LATE_ACK);
}

static void pm_case(uint32_t mode)
{
  start(11, mode);
  assert(apply(BKENGTEST_OP_KEY, 11, 2,
               BKVOICE_PRODUCT_KEY_POWER, 0, 0) == 0);
  assert(apply(BKENGTEST_OP_ADVANCE, 11, 3, 0, 3000, 0) == 0);
  assert(apply(BKENGTEST_OP_KEY, 11, 4, 0, 3000, 0) == 0);
  int request = bkengtest_pm_request(&g_test);
  int first = bkengtest_pm_status(&g_test);
  int second = bkengtest_pm_status(&g_test);

  if (mode == BKENGTEST_PM_DECLINED)
    {
      assert(request == -ETIMEDOUT && first == 0 && second == 0);
    }
  else if (mode == BKENGTEST_PM_UNKNOWN)
    {
      assert(request == -ETIMEDOUT && first == -ETIMEDOUT &&
             second == -ETIMEDOUT);
    }
  else if (mode == BKENGTEST_PM_PENDING)
    {
      assert(request == 0 && first == 1 && second == 1);
    }
  else
    {
      assert(mode == BKENGTEST_PM_LATE_ACK);
      assert(request == -ETIMEDOUT && first == -ETIMEDOUT && second == 1);
    }
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "release-2999")) release_case(2999, false);
  else if (!strcmp(argv[1], "release-3000")) release_case(3000, true);
  else if (!strcmp(argv[1], "release-3001")) release_case(3001, true);
  else if (!strcmp(argv[1], "no-held")) release_case(3000, true);
  else if (!strcmp(argv[1], "status")) status_case();
  else if (!strcmp(argv[1], "session-expiry")) expiry_case();
  else if (!strcmp(argv[1], "sequence")) sequence_case();
  else if (!strcmp(argv[1], "session-ownership")) session_ownership_case();
  else if (!strcmp(argv[1], "disconnect")) disconnect_case();
  else if (!strcmp(argv[1], "cp-declined")) pm_case(BKENGTEST_PM_DECLINED);
  else if (!strcmp(argv[1], "cp-unknown")) pm_case(BKENGTEST_PM_UNKNOWN);
  else if (!strcmp(argv[1], "cp-pending")) pm_case(BKENGTEST_PM_PENDING);
  else if (!strcmp(argv[1], "cp-late-ack")) pm_case(BKENGTEST_PM_LATE_ACK);
  else return 2;
  puts("CONTRACT_PASS");
  return 0;
}
