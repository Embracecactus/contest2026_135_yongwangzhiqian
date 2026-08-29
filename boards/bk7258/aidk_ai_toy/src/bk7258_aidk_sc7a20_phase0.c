/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_sc7a20_phase0.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK AI Toy SC7A20H Phase 0 identity probe.  This is intentionally not a
 * sensor driver: it reads only WHO_AM_I through the already-registered NuttX
 * I2C character device and leaves every writable sensor register untouched.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_SC7A20_PHASE0

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>

#include <arch/board/board.h>

#define AIDK_SC7A20_STRINGIFY_(value) #value
#define AIDK_SC7A20_STRINGIFY(value)  AIDK_SC7A20_STRINGIFY_(value)
#define AIDK_SC7A20_DEVPATH           \
  "/dev/i2c" AIDK_SC7A20_STRINGIFY(BK7258_BOARD_SC7A20_I2C_BUS)

/* Keep the probe pinned to the reviewed schematic and maintained AP profile.
 * A board-header or defconfig edit must not silently move this transaction.
 */

#if BK7258_BOARD_SC7A20_I2C_BUS != 0 || \
    BK7258_BOARD_SC7A20_I2C_SCL_GPIO != 20 || \
    BK7258_BOARD_SC7A20_I2C_SDA_GPIO != 21
#  error "AIDK SC7A20H must use I2C0 on P20/P21"
#endif

#if CONFIG_BK7258_I2C_BUS != BK7258_BOARD_SC7A20_I2C_BUS
#  error "AIDK AP profile must publish the SC7A20H hardware I2C bus"
#endif

#if BK7258_BOARD_SC7A20_I2C_ADDRESS != 0x18 || \
    BK7258_BOARD_SC7A20_POWER_ALWAYS_ON != 1
#  error "AIDK SC7A20H must be always-powered with SDO strapped low"
#endif

int bk7258_aidk_sc7a20_phase0_probe(void)
{
  struct i2c_msg_s messages[2];
  struct i2c_transfer_s transfer;
  struct file filep;
  uint8_t reg = BK7258_BOARD_SC7A20_WHO_AM_I_REG;
  uint8_t identity = 0;
  int close_ret;
  int ret;

  ret = file_open(&filep, AIDK_SC7A20_DEVPATH, O_RDWR);
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK SC7A20H phase0 FAIL: open %s: %d\n",
             AIDK_SC7A20_DEVPATH, ret);
      return ret;
    }

  messages[0].frequency = BK7258_BOARD_SC7A20_I2C_FREQUENCY;
  messages[0].addr = BK7258_BOARD_SC7A20_I2C_ADDRESS;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = &reg;
  messages[0].length = 1;

  messages[1].frequency = BK7258_BOARD_SC7A20_I2C_FREQUENCY;
  messages[1].addr = BK7258_BOARD_SC7A20_I2C_ADDRESS;
  messages[1].flags = I2C_M_READ;
  messages[1].buffer = &identity;
  messages[1].length = 1;

  transfer.msgv = messages;
  transfer.msgc = 2;

  ret = file_ioctl(&filep, I2CIOC_TRANSFER,
                   (unsigned long)(uintptr_t)&transfer);
  close_ret = file_close(&filep);
  if (ret >= 0 && close_ret < 0)
    {
      ret = close_ret;
    }

  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK SC7A20H phase0 FAIL: transfer: %d\n", ret);
      return ret;
    }

  if (identity != BK7258_BOARD_SC7A20_WHO_AM_I_VALUE)
    {
      syslog(LOG_ERR,
             "AIDK SC7A20H phase0 FAIL: id=0x%02x expected=0x%02x\n",
             identity, BK7258_BOARD_SC7A20_WHO_AM_I_VALUE);
      return -ENODEV;
    }

  syslog(LOG_INFO,
         "AIDK SC7A20H phase0 PASS: addr=0x%02x id=0x%02x\n",
         BK7258_BOARD_SC7A20_I2C_ADDRESS, identity);
  return OK;
}

#endif /* CONFIG_BK7258_AIDK_SC7A20_PHASE0 */
