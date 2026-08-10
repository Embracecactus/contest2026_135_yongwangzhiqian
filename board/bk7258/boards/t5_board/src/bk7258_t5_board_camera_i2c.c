/****************************************************************************
 * board/bk7258/boards/t5_board/src/bk7258_t5_board_camera_i2c.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board V1.0.2 DVP sensor-control I2C adapter.
 *
 * BK7258 has two hardware I2C controllers, while the P10 camera connector
 * uses GPIO13/GPIO15 and therefore reaches the SDK through simulated I2C2.
 * The immutable v3.1.1.9 simulated driver uses a CPU-cycle delay calibrated
 * for a different clock.  This board-owned link wrapper keeps the SDK DVP
 * implementation intact while providing clock-independent 10 us half cycles
 * for I2C2.  Hardware I2C0/I2C1 continue to use the original SDK functions.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_pm.h>

#include <components/dvp_camera_types.h>
#include <driver/gpio.h>
#include <driver/i2c.h>

#define T5_CAMERA_I2C_ID             BK7258_BOARD_DVP_I2C_BUS
#define T5_CAMERA_I2C_HALF_PERIOD_US 10u
#define T5_CAMERA_RESET_SETTLE_US    20000u
#define T5_CAMERA_RESET_ASSERT_US    100000u
#define T5_CAMERA_RESET_RELEASE_US   100000u
#define T5_CAMERA_MCLK_SETTLE_US     40000u
#define T5_CAMERA_SYS_REG_BASE       0x44010000u
#define T5_CAMERA_GPIO_REG_BASE      0x44000400u
#define T5_CAMERA_GPIO_CFG_BASE      (T5_CAMERA_SYS_REG_BASE + 0xc0u)
#define T5_CAMERA_GPIO_PER_MUX_REG   8u
#define T5_CAMERA_GPIO_MUX_WIDTH     4u
#define T5_CAMERA_DVP_INPUT_MUX_MODE 0u
#define T5_CAMERA_MCLK_MUX_MODE      1u
#define T5_CAMERA_GPIO_2_FUNC_EN     (1u << 6)

#define T5_CAMERA_REG32(address) \
  (*(FAR volatile uint32_t *)(uintptr_t)(address))

static mutex_t g_t5_camera_i2c_lock = NXMUTEX_INITIALIZER;

extern bk_err_t __real_bk_i2c_init_v2(i2c_id_t id,
                                       const i2c_config_t *config);
extern bk_err_t __real_bk_i2c_deinit_v2(i2c_id_t id);
extern bk_err_t __real_bk_i2c_memory_read_v2(
  i2c_id_t id, const i2c_mem_param_t *param);
extern bk_err_t __real_bk_i2c_memory_write_v2(
  i2c_id_t id, const i2c_mem_param_t *param);
extern void __real_dvp_camera_mclk_enable(mclk_freq_t mclk);
extern uint32_t sys_amp_res_acquire(void);
extern uint32_t sys_amp_res_release(void);

static bool t5_camera_i2c_is_board_bus(i2c_id_t id)
{
  return (unsigned int)id == T5_CAMERA_I2C_ID;
}

static gpio_id_t t5_camera_i2c_scl(void)
{
  return (gpio_id_t)BK7258_BOARD_DVP_I2C_SCL_GPIO;
}

static gpio_id_t t5_camera_i2c_sda(void)
{
  return (gpio_id_t)BK7258_BOARD_DVP_I2C_SDA_GPIO;
}

static void t5_camera_i2c_delay(void)
{
  up_udelay(T5_CAMERA_I2C_HALF_PERIOD_US);
}

static void t5_camera_i2c_drive_low(gpio_id_t pin)
{
  bk_gpio_disable_input(pin);
  bk_gpio_set_output_low(pin);
  bk_gpio_enable_output(pin);
}

static void t5_camera_i2c_drive_high(gpio_id_t pin)
{
  bk_gpio_disable_input(pin);
  bk_gpio_set_output_high(pin);
  bk_gpio_enable_output(pin);
}

static void t5_camera_i2c_release(gpio_id_t pin)
{
  bk_gpio_disable_output(pin);
  bk_gpio_pull_up(pin);
  bk_gpio_enable_input(pin);
}

static bool t5_camera_i2c_release_scl(void)
{
  /* Match Tuya's BK7258 software-I2C electrical mode.  SCL and transmitted
   * high bits are push-pull; SDA becomes an input only for ACK/read cycles.
   * The camera SCCB devices used by this board do not clock-stretch. */

  t5_camera_i2c_drive_high(t5_camera_i2c_scl());
  return true;
}

static bool t5_camera_i2c_start(void)
{
  t5_camera_i2c_drive_high(t5_camera_i2c_sda());
  if (!t5_camera_i2c_release_scl())
    {
      return false;
    }

  t5_camera_i2c_delay();
  t5_camera_i2c_drive_low(t5_camera_i2c_sda());
  t5_camera_i2c_delay();
  t5_camera_i2c_drive_low(t5_camera_i2c_scl());
  return true;
}

static void t5_camera_i2c_stop(void)
{
  t5_camera_i2c_drive_low(t5_camera_i2c_sda());
  t5_camera_i2c_delay();
  (void)t5_camera_i2c_release_scl();
  t5_camera_i2c_delay();
  t5_camera_i2c_drive_high(t5_camera_i2c_sda());
  t5_camera_i2c_delay();
}

static bool t5_camera_i2c_clock_high(void)
{
  if (!t5_camera_i2c_release_scl())
    {
      return false;
    }

  t5_camera_i2c_delay();
  return true;
}

static bool t5_camera_i2c_write_byte(uint8_t byte)
{
  uint8_t mask;
  bool acknowledged;

  for (mask = 0x80; mask != 0; mask >>= 1)
    {
      if ((byte & mask) != 0)
        {
          t5_camera_i2c_drive_high(t5_camera_i2c_sda());
        }
      else
        {
          t5_camera_i2c_drive_low(t5_camera_i2c_sda());
        }

      t5_camera_i2c_delay();
      if (!t5_camera_i2c_clock_high())
        {
          return false;
        }

      t5_camera_i2c_drive_low(t5_camera_i2c_scl());
    }

  t5_camera_i2c_release(t5_camera_i2c_sda());
  t5_camera_i2c_delay();
  if (!t5_camera_i2c_clock_high())
    {
      return false;
    }

  acknowledged = !bk_gpio_get_input(t5_camera_i2c_sda());
  t5_camera_i2c_drive_low(t5_camera_i2c_scl());
  t5_camera_i2c_drive_high(t5_camera_i2c_sda());
  t5_camera_i2c_delay();
  return acknowledged;
}

static bool t5_camera_i2c_read_byte(uint8_t *byte, bool last)
{
  uint8_t value = 0;
  uint8_t bit;

  if (byte == NULL)
    {
      return false;
    }

  t5_camera_i2c_release(t5_camera_i2c_sda());
  for (bit = 0; bit < 8; bit++)
    {
      value <<= 1;
      t5_camera_i2c_delay();
      if (!t5_camera_i2c_clock_high())
        {
          return false;
        }

      if (bk_gpio_get_input(t5_camera_i2c_sda()))
        {
          value |= 1;
        }

      t5_camera_i2c_drive_low(t5_camera_i2c_scl());
    }

  if (last)
    {
      t5_camera_i2c_drive_high(t5_camera_i2c_sda());
    }
  else
    {
      t5_camera_i2c_drive_low(t5_camera_i2c_sda());
    }

  t5_camera_i2c_delay();
  if (!t5_camera_i2c_clock_high())
    {
      return false;
    }

  t5_camera_i2c_drive_low(t5_camera_i2c_scl());
  t5_camera_i2c_drive_high(t5_camera_i2c_sda());
  *byte = value;
  return true;
}

static bool t5_camera_i2c_write_address(uint32_t address,
                                         i2c_mem_addr_size_t size)
{
  if (size == I2C_MEM_ADDR_SIZE_16BIT &&
      !t5_camera_i2c_write_byte((uint8_t)(address >> 8)))
    {
      return false;
    }

  return t5_camera_i2c_write_byte((uint8_t)address);
}

static bool t5_camera_i2c_valid_param(const i2c_mem_param_t *param)
{
  return param != NULL && param->dev_addr <= 0x7f &&
         param->data != NULL && param->data_size != 0 &&
         (param->mem_addr_size == I2C_MEM_ADDR_SIZE_8BIT ||
          param->mem_addr_size == I2C_MEM_ADDR_SIZE_16BIT);
}

static bk_err_t t5_camera_i2c_memory_read(const i2c_mem_param_t *param);

static void t5_camera_sensor_reset(void)
{
  gpio_id_t reset = (gpio_id_t)BK7258_BOARD_DVP_RESET_GPIO;

  /* The immutable SDK toggles RESET low/high without an asserted interval.
   * GC2145's public data sheet omits this board-level delay, while the
   * source-available Tuya T5 binding for the same sensor holds active-low
   * RESET for 100 ms and waits another 100 ms before enabling DVP/MCLK. */

  bk_gpio_disable_input(reset);
  bk_gpio_set_output_high(reset);
  bk_gpio_enable_output(reset);
  (void)nxsig_usleep(T5_CAMERA_RESET_SETTLE_US);
  bk_gpio_set_output_low(reset);
  (void)nxsig_usleep(T5_CAMERA_RESET_ASSERT_US);
  bk_gpio_set_output_high(reset);
  (void)nxsig_usleep(T5_CAMERA_RESET_RELEASE_US);
}

static void t5_camera_dvp_pinmux_set(uint8_t pin, uint32_t mode)
{
  uintptr_t address = T5_CAMERA_GPIO_CFG_BASE +
                      pin / T5_CAMERA_GPIO_PER_MUX_REG * sizeof(uint32_t);
  uint32_t shift = pin % T5_CAMERA_GPIO_PER_MUX_REG *
                   T5_CAMERA_GPIO_MUX_WIDTH;
  uint32_t mask = 0x0fu << shift;
  uint32_t value = T5_CAMERA_REG32(address);

  T5_CAMERA_REG32(address) =
    (value & ~mask) | (mode << shift);

  /* gpio_dev_map() returns BK_OK even when gpio_hal_func_map() rejects an
   * already-owned pin.  In that path the sysctrl selector may look right
   * while the pad remains in GPIO mode.  The DVP group is board-exclusive,
   * so also enforce the documented peripheral-enable bit on each pad. */

  address = T5_CAMERA_GPIO_REG_BASE + pin * sizeof(uint32_t);
  T5_CAMERA_REG32(address) |= T5_CAMERA_GPIO_2_FUNC_EN;
}

static int t5_camera_dvp_pinmux_apply(void)
{
  static const uint8_t g_dvp_pins[] =
  {
    BK7258_BOARD_DVP_MCLK_GPIO,
    BK7258_BOARD_DVP_PCLK_GPIO,
    BK7258_BOARD_DVP_HSYNC_GPIO,
    BK7258_BOARD_DVP_VSYNC_GPIO,
    BK7258_BOARD_DVP_D0_GPIO,
    BK7258_BOARD_DVP_D1_GPIO,
    BK7258_BOARD_DVP_D2_GPIO,
    BK7258_BOARD_DVP_D3_GPIO,
    BK7258_BOARD_DVP_D4_GPIO,
    BK7258_BOARD_DVP_D5_GPIO,
    BK7258_BOARD_DVP_D6_GPIO,
    BK7258_BOARD_DVP_D7_GPIO,
  };
  irqstate_t flags;
  unsigned int index;

  /* These pins belong to the AP camera datapath.  v3.1.1.9's public
   * gpio_dev_map() discards the lower-level mapping failure, so apply the
   * BK7258 DVP slots here after the real SDK helper.  Use the same AMP
   * sys-register lock as the SDK sys_ctrl implementation, plus a local
   * critical section to make each register RMW atomic on AP SMP. */

  if (sys_amp_res_acquire() != 0)
    {
      return -EBUSY;
    }

  flags = up_irq_save();
  for (index = 0; index < sizeof(g_dvp_pins); index++)
    {
      /* GPIO27 uses the second peripheral slot for CLK_AUXS_CIS.  The
       * remaining BK7258 DVP signals are all in the first peripheral slot:
       * P29/P30/P31 are PCLK/HSYNC/VSYNC and P32..P39 are D0..D7. */

      t5_camera_dvp_pinmux_set(g_dvp_pins[index],
        g_dvp_pins[index] == BK7258_BOARD_DVP_MCLK_GPIO ?
        T5_CAMERA_MCLK_MUX_MODE : T5_CAMERA_DVP_INPUT_MUX_MODE);
    }

  __asm volatile ("dmb sy" ::: "memory");
  up_irq_restore(flags);

  return sys_amp_res_release() == 0 ? OK : -EIO;
}

void __wrap_dvp_camera_mclk_enable(mclk_freq_t mclk)
{
  /* Reproduce the T5AI board sequence before the immutable SDK performs
   * sensor auto-detection: idle SCCB high, reset high/low/high, then enable
   * the sensor clock.  This wrapper is board-scoped and leaves the SDK's
   * clock source/divider implementation unchanged. */

  t5_camera_i2c_drive_high(t5_camera_i2c_scl());
  t5_camera_i2c_drive_high(t5_camera_i2c_sda());
  t5_camera_sensor_reset();
  __real_dvp_camera_mclk_enable(mclk);
  if (t5_camera_dvp_pinmux_apply() < 0)
    {
      syslog(LOG_ERR, "BKCAM DVP AP pinmux apply failed\n");
    }

  (void)nxsig_usleep(T5_CAMERA_MCLK_SETTLE_US);
}

static bk_err_t t5_camera_i2c_memory_write(const i2c_mem_param_t *param)
{
  uint32_t index;
  bool success = false;
  int ret;

  if (!t5_camera_i2c_valid_param(param))
    {
      return BK_FAIL;
    }

  ret = nxmutex_lock(&g_t5_camera_i2c_lock);
  if (ret < 0)
    {
      return BK_FAIL;
    }

  if (!t5_camera_i2c_start() ||
      !t5_camera_i2c_write_byte((uint8_t)(param->dev_addr << 1)) ||
      !t5_camera_i2c_write_address(param->mem_addr,
                                   param->mem_addr_size))
    {
      goto out;
    }

  for (index = 0; index < param->data_size; index++)
    {
      if (!t5_camera_i2c_write_byte(param->data[index]))
        {
          goto out;
        }
    }

  success = true;

out:
  t5_camera_i2c_stop();
  nxmutex_unlock(&g_t5_camera_i2c_lock);
  return success ? BK_OK : BK_ERR_I2C_ACK_TIMEOUT;
}

static bk_err_t t5_camera_i2c_memory_read(const i2c_mem_param_t *param)
{
  uint32_t index;
  bool success = false;
  int ret;

  if (!t5_camera_i2c_valid_param(param))
    {
      return BK_FAIL;
    }

  ret = nxmutex_lock(&g_t5_camera_i2c_lock);
  if (ret < 0)
    {
      return BK_FAIL;
    }

  if (!t5_camera_i2c_start() ||
      !t5_camera_i2c_write_byte((uint8_t)(param->dev_addr << 1)) ||
      !t5_camera_i2c_write_address(param->mem_addr,
                                   param->mem_addr_size) ||
      !t5_camera_i2c_start() ||
      !t5_camera_i2c_write_byte((uint8_t)((param->dev_addr << 1) | 1u)))
    {
      goto out;
    }

  for (index = 0; index < param->data_size; index++)
    {
      if (!t5_camera_i2c_read_byte(&param->data[index],
                                   index + 1u == param->data_size))
        {
          goto out;
        }
    }

  success = true;

out:
  t5_camera_i2c_stop();
  nxmutex_unlock(&g_t5_camera_i2c_lock);
  return success ? BK_OK : BK_ERR_I2C_ACK_TIMEOUT;
}

bk_err_t __wrap_bk_i2c_init_v2(i2c_id_t id,
                                const i2c_config_t *config)
{
  if (!t5_camera_i2c_is_board_bus(id))
    {
      return __real_bk_i2c_init_v2(id, config);
    }

  (void)config;
  t5_camera_i2c_drive_high(t5_camera_i2c_scl());
  t5_camera_i2c_drive_high(t5_camera_i2c_sda());
  t5_camera_i2c_delay();
  return bk_gpio_get_input(t5_camera_i2c_scl()) &&
         bk_gpio_get_input(t5_camera_i2c_sda()) ? BK_OK :
         BK_ERR_I2C_SM_BUS_BUSY;
}

bk_err_t __wrap_bk_i2c_deinit_v2(i2c_id_t id)
{
  if (!t5_camera_i2c_is_board_bus(id))
    {
      return __real_bk_i2c_deinit_v2(id);
    }

  t5_camera_i2c_drive_high(t5_camera_i2c_scl());
  t5_camera_i2c_drive_high(t5_camera_i2c_sda());
  return BK_OK;
}

bk_err_t __wrap_bk_i2c_memory_read_v2(
  i2c_id_t id, const i2c_mem_param_t *param)
{
  return t5_camera_i2c_is_board_bus(id) ?
         t5_camera_i2c_memory_read(param) :
         __real_bk_i2c_memory_read_v2(id, param);
}

bk_err_t __wrap_bk_i2c_memory_write_v2(
  i2c_id_t id, const i2c_mem_param_t *param)
{
  return t5_camera_i2c_is_board_bus(id) ?
         t5_camera_i2c_memory_write(param) :
         __real_bk_i2c_memory_write_v2(id, param);
}

#endif /* CONFIG_BK7258_T5_BOARD_CAMERA */
