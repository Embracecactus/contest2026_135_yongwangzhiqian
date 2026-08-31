/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <nuttx/irq.h>

#include <driver/gpio.h>

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
static bool g_gpio_input;
static bool g_gpio_output;
static bool g_gpio_output_enabled;
static gpio_id_t g_irq_pin;
static gpio_int_type_t g_irq_type;
static gpio_isr_t g_irq_handler;
static unsigned int g_irq_clear_count;
static bool g_irq_enabled;
static unsigned int g_public_irq_count;
static uint8_t g_public_irq_pin;
static FAR void *g_public_irq_arg;
static uint32_t g_ldo_module;
static gpio_id_t g_ldo_pin;
static gpio_output_state_e g_ldo_state;

bk_err_t bk_gpio_driver_init(void)
{
  return 0;
}

bk_err_t bk_gpio_enable_output(gpio_id_t gpio_id)
{
  (void)gpio_id;
  g_gpio_output_enabled = true;
  return 0;
}

bk_err_t bk_gpio_disable_output(gpio_id_t gpio_id)
{
  (void)gpio_id;
  g_gpio_output_enabled = false;
  return 0;
}

bk_err_t bk_gpio_enable_input(gpio_id_t gpio_id)
{
  (void)gpio_id;
  return 0;
}

bk_err_t bk_gpio_disable_input(gpio_id_t gpio_id)
{
  (void)gpio_id;
  return 0;
}

bk_err_t bk_gpio_disable_pull(gpio_id_t gpio_id)
{
  (void)gpio_id;
  return 0;
}

bk_err_t bk_gpio_pull_up(gpio_id_t gpio_id)
{
  (void)gpio_id;
  return 0;
}

bk_err_t bk_gpio_pull_down(gpio_id_t gpio_id)
{
  (void)gpio_id;
  return 0;
}

bk_err_t bk_gpio_set_output_value(gpio_id_t gpio_id, bool value)
{
  (void)gpio_id;
  g_gpio_output = value;
  return 0;
}

bool bk_gpio_set_capacity(gpio_id_t gpio_id, uint32_t capacity)
{
  (void)gpio_id;
  (void)capacity;
  return false;
}

bool bk_gpio_get_input(gpio_id_t gpio_id)
{
  (void)gpio_id;
  return g_gpio_input;
}

bool bk_gpio_get_output(gpio_id_t gpio_id)
{
  (void)gpio_id;
  return g_gpio_output;
}

bk_err_t bk_gpio_set_interrupt_type(gpio_id_t gpio_id,
                                    gpio_int_type_t type)
{
  g_irq_pin = gpio_id;
  g_irq_type = type;
  return 0;
}

bk_err_t bk_gpio_enable_interrupt(gpio_id_t gpio_id)
{
  g_irq_pin = gpio_id;
  g_irq_enabled = true;
  return 0;
}

bk_err_t bk_gpio_disable_interrupt(gpio_id_t gpio_id)
{
  g_irq_pin = gpio_id;
  g_irq_enabled = false;
  return 0;
}

bk_err_t bk_gpio_clear_interrupt(gpio_id_t gpio_id)
{
  g_irq_pin = gpio_id;
  g_irq_clear_count++;
  return 0;
}

bk_err_t bk_gpio_register_isr(gpio_id_t gpio_id, gpio_isr_t isr)
{
  g_irq_pin = gpio_id;
  g_irq_handler = isr;
  return 0;
}

bk_err_t bk_pm_module_vote_ctrl_external_ldo(
  uint32_t module, gpio_id_t gpio_id, gpio_output_state_e value)
{
  g_ldo_module = module;
  g_ldo_pin = gpio_id;
  g_ldo_state = value;
  return 0;
}

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
  g_gpio_input = false;
  g_gpio_output = false;
  g_gpio_output_enabled = false;
  g_irq_pin = UINT32_MAX;
  g_irq_type = GPIO_INT_TYPE_FALLING_EDGE;
  g_irq_handler = NULL;
  g_irq_clear_count = 0;
  g_irq_enabled = false;
  g_public_irq_count = 0;
  g_public_irq_pin = UINT8_MAX;
  g_public_irq_arg = NULL;
  g_ldo_module = UINT32_MAX;
  g_ldo_pin = UINT32_MAX;
  g_ldo_state = GPIO_OUTPUT_STATE_INVALID;
}

static void test_public_irq_callback(uint8_t pin, FAR void *arg)
{
  g_public_irq_count++;
  g_public_irq_pin = pin;
  g_public_irq_arg = arg;
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

static void test_gpio_and_shared_rail_hide_sdk_contract(void)
{
  bool high;

  reset_fixture();
  assert(bk7258_gpio_configure_output(25, true,
                                      BK7258_GPIO_DRIVE_0) == 0);
  assert(g_gpio_output);
  assert(g_gpio_output_enabled);
  assert(bk7258_gpio_read_output(25, &high) == 0 && high);

  g_gpio_input = true;
  assert(bk7258_gpio_configure_input(26, BK7258_GPIO_PULL_UP) == 0);
  assert(bk7258_gpio_read_input(26, &high) == 0 && high);

  assert(bk7258_shared_rail_vote(BK7258_SHARED_RAIL_SDIO, 52, true) == 0);
  assert(g_ldo_module == GPIO_CTRL_LDO_MODULE_SDIO);
  assert(g_ldo_pin == 52);
  assert(g_ldo_state == GPIO_OUTPUT_STATE_HIGH);
  assert(bk7258_shared_rail_vote(BK7258_SHARED_RAIL_NFC, 52, false) == 0);
  assert(g_ldo_module == GPIO_CTRL_LDO_MODULE_NFC);
  assert(g_ldo_state == GPIO_OUTPUT_STATE_LOW);

  assert(bk7258_gpio_configure_output(56, false,
                                      BK7258_GPIO_DRIVE_0) == -ERANGE);
  assert(bk7258_gpio_read_input(0, NULL) == -EINVAL);
}

static void test_open_drain_fast_path_and_irq_hide_sdk_contract(void)
{
  uintptr_t pad = GPIO_BASE + 13u * 4u;
  uint32_t token = 0x1234u;

  reset_fixture();
  assert(bk7258_gpio_configure_open_drain(
           13, BK7258_GPIO_PULL_UP) == 0);
  assert(!g_gpio_output_enabled);
  assert(bk7258_gpio_open_drain_write(13, false) == 0);
  assert(g_gpio_output_enabled);
  assert(bk7258_gpio_open_drain_write(13, true) == 0);
  assert(!g_gpio_output_enabled);

  assert(bk7258_gpio_fast_write(13, true) == 0);
  assert(mock_reg32_read(pad) == 0x2u);
  assert(bk7258_gpio_fast_write(13, false) == 0);
  assert(mock_reg32_read(pad) == 0u);
  assert(bk7258_gpio_fast_release_pullup(13) == 0);
  assert(mock_reg32_read(pad) == 0x3eu);
  assert(bk7258_gpio_fast_release_pullup(56) == -ERANGE);

  assert(bk7258_gpio_irq_configure(
           14, BK7258_GPIO_PULL_UP, BK7258_GPIO_IRQ_FALLING_EDGE,
           test_public_irq_callback, &token) == 0);
  assert(g_irq_pin == 14);
  assert(g_irq_type == GPIO_INT_TYPE_FALLING_EDGE);
  assert(g_irq_handler != NULL);
  g_irq_handler(14);
  assert(g_public_irq_count == 1);
  assert(g_public_irq_pin == 14);
  assert(g_public_irq_arg == &token);

  assert(bk7258_gpio_irq_enable(14, true) == 0);
  assert(g_irq_enabled && g_irq_clear_count == 1);
  assert(bk7258_gpio_irq_enable(14, false) == 0);
  assert(!g_irq_enabled && g_irq_clear_count == 2);
}

int main(void)
{
  test_validation_is_transactional();
  test_batch_updates_selector_and_pad();
  test_lock_failures_are_reported();
  test_gpio_and_shared_rail_hide_sdk_contract();
  test_open_drain_fast_path_and_irq_hide_sdk_contract();
  puts("BK7258_PINMUX_TEST_PASS cases=5");
  return 0;
}
