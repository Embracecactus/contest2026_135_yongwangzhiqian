/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_camera_glue.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK GC2145 physical route for the reusable BK7258 DVP lower half.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_CAMERA

#include <errno.h>
#include <stdint.h>

#include <nuttx/signal.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_dvp.h>
#include <arch/chip/bk7258_pinmux.h>

#include <driver/gpio.h>

#include "bk7258_aidk_camera_glue.h"

#define AIDK_CAMERA_RESET_SETTLE_US     20000u
#define AIDK_CAMERA_RESET_ASSERT_US    100000u
#define AIDK_CAMERA_RESET_RELEASE_US   100000u
#define AIDK_CAMERA_MCLK_SETTLE_US      40000u
#define AIDK_CAMERA_DVP_INPUT_MUX_MODE  0u
#define AIDK_CAMERA_MCLK_MUX_MODE       1u
#define AIDK_CAMERA_I2C1_MUX_MODE       0u

extern bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev);
extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);

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

static int aidk_camera_i2c_route_apply(void)
{
  static const struct bk7258_pinmux_config_s configs[] =
  {
    { BK7258_BOARD_DVP_I2C_SCL_GPIO, AIDK_CAMERA_I2C1_MUX_MODE, true },
    { BK7258_BOARD_DVP_I2C_SDA_GPIO, AIDK_CAMERA_I2C1_MUX_MODE, true },
  };
  gpio_id_t scl = (gpio_id_t)BK7258_BOARD_DVP_I2C_SCL_GPIO;
  gpio_id_t sda = (gpio_id_t)BK7258_BOARD_DVP_I2C_SDA_GPIO;

  if (gpio_dev_unmap(scl) != BK_OK ||
      gpio_dev_unmap(sda) != BK_OK ||
      gpio_dev_map(scl, GPIO_DEV_I2C1_SCL) != BK_OK ||
      gpio_dev_map(sda, GPIO_DEV_I2C1_SDA) != BK_OK)
    {
      return -EIO;
    }

  return bk7258_pinmux_apply(configs, sizeof(configs) / sizeof(configs[0]));
}

static int aidk_camera_sensor_reset(void)
{
  gpio_id_t reset = (gpio_id_t)BK7258_BOARD_DVP_RESET_GPIO;

  if (bk_gpio_disable_input(reset) != BK_OK ||
      bk_gpio_set_output_high(reset) != BK_OK ||
      bk_gpio_enable_output(reset) != BK_OK)
    {
      return -EIO;
    }

  (void)nxsig_usleep(AIDK_CAMERA_RESET_SETTLE_US);
  if (bk_gpio_set_output_low(reset) != BK_OK)
    {
      return -EIO;
    }

  (void)nxsig_usleep(AIDK_CAMERA_RESET_ASSERT_US);
  if (bk_gpio_set_output_high(reset) != BK_OK)
    {
      return -EIO;
    }

  (void)nxsig_usleep(AIDK_CAMERA_RESET_RELEASE_US);
  return 0;
}

static int aidk_camera_prepare(FAR void *arg)
{
  static const struct bk7258_pinmux_config_s configs[] =
  {
    { BK7258_BOARD_DVP_MCLK_GPIO, AIDK_CAMERA_MCLK_MUX_MODE, true },
    { BK7258_BOARD_DVP_PCLK_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_HSYNC_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_VSYNC_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D0_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D1_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D2_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D3_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D4_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D5_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D6_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D7_GPIO, AIDK_CAMERA_DVP_INPUT_MUX_MODE, true },
  };
  int ret;

  (void)arg;
  ret = aidk_camera_i2c_route_apply();
  if (ret < 0)
    {
      return ret;
    }

  ret = aidk_camera_sensor_reset();
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_pinmux_apply(configs, sizeof(configs) / sizeof(configs[0]));
}

static void aidk_camera_mclk_started(FAR void *arg)
{
  (void)arg;
  (void)nxsig_usleep(AIDK_CAMERA_MCLK_SETTLE_US);
}

static const struct bk7258_dvp_binding_s g_aidk_camera_binding =
{
  .version = BK7258_DVP_BINDING_VERSION,
  .size = sizeof(struct bk7258_dvp_binding_s),
  .arg = NULL,
  .i2c_write = NULL,
  .prepare = aidk_camera_prepare,
  .mclk_started = aidk_camera_mclk_started,
  .i2c_bus = BK7258_BOARD_DVP_I2C_BUS,
  .i2c = NULL,
};

FAR const struct bk7258_dvp_binding_s *bk7258_aidk_camera_binding(void)
{
  return &g_aidk_camera_binding;
}

#endif /* CONFIG_BK7258_AIDK_CAMERA */
