/****************************************************************************
 * Host contract test for BK7258 raw reset-cause error propagation.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <arch/chip/bk7258_reset_cause.h>

volatile uint32_t g_bk7258_test_reset_reg;

static int g_marker_result;
static uint32_t g_marker_reason;

int bk7258_reset_marker_previous(uint32_t *reason)
{
  if (g_marker_result > 0)
    {
      *reason = g_marker_reason;
    }

  return g_marker_result;
}

int main(void)
{
  struct bk7258_reset_cause_raw_s raw;

  g_bk7258_test_reset_reg = BK7258_RESET_SOURCE_REBOOT << 4;
  g_marker_result = -EAGAIN;
  assert(bk7258_reset_cause_read(&raw) == -EAGAIN);

  g_marker_result = 0;
  assert(bk7258_reset_cause_read(&raw) == 0);
  assert(raw.source == BK7258_RESET_SOURCE_REBOOT);
  assert(!raw.from_persistent_flag);

  g_marker_result = 1;
  g_marker_reason = BK7258_RESET_SOURCE_WATCHDOG;
  assert(bk7258_reset_cause_read(&raw) == 0);
  assert(raw.source == BK7258_RESET_SOURCE_REBOOT);
  assert(!raw.from_persistent_flag);

  /* A stale marker must not rewrite a real cold-power event. */

  g_bk7258_test_reset_reg = BK7258_RESET_SOURCE_POWERON << 4;
  assert(bk7258_reset_cause_read(&raw) == 0);
  assert(raw.source == BK7258_RESET_SOURCE_POWERON);
  assert(!raw.from_persistent_flag);

  /* Explicit hardware WDT evidence is corroborated, not replaced. */

  g_bk7258_test_reset_reg = BK7258_RESET_SOURCE_NMI_WDT << 4;
  assert(bk7258_reset_cause_read(&raw) == 0);
  assert(raw.source == BK7258_RESET_SOURCE_NMI_WDT);
  assert(raw.from_persistent_flag);

  /* A confirmed marker may recover only an undocumented hardware value. */

  g_bk7258_test_reset_reg = 0x7fu << 4;
  assert(bk7258_reset_cause_read(&raw) == 0);
  assert(raw.source == BK7258_RESET_SOURCE_WATCHDOG);
  assert(raw.from_persistent_flag);

  /* Even a structurally valid record cannot inject a non-WDT source. */

  g_marker_reason = BK7258_RESET_SOURCE_REBOOT;
  assert(bk7258_reset_cause_read(&raw) == 0);
  assert(raw.source == 0x7fu);
  assert(!raw.from_persistent_flag);

  assert(bk7258_reset_cause_read(NULL) == -EINVAL);
  puts("bk7258 reset-cause contract test: PASS");
  return 0;
}
