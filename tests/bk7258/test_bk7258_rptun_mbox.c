/****************************************************************************
 * tests/test_bk7258_rptun_mbox.c
 *
 * Host-side unit tests for the RPTUN mailbox *notify* state machine in
 * bk7258_rptun_mbox.c (the CP/AP MBOX0 transport wrapper).
 *
 * Strategy
 * --------
 * The REAL bk7258_rptun_mbox.c is compiled unmodified (apart from neutralizing
 * the ARM-only "dmb sy" barrier in a throwaway copy) against the mock SDK /
 * NuttX / arch headers in tests/mocks/.  The transport is fully controllable,
 * so we can deterministically exercise the coalescing, -EAGAIN retry, and
 * "lost TX-complete" recovery paths without any hardware or QEMU.
 *
 * The wrapper's outgoing sends are observed via the mocked mb_chnl_write()
 * (last command + write counter); its incoming dispatch is observed through
 * the notify callback we register.  We never modify the firmware/SDK.
 *
 * Build: see tests/Makefile (run `make` in this directory).
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "bk7258_rptun_mbox.h"   /* copied verbatim into build/ by the Makefile */
#include "mock_sdk.h"

/* ------------------------------------------------------------------ */
/* Tiny test framework                                                */
/* ------------------------------------------------------------------ */

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                    \
  do                                                                        \
    {                                                                       \
      g_checks++;                                                           \
      if (!(cond))                                                          \
        {                                                                   \
          g_failures++;                                                     \
          fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);            \
          fprintf(stderr, __VA_ARGS__);                                     \
          fprintf(stderr, "\n");                                            \
        }                                                                   \
    }                                                                       \
  while (0)

/* ------------------------------------------------------------------ */
/* Notify callback recorder (observes the incoming dispatch path)     */
/* ------------------------------------------------------------------ */

typedef struct
{
  uint32_t generation;
  uint32_t value;
} notify_rec_t;

#define MAX_RECS 128
static notify_rec_t g_recs[MAX_RECS];
static int g_rec_count = 0;
static pthread_mutex_t g_rec_lock = PTHREAD_MUTEX_INITIALIZER;

static void test_notify_cb(uint32_t generation, uint32_t value)
{
  pthread_mutex_lock(&g_rec_lock);
  if (g_rec_count < MAX_RECS)
    {
      g_recs[g_rec_count].generation = generation;
      g_recs[g_rec_count].value = value;
      g_rec_count++;
    }
  pthread_mutex_unlock(&g_rec_lock);
}

static void clear_recs(void)
{
  pthread_mutex_lock(&g_rec_lock);
  g_rec_count = 0;
  pthread_mutex_unlock(&g_rec_lock);
}

static bool has_rec(uint32_t generation, uint32_t value)
{
  bool found = false;
  pthread_mutex_lock(&g_rec_lock);
  for (int i = 0; i < g_rec_count; i++)
    {
      if (g_recs[i].generation == generation && g_recs[i].value == value)
        {
          found = true;
          break;
        }
    }
  pthread_mutex_unlock(&g_rec_lock);
  return found;
}

static bool has_value(uint32_t value)
{
  bool found = false;
  pthread_mutex_lock(&g_rec_lock);
  for (int i = 0; i < g_rec_count; i++)
    {
      if (g_recs[i].value == value)
        {
          found = true;
          break;
        }
    }
  pthread_mutex_unlock(&g_rec_lock);
  return found;
}

/* Drain any in-flight queued notify, then zero the observation baseline so
 * each test starts from a clean, quiescent state. */
static void test_setup(bool tx_complete)
{
  mock_mbox_set_busy(false);
  mock_mbox_set_tx_complete(tx_complete);
  usleep(30000);          /* let the worker deliver anything still queued */
  clear_recs();
  mock_mbox_reset();      /* zeroes last write + write count, tx_complete=true */
  if (!tx_complete)
    {
      mock_mbox_set_tx_complete(false);
    }
}

/* ------------------------------------------------------------------ */
/* Test 1: send before initialization returns -ENODEV                 */
/* (must run before bk7258_rptun_mbox_initialize)                     */
/* ------------------------------------------------------------------ */

static void test_pre_init(void)
{
  int r = bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_NOTIFY, 1, 0x10);
  CHECK(r == -ENODEV, "pre-init send -> -ENODEV (got %d)", r);

  r = bk7258_rptun_mbox_notify(1, 0x10);
  CHECK(r == -ENODEV, "pre-init notify -> -ENODEV (got %d)", r);
}

/* ------------------------------------------------------------------ */
/* Test 2: a single notify is delivered on the wire (no contention)   */
/* ------------------------------------------------------------------ */

static void test_single_notify(void)
{
  test_setup(true);
  mock_mbox_set_boot_generation(1);

  int r = bk7258_rptun_mbox_notify(1, 0x10);
  CHECK(r == 0, "single notify -> OK (got %d)", r);

  uint32_t type = 0;
  uint32_t gen = 0;
  uint32_t val = 0;
  mock_mbox_get_last_write(&type, &gen, &val);
  CHECK(type == BK7258_RPTUN_MBOX_NOTIFY, "wire type == NOTIFY (got %u)", type);
  CHECK(gen == 1, "wire generation == 1 (got %u)", gen);
  CHECK(val == 0x10, "wire value == 0x10 (got 0x%x)", val);
}

/* ------------------------------------------------------------------ */
/* Test 3: outgoing notifies of the same generation are OR-coalesced  */
/* ------------------------------------------------------------------ */

static void test_outgoing_coalesce(void)
{
  test_setup(false);              /* tx_complete off to isolate the path */
  mock_mbox_set_busy(true);
  mock_mbox_set_boot_generation(1);

  int before = mock_mbox_write_count();

  /* Both notifies hit a busy slot -> queued (returns OK), no wire write yet. */
  int r1 = bk7258_rptun_mbox_notify(1, 0x01);
  int r2 = bk7258_rptun_mbox_notify(1, 0x02);
  CHECK(r1 == 0, "notify A returns OK (queued) (got %d)", r1);
  CHECK(r2 == 0, "notify B returns OK (queued) (got %d)", r2);
  CHECK(mock_mbox_write_count() == before,
        "no successful wire write while slot busy");

  /* Free the slot; the worker's poll re-arms retry_notify and sends. */
  mock_mbox_set_busy(false);
  CHECK(mock_mbox_wait_for_write(500) == 0, "wire write occurred after slot freed");

  uint32_t type = 0;
  uint32_t gen = 0;
  uint32_t val = 0;
  mock_mbox_get_last_write(&type, &gen, &val);
  CHECK(type == BK7258_RPTUN_MBOX_NOTIFY, "coalesced wire type == NOTIFY");
  CHECK(val == (0x01u | 0x02u),
        "coalesced value == OR(0x01,0x02)=0x03 (got 0x%x)", val);
  CHECK(mock_mbox_write_count() == before + 1,
        "exactly one wire write after recovery (got %d, expected %d)",
        mock_mbox_write_count(), before + 1);
}

/* ------------------------------------------------------------------ */
/* Test 4: -EAGAIN recovery (busy slot, then free) delivers the value */
/* ------------------------------------------------------------------ */

static void test_eagain_recovery(void)
{
  test_setup(false);
  mock_mbox_set_busy(true);
  mock_mbox_set_boot_generation(1);

  int before = mock_mbox_write_count();

  int r = bk7258_rptun_mbox_notify(1, 0xAA);
  CHECK(r == 0, "notify queued OK despite busy (got %d)", r);
  CHECK(mock_mbox_write_count() == before, "no wire write while busy");

  mock_mbox_set_busy(false);
  CHECK(mock_mbox_wait_for_write(500) == 0, "wire write after recovery");

  uint32_t type = 0;
  uint32_t gen = 0;
  uint32_t val = 0;
  mock_mbox_get_last_write(&type, &gen, &val);
  CHECK(val == 0xAA, "recovered wire value == 0xAA (got 0x%x)", val);
  CHECK(mock_mbox_write_count() == before + 1,
        "exactly one wire write after recovery (got %d)", mock_mbox_write_count());
}

/* ------------------------------------------------------------------ */
/* Test 5: "stranded" notify (TX-complete interrupt lost) is NOT lost */
/*                                                                        */
/* Reproduces the vring-leak precondition: a notify is queued, the slot */
/* is busy, and the SDK never fires TX-complete.  The implementation must */
/* still deliver the value because its worker polls every 1 ms and re-arms */
/* retry_notify.  If the worker were purely event-driven on TX-complete,   */
/* this value would be stranded -> exactly the leak precondition.         */
/* ------------------------------------------------------------------ */

static void test_stranded_notify(void)
{
  test_setup(false);              /* tx_complete stays OFF the whole time */
  mock_mbox_set_busy(true);
  mock_mbox_set_boot_generation(1);

  int before = mock_mbox_write_count();

  int r = bk7258_rptun_mbox_notify(1, 0x55);
  CHECK(r == 0, "notify queued OK (got %d)", r);
  /* No TX-complete will ever fire; only the poll can recover it. */
  CHECK(mock_mbox_write_count() == before, "no wire write while busy");

  mock_mbox_set_busy(false);
  CHECK(mock_mbox_wait_for_write(500) == 0,
        "notify delivered despite LOST tx-complete (poll recovered it)");

  uint32_t type = 0;
  uint32_t gen = 0;
  uint32_t val = 0;
  mock_mbox_get_last_write(&type, &gen, &val);
  CHECK(val == 0x55, "stranded notify value preserved == 0x55 (got 0x%x)", val);
  CHECK(mock_mbox_write_count() == before + 1,
        "exactly one wire write after recovery (got %d)", mock_mbox_write_count());
}

/* ------------------------------------------------------------------ */
/* Test 6: invalid arguments return -EINVAL                           */
/* ------------------------------------------------------------------ */

static void test_invalid_args(void)
{
  test_setup(true);
  mock_mbox_set_boot_generation(1);

  int r;

  r = bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_INVALID, 1, 0x10);
  CHECK(r == -EINVAL, "send(INVALID type) -> -EINVAL (got %d)", r);

  r = bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_NOTIFY, 0, 0x10);
  CHECK(r == -EINVAL, "send(generation==0) -> -EINVAL (got %d)", r);

  r = bk7258_rptun_mbox_notify(0, 0x10);
  CHECK(r == -EINVAL, "notify(generation==0) -> -EINVAL (got %d)", r);

  /* Sanity: a valid send still works. */
  r = bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_NOTIFY, 1, 0x10);
  CHECK(r == 0, "valid send -> OK (got %d)", r);
}

/* ------------------------------------------------------------------ */
/* Test 7: incoming same-type coalescing is last-value-wins           */
/*                                                                        */
/* Two incoming NOTIFY commands of the same generation arrive before the */
/* worker dispatches them.  The wrapper overwrites its per-type message   */
/* slot (it does NOT OR-accumulate incoming), so only the last value is   */
/* delivered.  We use the generation gate to suppress dispatch until both */
/* are stored, guaranteeing determinism.                                  */
/* ------------------------------------------------------------------ */

static void test_incoming_coalesce(void)
{
  test_setup(true);
  clear_recs();

  /* generation 0 -> dispatch() skips every message (gate mismatch),
   * so A and B are only stored, never delivered yet. */
  mock_mbox_set_boot_generation(0);
  mock_mbox_set_boot_state(BK7258_AP_STATE_READY);

  mock_mbox_inject_rx(BK7258_RPTUN_MBOX_NOTIFY, 5, 0x11);
  mock_mbox_inject_rx(BK7258_RPTUN_MBOX_NOTIFY, 5, 0x22);

  /* Now open the gate and kick the worker with a third, identical message
   * so it dispatches the stored (last) value. */
  mock_mbox_set_boot_generation(5);
  mock_mbox_inject_rx(BK7258_RPTUN_MBOX_NOTIFY, 5, 0x22);

  usleep(50000);   /* let the worker run */

  CHECK(has_rec(5, 0x22),
        "incoming coalesced delivery carries LAST value 0x22");
  CHECK(!has_value(0x11),
        "first incoming value 0x11 was overwritten (never delivered)");
  CHECK(!has_value(0x33),
        "incoming is NOT OR-coalesced (would be 0x33)");
  CHECK(!has_value(0x11 | 0x22),
        "incoming is last-value-wins, not bitwise-OR");

  /* Stop further timeout-poll callbacks now that generation is non-zero. */
  mock_mbox_set_boot_generation(0);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
  printf("== bk7258_rptun_mbox notify state-machine unit tests ==\n");

  /* Test 1 must run before the module is initialized. */
  test_pre_init();

  int r = bk7258_rptun_mbox_initialize();
  if (r != 0)
    {
      fprintf(stderr, "FATAL: bk7258_rptun_mbox_initialize() failed: %d\n", r);
      return 2;
    }

  bk7258_rptun_mbox_set_notify(test_notify_cb);

  test_single_notify();
  test_outgoing_coalesce();
  test_eagain_recovery();
  test_stranded_notify();
  test_invalid_args();
  test_incoming_coalesce();

  mock_mbox_fini();

  printf("\n%s: %d/%d checks failed\n",
         g_failures == 0 ? "PASS" : "FAIL", g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
