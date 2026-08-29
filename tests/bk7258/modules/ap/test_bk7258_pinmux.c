/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <nuttx/irq.h>

#include "bk7258_pinmux.h"

#include "mock_reg32.h"

#define SELECTOR_BASE 0x440100c0u
#define GPIO_BASE     0x44000400u
#define PERIPHERAL    (1u << 6)

static uint32_t g_acquire_result;
static uint32_t g_release_result;
static unsigned int g_acquire_count;
static unsigned int g_release_count;
static unsigned int g_irq_save_count;
static unsigned int g_irq_restore_count;

uint32_t sys_amp_res_acquire(void)
{
  g_acquire_count++;
  return g_acquire_result;
}

uint32_t sys_amp_res_release(void)
{
  g_release_count++;
  return g_release_result;
}

irqstate_t up_irq_save(void)
{
  g_irq_save_count++;
  return 0x7258u;
}

void up_irq_restore(irqstate_t flags)
{
  assert(flags == 0x7258u);
  g_irq_restore_count++;
}

static void reset_fixture(void)
{
  mock_reg32_reset();
  g_acquire_result = 0;
  g_release_result = 0;
  g_acquire_count = 0;
  g_release_count = 0;
  g_irq_save_count = 0;
  g_irq_restore_count = 0;
}

static void test_validation_is_transactional(void)
{
  const struct bk7258_pinmux_config_s invalid[] =
  {
    { 1, 2, true },
    { 56, 0, false },
  };

  reset_fixture();
  mock_reg32_set(SELECTOR_BASE, 0x12345678u);
  assert(bk7258_pinmux_apply(NULL, 1) == -EINVAL);
  assert(bk7258_pinmux_apply(invalid, 2) == -ERANGE);
  assert(mock_reg32_read(SELECTOR_BASE) == 0x12345678u);
  assert(g_acquire_count == 0);
}

static void test_batch_updates_selector_and_pad(void)
{
  const struct bk7258_pinmux_config_s configs[] =
  {
    { 1, 3, true },
    { 7, 10, false },
    { 9, 4, true },
  };

  reset_fixture();
  mock_reg32_set(SELECTOR_BASE, 0xffffffffu);
  mock_reg32_set(SELECTOR_BASE + 4u, 0u);
  mock_reg32_set(GPIO_BASE + 1u * 4u, 0x100u);
  mock_reg32_set(GPIO_BASE + 7u * 4u, 0x1ffu);
  assert(bk7258_pinmux_apply(configs, 3) == 0);
  assert(mock_reg32_read(SELECTOR_BASE) == 0xafffff3fu);
  assert(mock_reg32_read(SELECTOR_BASE + 4u) == 0x40u);
  assert(mock_reg32_read(GPIO_BASE + 1u * 4u) == (0x100u | PERIPHERAL));
  assert(mock_reg32_read(GPIO_BASE + 7u * 4u) == (0x1ffu & ~PERIPHERAL));
  assert(mock_reg32_read(GPIO_BASE + 9u * 4u) == PERIPHERAL);
  assert(g_acquire_count == 1 && g_release_count == 1);
  assert(g_irq_save_count == 1 && g_irq_restore_count == 1);
}

static void test_lock_failures_are_reported(void)
{
  const struct bk7258_pinmux_config_s config = { 2, 1, true };

  reset_fixture();
  g_acquire_result = 1;
  assert(bk7258_pinmux_apply(&config, 1) == -EBUSY);
  assert(g_release_count == 0 && g_irq_save_count == 0);
  assert(mock_reg32_read(SELECTOR_BASE) == 0);

  reset_fixture();
  g_release_result = 1;
  assert(bk7258_pinmux_apply(&config, 1) == -EIO);
  assert(g_release_count == 1 && g_irq_restore_count == 1);
}

int main(void)
{
  test_validation_is_transactional();
  test_batch_updates_selector_and_pad();
  test_lock_failures_are_reported();
  puts("BK7258_PINMUX_TEST_PASS cases=3");
  return 0;
}
