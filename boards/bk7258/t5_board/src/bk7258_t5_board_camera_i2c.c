/****************************************************************************
 * boards/bk7258/t5_board/src/bk7258_t5_board_camera_i2c.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board V1.0.2 DVP sensor-control I2C adapter.
 *
 * BK7258 has two hardware I2C controllers, while the P10 camera connector
 * uses GPIO13/GPIO15 and therefore reaches the SDK through simulated I2C2.
 * The immutable v3.1.1.9 simulated driver uses a CPU-cycle delay calibrated
 * for a different clock.  This board-owned transport is registered through
 * the chip DVP binding and provides clock-independent 5 us half cycles for
 * I2C2 without exporting or replacing SDK symbols from the board layer.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_T5_BOARD_CAMERA

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_dvp.h>
#include <arch/chip/bk7258_pinmux.h>

#include "bk7258_t5_board_camera_i2c.h"

#define T5_CAMERA_I2C_HALF_PERIOD_US 5u
#define T5_CAMERA_RESET_SETTLE_US    20000u
#define T5_CAMERA_RESET_ASSERT_US    100000u
#define T5_CAMERA_RESET_RELEASE_US   100000u
#define T5_CAMERA_MCLK_SETTLE_US     40000u
#define T5_CAMERA_DVP_INPUT_MUX_MODE 0u
#define T5_CAMERA_MCLK_MUX_MODE      1u

static mutex_t g_t5_camera_i2c_lock = NXMUTEX_INITIALIZER;

static uint8_t t5_camera_i2c_scl(void)
{
  return BK7258_BOARD_DVP_I2C_SCL_GPIO;
}

static uint8_t t5_camera_i2c_sda(void)
{
  return BK7258_BOARD_DVP_I2C_SDA_GPIO;
}

static void t5_camera_i2c_delay(void)
{
  uint32_t cycles;
  uint32_t start;
  unsigned long frequency = up_perf_getfreq();

  /* The generic up_udelay() walks the NuttX timer lower-half on every GPIO
   * edge, which makes a full GC2145 register table take about a minute.
   * Use NuttX's per-core DWT cycle counter for this short board-level SCCB
   * delay.  up_perf_getfreq() is refreshed by the BK7258 DVFS path, so the
   * timing follows the live AP frequency instead of assuming the original
   * bring-up clock.
   */

  cycles = (uint32_t)(((uint64_t)frequency *
                       T5_CAMERA_I2C_HALF_PERIOD_US + 999999u) /
                      1000000u);
  start = (uint32_t)up_perf_gettime();
  while ((uint32_t)((uint32_t)up_perf_gettime() - start) < cycles)
    {
    }
}

static void t5_camera_i2c_drive_low(uint8_t pin)
{
  /* The chip helper owns the timing-sensitive AON GPIO mechanism.  P13/P15
   * are board-exclusive while the camera is open.
   */

  (void)bk7258_gpio_fast_write(pin, false);
}

static void t5_camera_i2c_drive_high(uint8_t pin)
{
  (void)bk7258_gpio_fast_write(pin, true);
}

static void t5_camera_i2c_release(uint8_t pin)
{
  /* Output disabled, input and internal pull-up enabled.  Keep the output
   * latch high so returning to output mode cannot create a low glitch.
   */

  (void)bk7258_gpio_fast_release_pullup(pin);
}

static bool t5_camera_i2c_get_input(uint8_t pin)
{
  bool high = false;

  return bk7258_gpio_read_input(pin, &high) == 0 && high;
}

static int t5_camera_i2c_configure_lines(void)
{
  int ret;

  ret = bk7258_gpio_configure_output(t5_camera_i2c_scl(), true,
                                     BK7258_GPIO_DRIVE_0);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_gpio_configure_output(t5_camera_i2c_sda(), true,
                                      BK7258_GPIO_DRIVE_0);
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

  acknowledged = !t5_camera_i2c_get_input(t5_camera_i2c_sda());
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

      if (t5_camera_i2c_get_input(t5_camera_i2c_sda()))
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

static bool t5_camera_i2c_write_address(uint32_t address, uint8_t size)
{
  if (size == 2u &&
      !t5_camera_i2c_write_byte((uint8_t)(address >> 8)))
    {
      return false;
    }

  return t5_camera_i2c_write_byte((uint8_t)address);
}

static bool t5_camera_i2c_valid_param(
  FAR const struct bk7258_dvp_i2c_transfer_s *transfer)
{
  return transfer != NULL && transfer->address <= 0x7f &&
         transfer->buffer != NULL && transfer->length != 0 &&
         (transfer->memory_address_bytes == 1u ||
          transfer->memory_address_bytes == 2u);
}

static int t5_camera_i2c_memory_read(
  FAR const struct bk7258_dvp_i2c_transfer_s *transfer);

static int t5_camera_sensor_reset(void)
{
  uint8_t reset = BK7258_BOARD_DVP_RESET_GPIO;
  int ret;

  /* The immutable SDK toggles RESET low/high without an asserted interval.
   * GC2145's public data sheet omits this board-level delay, while the
   * source-available Tuya T5 binding for the same sensor holds active-low
   * RESET for 100 ms and waits another 100 ms before enabling DVP/MCLK. */

  ret = bk7258_gpio_configure_output(reset, true, BK7258_GPIO_DRIVE_0);
  if (ret < 0)
    {
      return ret;
    }

  (void)nxsig_usleep(T5_CAMERA_RESET_SETTLE_US);
  ret = bk7258_gpio_write(reset, false);
  if (ret < 0)
    {
      return ret;
    }

  (void)nxsig_usleep(T5_CAMERA_RESET_ASSERT_US);
  ret = bk7258_gpio_write(reset, true);
  if (ret < 0)
    {
      return ret;
    }

  (void)nxsig_usleep(T5_CAMERA_RESET_RELEASE_US);
  return OK;
}

static int t5_camera_dvp_pinmux_apply(void)
{
  static const struct bk7258_pinmux_config_s configs[] =
  {
    { BK7258_BOARD_DVP_MCLK_GPIO, T5_CAMERA_MCLK_MUX_MODE, true },
    { BK7258_BOARD_DVP_PCLK_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_HSYNC_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_VSYNC_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D0_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D1_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D2_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D3_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D4_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D5_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D6_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
    { BK7258_BOARD_DVP_D7_GPIO, T5_CAMERA_DVP_INPUT_MUX_MODE, true },
  };

  return bk7258_pinmux_apply(configs, sizeof(configs) / sizeof(configs[0]));
}

int bk7258_t5_camera_prepare(FAR void *arg)
{
  int ret;

  (void)arg;
  ret = t5_camera_i2c_configure_lines();
  if (ret < 0)
    {
      return ret;
    }

  ret = t5_camera_sensor_reset();
  if (ret < 0)
    {
      return ret;
    }

  ret = t5_camera_dvp_pinmux_apply();
  return ret;
}

void bk7258_t5_camera_mclk_started(FAR void *arg)
{
  (void)arg;
  (void)nxsig_usleep(T5_CAMERA_MCLK_SETTLE_US);
}

static int t5_camera_i2c_memory_write(
  FAR const struct bk7258_dvp_i2c_transfer_s *transfer)
{
  uint32_t index;
  bool success = false;
  int ret;

  if (!t5_camera_i2c_valid_param(transfer))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_t5_camera_i2c_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!t5_camera_i2c_start() ||
      !t5_camera_i2c_write_byte((uint8_t)(transfer->address << 1)) ||
      !t5_camera_i2c_write_address(transfer->memory_address,
                                   transfer->memory_address_bytes))
    {
      goto out;
    }

  for (index = 0; index < transfer->length; index++)
    {
      if (!t5_camera_i2c_write_byte(transfer->buffer[index]))
        {
          goto out;
        }
    }

  success = true;

out:
  t5_camera_i2c_stop();
  nxmutex_unlock(&g_t5_camera_i2c_lock);
  return success ? 0 : -ETIMEDOUT;
}

static int t5_camera_i2c_memory_read(
  FAR const struct bk7258_dvp_i2c_transfer_s *transfer)
{
  uint32_t index;
  bool success = false;
  int ret;

  if (!t5_camera_i2c_valid_param(transfer))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_t5_camera_i2c_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!t5_camera_i2c_start() ||
      !t5_camera_i2c_write_byte((uint8_t)(transfer->address << 1)) ||
      !t5_camera_i2c_write_address(transfer->memory_address,
                                   transfer->memory_address_bytes) ||
      !t5_camera_i2c_start() ||
      !t5_camera_i2c_write_byte((uint8_t)((transfer->address << 1) | 1u)))
    {
      goto out;
    }

  for (index = 0; index < transfer->length; index++)
    {
      if (!t5_camera_i2c_read_byte(&transfer->buffer[index],
                                   index + 1u == transfer->length))
        {
          goto out;
        }
    }

  success = true;

out:
  t5_camera_i2c_stop();
  nxmutex_unlock(&g_t5_camera_i2c_lock);
  return success ? 0 : -ETIMEDOUT;
}

static int t5_camera_i2c_initialize(FAR void *arg)
{
  int ret;

  (void)arg;
  ret = t5_camera_i2c_configure_lines();
  if (ret < 0)
    {
      return ret;
    }

  t5_camera_i2c_delay();
  return t5_camera_i2c_get_input(t5_camera_i2c_scl()) &&
         t5_camera_i2c_get_input(t5_camera_i2c_sda()) ? 0 : -EBUSY;
}

static int t5_camera_i2c_uninitialize(FAR void *arg)
{
  (void)arg;
  t5_camera_i2c_drive_high(t5_camera_i2c_scl());
  t5_camera_i2c_drive_high(t5_camera_i2c_sda());
  return 0;
}

static int t5_camera_i2c_read(
  FAR void *arg,
  FAR const struct bk7258_dvp_i2c_transfer_s *transfer)
{
  (void)arg;
  return t5_camera_i2c_memory_read(transfer);
}

static int t5_camera_i2c_write(
  FAR void *arg,
  FAR const struct bk7258_dvp_i2c_transfer_s *transfer)
{
  (void)arg;
  return t5_camera_i2c_memory_write(transfer);
}

const struct bk7258_dvp_i2c_ops_s g_bk7258_t5_camera_i2c_ops =
{
  .initialize = t5_camera_i2c_initialize,
  .uninitialize = t5_camera_i2c_uninitialize,
  .read = t5_camera_i2c_read,
  .write = t5_camera_i2c_write,
};

#endif /* CONFIG_BK7258_T5_BOARD_CAMERA */
