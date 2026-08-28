/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_camera_phase0.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK AI Toy GC2145 Phase 0 identity probe.  This is intentionally not a
 * sensor driver: it powers the module, reads only ID registers 0xf0/0xf1,
 * reports the result, and returns the module to its powered-off state.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_CAMERA_PHASE0

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/signal.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_pm.h>

#include <driver/gpio.h>
#include <driver/i2c.h>

/* These v3.1.1.9 GPIO mux helpers are exported by the immutable driver
 * archive, but their private declarations are not copied into the bundle.
 */

extern bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev);
extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);

#define AIDK_CAMERA_POWER_SETTLE_US  10000u
#define AIDK_CAMERA_MCLK_SETTLE_US   40000u
#define AIDK_CAMERA_RESET_SETTLE_US 100000u
#define AIDK_CAMERA_I2C_TIMEOUT_MS     100u
#define AIDK_CAMERA_SYS_REG_BASE       0x44010000u
#define AIDK_CAMERA_GPIO_REG_BASE      0x44000400u
#define AIDK_CAMERA_GPIO_CFG_BASE      (AIDK_CAMERA_SYS_REG_BASE + 0xc0u)
#define AIDK_CAMERA_GPIO_PER_MUX_REG   8u
#define AIDK_CAMERA_GPIO_MUX_WIDTH     4u
#define AIDK_CAMERA_I2C1_PAD_MODE      0u
#define AIDK_CAMERA_MCLK_PAD_MODE      1u
#define AIDK_CAMERA_GPIO_2_FUNC_EN     (1u << 6)

#define AIDK_CAMERA_REG32(address) \
  (*(FAR volatile uint32_t *)(uintptr_t)(address))

/* Phase 0 is deliberately pinned to the reviewed schematic facts.  A later
 * board-header edit cannot silently move an electrical control signal.
 */

#if BK7258_BOARD_DVP_I2C_BUS != 1 || \
    BK7258_BOARD_DVP_I2C_MAP_MODE != 1 || \
    BK7258_BOARD_DVP_I2C_SCL_GPIO != 42 || \
    BK7258_BOARD_DVP_I2C_SDA_GPIO != 43
#  error "AIDK GC2145 SCCB must use I2C1 map mode 1 on P42/P43"
#endif

#if BK7258_BOARD_DVP_POWER_GPIO != 49 || \
    BK7258_BOARD_DVP_POWER_ACTIVE_HIGH != 1
#  error "AIDK camera power must be active-high DVP_PWR_CTL on P49"
#endif

#if BK7258_BOARD_DVP_RESET_GPIO != 28 || \
    BK7258_BOARD_DVP_RESET_ACTIVE_LOW != 1
#  error "AIDK camera reset must be active-low DVP_RST on P28"
#endif

#if BK7258_BOARD_DVP_MCLK_GPIO != 27 || \
    BK7258_BOARD_DVP_PWDN_AVAILABLE != 0
#  error "AIDK camera must use P27 MCLK and have no MCU-controlled PWDNB"
#endif

#ifndef CONFIG_CAMERA_PWR_ACTIVE_HIGH
#  error "AIDK GC2145 Phase 0 requires CONFIG_CAMERA_PWR_ACTIVE_HIGH=y"
#endif

static int aidk_camera_gpio_output(gpio_id_t pin, bool high)
{
  bk_err_t error;

  error = gpio_dev_unmap(pin);
  if (error != BK_OK)
    {
      return -EIO;
    }

  if (bk_gpio_disable_input(pin) != BK_OK ||
      (high ? bk_gpio_set_output_high(pin) :
              bk_gpio_set_output_low(pin)) != BK_OK ||
      bk_gpio_enable_output(pin) != BK_OK)
    {
      return -EIO;
    }

  return OK;
}

static bool aidk_camera_pinmux_matches(gpio_id_t pin, uint32_t mode)
{
  uintptr_t address = AIDK_CAMERA_GPIO_CFG_BASE +
                      (uint32_t)pin / AIDK_CAMERA_GPIO_PER_MUX_REG *
                      sizeof(uint32_t);
  uint32_t shift = (uint32_t)pin % AIDK_CAMERA_GPIO_PER_MUX_REG *
                   AIDK_CAMERA_GPIO_MUX_WIDTH;
  uint32_t pad = AIDK_CAMERA_REG32(AIDK_CAMERA_GPIO_REG_BASE +
                                   (uint32_t)pin * sizeof(uint32_t));
  uint32_t mux = AIDK_CAMERA_REG32(address);

  /* gpio_dev_map() discards the lower-level mapper's status in v3.1.1.9.
   * Verify both selector and peripheral-enable bit before powering a bus or
   * clock; do not force the shared register from board code.
   */

  return ((mux >> shift) & 0x0fu) == mode &&
         (pad & AIDK_CAMERA_GPIO_2_FUNC_EN) != 0;
}

static int aidk_camera_read_register(uint8_t reg, uint8_t *value)
{
  i2c_mem_param_t param;
  bk_err_t error;

  param.dev_addr = BK7258_BOARD_GC2145_I2C_ADDRESS;
  param.mem_addr = reg;
  param.mem_addr_size = I2C_MEM_ADDR_SIZE_8BIT;
  param.data = value;
  param.data_size = 1;
  param.timeout_ms = AIDK_CAMERA_I2C_TIMEOUT_MS;

  error = bk_i2c_memory_read((i2c_id_t)BK7258_BOARD_DVP_I2C_BUS,
                             &param);
  return error == BK_OK ? OK : -EIO;
}

int bk7258_aidk_camera_phase0_probe(void)
{
  const gpio_id_t power = (gpio_id_t)BK7258_BOARD_DVP_POWER_GPIO;
  const gpio_id_t reset = (gpio_id_t)BK7258_BOARD_DVP_RESET_GPIO;
  const gpio_id_t mclk = (gpio_id_t)BK7258_BOARD_DVP_MCLK_GPIO;
  const gpio_id_t scl = (gpio_id_t)BK7258_BOARD_DVP_I2C_SCL_GPIO;
  const gpio_id_t sda = (gpio_id_t)BK7258_BOARD_DVP_I2C_SDA_GPIO;
  i2c_config_t config;
  uint8_t id_high = 0;
  uint8_t id_low = 0;
  uint16_t id = 0;
  bool driver_initialized = false;
  bool i2c_initialized = false;
  bool mclk_held = false;
  int ret = OK;

  if (bk_gpio_driver_init() != BK_OK)
    {
      ret = -EIO;
      goto out;
    }

  /* R31 already biases power off.  Claim both outputs in their safe state
   * before applying power so reset stays asserted throughout rail startup.
   */

  ret = aidk_camera_gpio_output(reset, false);
  if (ret < 0)
    {
      goto out;
    }

  ret = aidk_camera_gpio_output(power, false);
  if (ret < 0)
    {
      goto out;
    }

  if (gpio_dev_unmap(scl) != BK_OK ||
      gpio_dev_unmap(sda) != BK_OK ||
      gpio_dev_map(scl, GPIO_DEV_I2C1_SCL) != BK_OK ||
      gpio_dev_map(sda, GPIO_DEV_I2C1_SDA) != BK_OK ||
      !aidk_camera_pinmux_matches(scl, AIDK_CAMERA_I2C1_PAD_MODE) ||
      !aidk_camera_pinmux_matches(sda, AIDK_CAMERA_I2C1_PAD_MODE))
    {
      ret = -EIO;
      goto out;
    }

  if (bk_gpio_set_output_high(power) != BK_OK)
    {
      ret = -EIO;
      goto out;
    }

  (void)nxsig_usleep(AIDK_CAMERA_POWER_SETTLE_US);

  if (gpio_dev_unmap(mclk) != BK_OK ||
      gpio_dev_map(mclk, GPIO_DEV_CLK_AUXS_CIS) != BK_OK ||
      !aidk_camera_pinmux_matches(mclk, AIDK_CAMERA_MCLK_PAD_MODE))
    {
      ret = -EIO;
      goto out;
    }

  ret = bk7258_pm_clock_get(BK7258_PM_CLOCK_CAMERA_MCLK_24M);
  if (ret < 0)
    {
      goto out;
    }

  mclk_held = true;
  (void)nxsig_usleep(AIDK_CAMERA_MCLK_SETTLE_US);

  if (bk_gpio_set_output_high(reset) != BK_OK)
    {
      ret = -EIO;
      goto out;
    }

  (void)nxsig_usleep(AIDK_CAMERA_RESET_SETTLE_US);

  if (bk_i2c_driver_init() != BK_OK)
    {
      ret = -EIO;
      goto out;
    }

  driver_initialized = true;
  config.baud_rate = BK7258_BOARD_DVP_I2C_FREQUENCY;
  config.addr_mode = I2C_ADDR_MODE_7BIT;
  config.slave_addr = 0;

  if (bk_i2c_init((i2c_id_t)BK7258_BOARD_DVP_I2C_BUS, &config) != BK_OK)
    {
      ret = -EIO;
      goto out;
    }

  i2c_initialized = true;

  /* Phase 0 contract: these are the only two sensor register accesses. */

  ret = aidk_camera_read_register(BK7258_BOARD_GC2145_ID_HIGH_REG,
                                  &id_high);
  if (ret < 0)
    {
      goto out;
    }

  ret = aidk_camera_read_register(BK7258_BOARD_GC2145_ID_LOW_REG,
                                  &id_low);
  if (ret < 0)
    {
      goto out;
    }

  id = ((uint16_t)id_high << 8) | id_low;
  if (id_high != BK7258_BOARD_GC2145_ID_HIGH_VALUE ||
      id_low != BK7258_BOARD_GC2145_ID_LOW_VALUE)
    {
      syslog(LOG_ERR,
             "AIDK GC2145 phase0 FAIL: id=0x%04x expected=0x%04x\n",
             id, BK7258_BOARD_GC2145_ID);
      ret = -ENODEV;
      goto out;
    }

out:
  if (i2c_initialized &&
      bk_i2c_deinit((i2c_id_t)BK7258_BOARD_DVP_I2C_BUS) != BK_OK &&
      ret == OK)
    {
      ret = -EIO;
    }

  if (driver_initialized && bk_i2c_driver_deinit() != BK_OK && ret == OK)
    {
      ret = -EIO;
    }

  (void)bk_gpio_set_output_low(reset);

  if (mclk_held)
    {
      int pm_ret = bk7258_pm_clock_put(BK7258_PM_CLOCK_CAMERA_MCLK_24M);

      if (pm_ret < 0 && ret == OK)
        {
          ret = pm_ret;
        }
    }

  (void)gpio_dev_unmap(mclk);
  (void)bk_gpio_set_output_low(power);
  (void)gpio_dev_unmap(scl);
  (void)gpio_dev_unmap(sda);

  if (ret == OK)
    {
      syslog(LOG_INFO,
             "AIDK GC2145 phase0 PASS: addr=0x%02x id=0x%04x\n",
             BK7258_BOARD_GC2145_I2C_ADDRESS, id);
    }
  else if (ret != -ENODEV)
    {
      syslog(LOG_ERR, "AIDK GC2145 phase0 FAIL: %d\n", ret);
    }

  return ret;
}

#endif /* CONFIG_BK7258_AIDK_CAMERA_PHASE0 */
