/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Host contract tests for the real BK7258 CP reset-marker implementation.
 ****************************************************************************/

#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/chip/bk7258_storage_config.h>
#include <arch/chip/bk7258_flash.h>

#include <nuttx/mutex.h>

#include "bk7258_reset_marker_internal.h"
#include "bk7258_storage_configure.h"
#include "bk7258_storage_internal.h"

#ifndef TEST_RESET_MARKER_SCENARIO
#  error "TEST_RESET_MARKER_SCENARIO is required"
#endif

#define TEST_UNUSED static __attribute__((unused))
#define MARKER_MAGIC 0x524d4b42u
#define MARKER_VERSION 2u

struct record_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t reason;
  uint32_t reason_inverse;
};

struct context_s
{
  uint8_t sector[BK7258_RESET_MARKER_ERASE_SIZE];
  int initialize_result;
  int read_result;
  int erase_result;
  int write_result;
  unsigned int initialize_calls;
  unsigned int read_calls;
  unsigned int erase_calls;
  unsigned int write_calls;
  uint32_t last_address;
  unsigned int order[16];
  unsigned int order_count;
  bool corrupt_readback;
  bool reenter_capture;
  int reenter_capture_result;
};

enum event_e
{
  EVENT_INITIALIZE,
  EVENT_READ,
  EVENT_ERASE,
  EVENT_WRITE
};

static struct context_s g_context;

TEST_UNUSED void event(enum event_e value)
{
  g_context.order[g_context.order_count++] = value;
}

int bk7258_flash_initialize(void)
{
  g_context.initialize_calls++;
  event(EVENT_INITIALIZE);
  if (g_context.reenter_capture)
    {
      g_context.reenter_capture = false;
      g_context.reenter_capture_result =
        bk7258_reset_marker_capture_previous();
    }

  return g_context.initialize_result;
}

int bk7258_flash_read(uint32_t address, void *buffer, size_t nbytes)
{
  struct context_s *state = &g_context;

  assert(address == 0x8000u);
  assert(nbytes == sizeof(struct record_s));
  state->read_calls++;
  state->last_address = address;
  event(EVENT_READ);
  memcpy(buffer, state->sector, nbytes);
  if (state->corrupt_readback)
    {
      ((uint8_t *)buffer)[0] ^= 1u;
    }

  return state->read_result;
}

int bk7258_flash_erase_sector(uint32_t address)
{
  struct context_s *state = &g_context;

  assert(address == 0x8000u);
  state->erase_calls++;
  state->last_address = address;
  event(EVENT_ERASE);
  memset(state->sector, 0xff, sizeof(state->sector));
  return state->erase_result;
}

int bk7258_flash_write(uint32_t address, const void *buffer, size_t nbytes)
{
  struct context_s *state = &g_context;

  assert(address == 0x8000u);
  assert(nbytes == sizeof(struct record_s));
  state->write_calls++;
  state->last_address = address;
  event(EVENT_WRITE);
  memcpy(state->sector, buffer, nbytes);
  return state->write_result;
}

TEST_UNUSED struct bk7258_storage_config_s make_storage_config(void)
{
  return (struct bk7258_storage_config_s)
  {
    .version = BK7258_STORAGE_CONFIG_VERSION,
    .size = sizeof(struct bk7258_storage_config_s),
    .reset_marker_address = 0x8000u,
    .reset_marker_erase_size = BK7258_RESET_MARKER_ERASE_SIZE,
  };
}

TEST_UNUSED void configure_valid(
  const struct bk7258_storage_config_s *value)
{
  assert(bk7258_storage_configure(value) == 0);
}

TEST_UNUSED void set_record(uint32_t reason, bool valid)
{
  struct record_s record =
  {
    .magic = MARKER_MAGIC,
    .version = MARKER_VERSION,
    .reason = reason,
    .reason_inverse = valid ? ~reason : ~reason ^ 1u,
  };

  memset(g_context.sector, 0xff, sizeof(g_context.sector));
  memcpy(g_context.sector, &record, sizeof(record));
}

TEST_UNUSED void test_unbound(void)
{
  uint32_t reason;

  assert(bk7258_reset_marker_capture_previous() == -EAGAIN);
  assert(bk7258_reset_marker_previous(&reason) == -EAGAIN);
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_WATCHDOG) ==
         -EAGAIN);
}

TEST_UNUSED void test_storage_config(void)
{
  struct bk7258_storage_config_s value = make_storage_config();

  configure_valid(&value);
  assert(bk7258_reset_marker_previous(NULL) == -EINVAL);
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_REBOOT) == -EINVAL);
  assert(g_context.initialize_calls == 0u);
}

TEST_UNUSED void test_capture_valid_and_cache(void)
{
  struct bk7258_storage_config_s value = make_storage_config();
  uint32_t reason = 0u;
  unsigned int reads;
  static const unsigned int expected[] =
  {
    EVENT_INITIALIZE, EVENT_READ, EVENT_ERASE
  };

  set_record(BK7258_RESET_SOURCE_WATCHDOG, true);
  configure_valid(&value);
  g_context.reenter_capture = true;
  assert(bk7258_reset_marker_capture_previous() == 0);
  assert(g_context.reenter_capture_result == -EBUSY);
  assert(memcmp(g_context.order, expected, sizeof(expected)) == 0);
  assert(bk7258_reset_marker_previous(&reason) == 1 &&
         reason == BK7258_RESET_SOURCE_WATCHDOG);
  reads = g_context.read_calls;
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_NMI_WDT) == 0);
  assert(bk7258_reset_marker_capture_previous() == 0);
  assert(g_context.read_calls == reads + 1u);
  assert(bk7258_reset_marker_previous(&reason) == 1 &&
         reason == BK7258_RESET_SOURCE_WATCHDOG);
}

TEST_UNUSED void test_capture_absent_or_torn(void)
{
  struct bk7258_storage_config_s value = make_storage_config();
  uint32_t reason = 0u;

  memset(g_context.sector, 0xff, sizeof(g_context.sector));
#if TEST_RESET_MARKER_SCENARIO == 5
  set_record(BK7258_RESET_SOURCE_WATCHDOG, false);
#endif
  configure_valid(&value);
  assert(bk7258_reset_marker_capture_previous() == 0);
  assert(g_context.erase_calls == 0u);
  assert(bk7258_reset_marker_previous(&reason) == 0);
}

TEST_UNUSED void test_operations_and_failures(void)
{
  struct bk7258_storage_config_s value = make_storage_config();
  uint32_t reason;
  static const unsigned int expected[] =
  {
    EVENT_INITIALIZE, EVENT_ERASE, EVENT_WRITE, EVENT_READ
  };

  configure_valid(&value);
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_WATCHDOG) == 0);
  assert(memcmp(g_context.order, expected, sizeof(expected)) == 0);
  memcpy(&reason, g_context.sector + 8u, sizeof(reason));
  assert(reason == BK7258_RESET_SOURCE_WATCHDOG);
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_REBOOT) == -EINVAL);
  assert(g_context.erase_calls == 1u);

  g_context.initialize_result = 8;
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_WATCHDOG) == -EIO);
  g_context.initialize_result = 0;
  mock_mutex_fail_next(1);
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_WATCHDOG) ==
         -EAGAIN);
  g_context.erase_result = -EIO;
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_WATCHDOG) == -EIO);
  g_context.erase_result = 0;
  g_context.write_result = -EIO;
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_WATCHDOG) == -EIO);
  g_context.write_result = 0;
  g_context.read_result = -EIO;
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_WATCHDOG) == -EIO);
  g_context.read_result = 0;
  g_context.corrupt_readback = true;
  assert(bk7258_reset_marker_stamp(BK7258_RESET_SOURCE_WATCHDOG) == -EIO);
}

int main(void)
{
  memset(&g_context, 0, sizeof(g_context));
#if TEST_RESET_MARKER_SCENARIO == 1
  test_unbound();
#elif TEST_RESET_MARKER_SCENARIO == 2
  test_storage_config();
#elif TEST_RESET_MARKER_SCENARIO == 3
  test_capture_valid_and_cache();
#elif TEST_RESET_MARKER_SCENARIO == 4 || TEST_RESET_MARKER_SCENARIO == 5
  test_capture_absent_or_torn();
#elif TEST_RESET_MARKER_SCENARIO == 6
  test_operations_and_failures();
#else
#  error "Unknown TEST_RESET_MARKER_SCENARIO"
#endif
  puts("bk7258 reset-marker contract test: PASS");
  return 0;
}
