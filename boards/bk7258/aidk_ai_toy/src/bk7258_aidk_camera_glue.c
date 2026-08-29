/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_camera_glue.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK GC2145 board glue for immutable v3.1.1.9 DVP sensor discovery.
 * It preserves the SDK implementation while enforcing the reviewed AIDK
 * hardware-I2C, reset and DVP pin routes at the board boundary.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_CAMERA

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/signal.h>

#include <arch/board/board.h>

#include <components/dvp_camera_types.h>
#include <driver/gpio.h>
#include <driver/i2c.h>

#define AIDK_CAMERA_RESET_SETTLE_US     20000u
#define AIDK_CAMERA_RESET_ASSERT_US    100000u
#define AIDK_CAMERA_RESET_RELEASE_US   100000u
#define AIDK_CAMERA_MCLK_SETTLE_US      40000u
#define AIDK_CAMERA_SYS_REG_BASE        0x44010000u
#define AIDK_CAMERA_GPIO_REG_BASE       0x44000400u
#define AIDK_CAMERA_GPIO_CFG_BASE       (AIDK_CAMERA_SYS_REG_BASE + 0xc0u)
#define AIDK_CAMERA_GPIO_PER_MUX_REG    8u
#define AIDK_CAMERA_GPIO_MUX_WIDTH      4u
#define AIDK_CAMERA_DVP_INPUT_MUX_MODE  0u
#define AIDK_CAMERA_MCLK_MUX_MODE       1u
#define AIDK_CAMERA_I2C1_MUX_MODE       0u
#define AIDK_CAMERA_GPIO_2_FUNC_EN      (1u << 6)

#define AIDK_CAMERA_REG32(address) \
  (*(FAR volatile uint32_t *)(uintptr_t)(address))

extern bk_err_t __real_bk_i2c_init_v2(i2c_id_t id,
                                       const i2c_config_t *config);
extern bk_err_t __real_bk_i2c_deinit_v2(i2c_id_t id);
extern bk_err_t __real_bk_i2c_memory_read_v2(
  i2c_id_t id, const i2c_mem_param_t *param);
extern bk_err_t __real_bk_i2c_memory_write_v2(
  i2c_id_t id, const i2c_mem_param_t *param);
extern void __real_dvp_camera_mclk_enable(mclk_freq_t mclk);
extern bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev);
extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern uint32_t sys_amp_res_acquire(void);
extern uint32_t sys_amp_res_release(void);

static int g_aidk_camera_dvp_route_result;

#if BK7258_BOARD_DVP_I2C_BUS != 1 || \
    BK7258_BOARD_DVP_I2C_MAP_MODE != 1 || \
    BK7258_BOARD_DVP_I2C_SCL_GPIO != 42 || \
    BK7258_BOARD_DVP_I2C_SDA_GPIO != 43
#  error "AIDK production camera must use hardware I2C1 on P42/P43"
#endif

#if BK7258_BOARD_DVP_MCLK_GPIO != 27 || \
    BK7258_BOARD_DVP_PCLK_GPIO != 29 || \
    BK7258_BOARD_DVP_D0_GPIO != 32 || \
    BK7258_BOARD_DVP_D7_GPIO != 39
#  error "AIDK DVP route no longer matches the reviewed board schematic"
#endif

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

  return ((mux >> shift) & 0x0fu) == mode &&
         (pad & AIDK_CAMERA_GPIO_2_FUNC_EN) != 0;
}

static bk_err_t aidk_camera_i2c_route_apply(void)
{
  gpio_id_t scl = (gpio_id_t)BK7258_BOARD_DVP_I2C_SCL_GPIO;
  gpio_id_t sda = (gpio_id_t)BK7258_BOARD_DVP_I2C_SDA_GPIO;

  if (gpio_dev_unmap(scl) != BK_OK ||
      gpio_dev_unmap(sda) != BK_OK ||
      gpio_dev_map(scl, GPIO_DEV_I2C1_SCL) != BK_OK ||
      gpio_dev_map(sda, GPIO_DEV_I2C1_SDA) != BK_OK ||
      !aidk_camera_pinmux_matches(scl, AIDK_CAMERA_I2C1_MUX_MODE) ||
      !aidk_camera_pinmux_matches(sda, AIDK_CAMERA_I2C1_MUX_MODE))
    {
      return BK_FAIL;
    }

  return BK_OK;
}

static void aidk_camera_sensor_reset(void)
{
  gpio_id_t reset = (gpio_id_t)BK7258_BOARD_DVP_RESET_GPIO;

  /* Match the validated GC2145 sequence used by the existing BK7258 camera
   * binding: settle high, assert RESET low, then wait after releasing it.
   */

  bk_gpio_disable_input(reset);
  bk_gpio_set_output_high(reset);
  bk_gpio_enable_output(reset);
  (void)nxsig_usleep(AIDK_CAMERA_RESET_SETTLE_US);
  bk_gpio_set_output_low(reset);
  (void)nxsig_usleep(AIDK_CAMERA_RESET_ASSERT_US);
  bk_gpio_set_output_high(reset);
  (void)nxsig_usleep(AIDK_CAMERA_RESET_RELEASE_US);
}

static void aidk_camera_dvp_pinmux_set(uint8_t pin, uint32_t mode)
{
  uintptr_t address = AIDK_CAMERA_GPIO_CFG_BASE +
                      pin / AIDK_CAMERA_GPIO_PER_MUX_REG * sizeof(uint32_t);
  uint32_t shift = pin % AIDK_CAMERA_GPIO_PER_MUX_REG *
                   AIDK_CAMERA_GPIO_MUX_WIDTH;
  uint32_t mask = 0x0fu << shift;
  uint32_t value = AIDK_CAMERA_REG32(address);

  AIDK_CAMERA_REG32(address) = (value & ~mask) | (mode << shift);
  AIDK_CAMERA_REG32(AIDK_CAMERA_GPIO_REG_BASE +
                    pin * sizeof(uint32_t)) |=
    AIDK_CAMERA_GPIO_2_FUNC_EN;
}

static int aidk_camera_dvp_pinmux_apply(void)
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

  if (sys_amp_res_acquire() != 0)
    {
      return -EBUSY;
    }

  flags = up_irq_save();
  for (index = 0; index < sizeof(g_dvp_pins); index++)
    {
      aidk_camera_dvp_pinmux_set(
        g_dvp_pins[index],
        g_dvp_pins[index] == BK7258_BOARD_DVP_MCLK_GPIO ?
          AIDK_CAMERA_MCLK_MUX_MODE : AIDK_CAMERA_DVP_INPUT_MUX_MODE);
    }

  __asm volatile ("dmb sy" ::: "memory");
  up_irq_restore(flags);
  return sys_amp_res_release() == 0 ? OK : -EIO;
}

bk_err_t __wrap_bk_i2c_init_v2(i2c_id_t id,
                                const i2c_config_t *config)
{
  if ((unsigned int)id == BK7258_BOARD_DVP_I2C_BUS)
    {
      if (g_aidk_camera_dvp_route_result < 0 ||
          aidk_camera_i2c_route_apply() != BK_OK)
        {
          return BK_FAIL;
        }
    }

  return __real_bk_i2c_init_v2(id, config);
}

bk_err_t __wrap_bk_i2c_deinit_v2(i2c_id_t id)
{
  return __real_bk_i2c_deinit_v2(id);
}

bk_err_t __wrap_bk_i2c_memory_read_v2(
  i2c_id_t id, const i2c_mem_param_t *param)
{
  return __real_bk_i2c_memory_read_v2(id, param);
}

bk_err_t __wrap_bk_i2c_memory_write_v2(
  i2c_id_t id, const i2c_mem_param_t *param)
{
  return __real_bk_i2c_memory_write_v2(id, param);
}

void __wrap_dvp_camera_mclk_enable(mclk_freq_t mclk)
{
  /* Sensor power was enabled by the V4L2 sensor facade immediately before
   * imgdata open.  Preserve the SDK clock implementation, then enforce the
   * AIDK-exclusive DVP route before sensor discovery continues.
   */

  aidk_camera_sensor_reset();
  __real_dvp_camera_mclk_enable(mclk);
  g_aidk_camera_dvp_route_result = aidk_camera_dvp_pinmux_apply();
  if (g_aidk_camera_dvp_route_result < 0)
    {
      syslog(LOG_ERR, "AIDK GC2145 DVP pinmux apply failed\n");
    }

  (void)nxsig_usleep(AIDK_CAMERA_MCLK_SETTLE_US);
}

#endif /* CONFIG_BK7258_AIDK_CAMERA */
