/*
 * boot_ota_flash_program.c - SRAM-only BK7258 metadata program primitive.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This clean-room closure follows the exact Beken v3.1.1.9 Flash driver/LL
 * sequence.  Every reachable function is linked into SRAM; it has no SDK,
 * RTOS, libc, heap or XIP dependency while the controller is busy.
 */

#include <stdint.h>

#include "boot_ota_flash_program.h"
#include "../chip/include/bk7258_partition_layout.h"

#define BOOT_OTA_RAM_TEXT \
  __attribute__((section(".boot_ota_ramfunc.text"), noinline, used))
#define BOOT_OTA_NORETURN __attribute__((noreturn))

#define OTA_REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define FLASH_BASE              0x44030000u
#define FLASH_OP_CTRL           (FLASH_BASE + 0x10u)
#define FLASH_DATA_SW_FLASH     (FLASH_BASE + 0x14u)
#define FLASH_DATA_FLASH_SW     (FLASH_BASE + 0x18u)
#define FLASH_CMD_CFG           (FLASH_BASE + 0x1cu)
#define FLASH_ID_REG            (FLASH_BASE + 0x20u)
#define FLASH_STATE             (FLASH_BASE + 0x24u)
#define FLASH_CONFIG            (FLASH_BASE + 0x28u)
#define FLASH_OP_CMD            (FLASH_BASE + 0x54u)

#define FLASH_OP_SW             (1u << 29)
#define FLASH_WP_VALUE          (1u << 30)
#define FLASH_BUSY_SW           (1u << 31)
#define FLASH_ADDRESS_MASK      0x00ffffffu
#define FLASH_COMMAND_SHIFT     24u
#define FLASH_COMMAND_MASK      (0x1fu << FLASH_COMMAND_SHIFT)
#define FLASH_MODE_SHIFT        4u
#define FLASH_MODE_MASK         (0x1fu << FLASH_MODE_SHIFT)
#define FLASH_MODE_DUAL         1u
#define FLASH_STATUS_DATA_MASK  0xffu
#define FLASH_WRSR_SHIFT        10u
#define FLASH_WRSR_MASK         (0xffffu << FLASH_WRSR_SHIFT)

#define FLASH_COMMAND_RDSR      3u
#define FLASH_COMMAND_RDSR2     6u
#define FLASH_COMMAND_WRSR2     7u
#define FLASH_COMMAND_READ      5u
#define FLASH_COMMAND_PROGRAM   12u
#define FLASH_COMMAND_READ_ID   20u
#define FLASH_COMMAND_CLEAR     22u

#define FLASH_EXPECTED_ID       0x00c86517u
#define FLASH_PROTECT_MASK      ((0x1fu << 2) | (1u << 14))

#define SYS_CPU0_SLEEP_CONFIG   0x44010044u
#define SYS_FLASH_TWO_WIRE      (1u << 7)

#define WDT_APB_STATUS          0x44800004u
#define WDT_APB_CTRL            0x44800010u
#define WDT_AON_CTRL            0x44000600u
#define WDT_KEY1                0x5au
#define WDT_KEY2                0xa5u
#define WDT_PERIOD              8000u

#define FLASH_WAIT_BUDGET       0x08000000u
#define WDT_FEED_INTERVAL_MASK  0x0000ffffu

#define METADATA_PRIMARY_START  BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET
#define METADATA_PRIMARY_SIZE   BK7258_ROLE_OTA_METADATA_PRIMARY_SIZE
#define METADATA_MIRROR_START   BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET
#define METADATA_MIRROR_SIZE    BK7258_ROLE_OTA_METADATA_MIRROR_SIZE
#define PROGRAM_SIZE            32u

static inline void ota_dsb(void)
{
  __asm volatile ("dsb sy" ::: "memory");
}

static inline void ota_isb(void)
{
  __asm volatile ("isb sy" ::: "memory");
}

BOOT_OTA_RAM_TEXT
static void boot_ota_ram_wdt_feed(void)
{
  uint32_t ctrl1 = (WDT_KEY1 << 16) | WDT_PERIOD;
  uint32_t ctrl2 = (WDT_KEY2 << 16) | WDT_PERIOD;

  OTA_REG32(WDT_APB_STATUS) = OTA_REG32(WDT_APB_STATUS) & ~0x3u;
  OTA_REG32(WDT_APB_CTRL) = ctrl1;
  OTA_REG32(WDT_APB_CTRL) = ctrl2;
  OTA_REG32(WDT_AON_CTRL) = ctrl1;
  OTA_REG32(WDT_AON_CTRL) = ctrl2;
}

BOOT_OTA_RAM_TEXT BOOT_OTA_NORETURN
static void boot_ota_ram_fail_reset(void)
{
  ota_dsb();
  for (;;)
    {
      __asm volatile ("nop");
    }
}

BOOT_OTA_RAM_TEXT
static void boot_ota_ram_wait_idle(void)
{
  uint32_t remaining = FLASH_WAIT_BUDGET;

  while ((OTA_REG32(FLASH_OP_CTRL) & FLASH_BUSY_SW) != 0)
    {
      if ((remaining & WDT_FEED_INTERVAL_MASK) == 0)
        {
          boot_ota_ram_wdt_feed();
        }

      if (remaining == 0)
        {
          boot_ota_ram_fail_reset();
        }

      remaining--;
    }
}

BOOT_OTA_RAM_TEXT
static void boot_ota_ram_trigger(uint32_t command, uint32_t address)
{
  uint32_t value;

  boot_ota_ram_wait_idle();
  value = OTA_REG32(FLASH_OP_CMD);
  value &= ~(FLASH_ADDRESS_MASK | FLASH_COMMAND_MASK);
  value |= (address & FLASH_ADDRESS_MASK) |
           ((command << FLASH_COMMAND_SHIFT) & FLASH_COMMAND_MASK);
  OTA_REG32(FLASH_OP_CMD) = value;
  OTA_REG32(FLASH_OP_CTRL) = OTA_REG32(FLASH_OP_CTRL) | FLASH_OP_SW;
  boot_ota_ram_wait_idle();
}

BOOT_OTA_RAM_TEXT
static void boot_ota_ram_prepare_controller(void)
{
  uint32_t value;

  value = OTA_REG32(FLASH_CONFIG) & ~FLASH_MODE_MASK;
  OTA_REG32(FLASH_CONFIG) = value;
  boot_ota_ram_trigger(FLASH_COMMAND_CLEAR, 0);
  OTA_REG32(SYS_CPU0_SLEEP_CONFIG) =
    OTA_REG32(SYS_CPU0_SLEEP_CONFIG) & ~SYS_FLASH_TWO_WIRE;
  value = OTA_REG32(FLASH_CONFIG) & ~FLASH_MODE_MASK;
  OTA_REG32(FLASH_CONFIG) = value |
                            (FLASH_MODE_DUAL << FLASH_MODE_SHIFT);
  OTA_REG32(SYS_CPU0_SLEEP_CONFIG) =
    OTA_REG32(SYS_CPU0_SLEEP_CONFIG) | SYS_FLASH_TWO_WIRE;
  ota_dsb();
  ota_isb();
}

BOOT_OTA_RAM_TEXT
static uint32_t boot_ota_ram_read_status(void)
{
  uint32_t status;

  OTA_REG32(FLASH_CMD_CFG) = 0;
  boot_ota_ram_trigger(FLASH_COMMAND_RDSR, 0);
  status = OTA_REG32(FLASH_STATE) & FLASH_STATUS_DATA_MASK;
  boot_ota_ram_trigger(FLASH_COMMAND_RDSR2, 0);
  status |= (OTA_REG32(FLASH_STATE) & FLASH_STATUS_DATA_MASK) << 8;
  return status;
}

BOOT_OTA_RAM_TEXT
static void boot_ota_ram_write_status(uint32_t status)
{
  uint32_t value;

  boot_ota_ram_wait_idle();
  OTA_REG32(FLASH_CMD_CFG) = 0;
  value = OTA_REG32(FLASH_CONFIG) & ~FLASH_WRSR_MASK;
  OTA_REG32(FLASH_CONFIG) = value |
                            ((status & 0xffffu) << FLASH_WRSR_SHIFT);
  OTA_REG32(FLASH_OP_CTRL) = OTA_REG32(FLASH_OP_CTRL) | FLASH_WP_VALUE;
  boot_ota_ram_trigger(FLASH_COMMAND_WRSR2, 0);
  OTA_REG32(FLASH_OP_CTRL) = OTA_REG32(FLASH_OP_CTRL) & ~FLASH_WP_VALUE;
}

BOOT_OTA_RAM_TEXT
static void boot_ota_ram_read32(uint32_t address, uint32_t words[8])
{
  uint32_t index;

  boot_ota_ram_trigger(FLASH_COMMAND_READ, address);
  for (index = 0; index < 8u; index++)
    {
      words[index] = OTA_REG32(FLASH_DATA_FLASH_SW);
    }
}

BOOT_OTA_RAM_TEXT
int boot_ota_flash_program32(uint32_t address, const uint8_t data[32])
{
  uint32_t words[8];
  uint32_t verify[8];
  uint32_t status_before;
  uint32_t status;
  uint32_t index;
  int result = -1;
  int primary_range;
  int mirror_range;

  primary_range = address >= METADATA_PRIMARY_START &&
    address - METADATA_PRIMARY_START <=
      METADATA_PRIMARY_SIZE - PROGRAM_SIZE;
  mirror_range = address >= METADATA_MIRROR_START &&
    address - METADATA_MIRROR_START <=
      METADATA_MIRROR_SIZE - PROGRAM_SIZE;

  if (data == (const uint8_t *)0 ||
      (address & (PROGRAM_SIZE - 1u)) != 0 ||
      (!primary_range && !mirror_range))
    {
      return -1;
    }

  boot_ota_ram_prepare_controller();
  boot_ota_ram_trigger(FLASH_COMMAND_READ_ID, 0);
  if ((OTA_REG32(FLASH_ID_REG) & FLASH_ADDRESS_MASK) != FLASH_EXPECTED_ID)
    {
      return -1;
    }

  status_before = boot_ota_ram_read_status();
  status = status_before & ~FLASH_PROTECT_MASK;
  if (status != status_before)
    {
      boot_ota_ram_write_status(status);
      if ((boot_ota_ram_read_status() & FLASH_PROTECT_MASK) != 0)
        {
          return -1;
        }
    }

  boot_ota_ram_read32(address, verify);
  for (index = 0; index < 8u; index++)
    {
      uint32_t offset = index * 4u;

      if (verify[index] != 0xffffffffu)
        {
          goto restore;
        }

      words[index] = (uint32_t)data[offset] |
                     ((uint32_t)data[offset + 1u] << 8) |
                     ((uint32_t)data[offset + 2u] << 16) |
                     ((uint32_t)data[offset + 3u] << 24);
      OTA_REG32(FLASH_DATA_SW_FLASH) = words[index];
    }

  boot_ota_ram_trigger(FLASH_COMMAND_PROGRAM, address);
  boot_ota_ram_read32(address, verify);
  result = 0;
  for (index = 0; index < 8u; index++)
    {
      if (verify[index] != words[index])
        {
          result = -1;
        }
    }

restore:
  status = boot_ota_ram_read_status();
  status = (status & ~FLASH_PROTECT_MASK) |
           (status_before & FLASH_PROTECT_MASK);
  boot_ota_ram_write_status(status);
  if ((boot_ota_ram_read_status() & FLASH_PROTECT_MASK) !=
      (status_before & FLASH_PROTECT_MASK))
    {
      result = -1;
    }

  boot_ota_ram_wdt_feed();
  return result;
}
