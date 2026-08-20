/*
 * boot_flash.c - read-only BK7258 BL1 raw-Flash access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include "boot_flash.h"
#include "boot_wdt.h"
#include <bk7258_partitions.h>

#define BL1_REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define FLASH_CONTROLLER_BASE       0x44030000u
#define FLASH_OP_CTRL               (FLASH_CONTROLLER_BASE + 0x10u)
#define FLASH_DATA_FLASH_TO_SW      (FLASH_CONTROLLER_BASE + 0x18u)
#define FLASH_OP_CMD                (FLASH_CONTROLLER_BASE + 0x54u)

#define FLASH_OP_SW                 (1u << 29)
#define FLASH_BUSY_SW               (1u << 31)
#define FLASH_ADDRESS_MASK          0x00ffffffu
#define FLASH_COMMAND_SHIFT         24u
#define FLASH_COMMAND_MASK          (0x1fu << FLASH_COMMAND_SHIFT)
#define FLASH_COMMAND_READ          5u
#define FLASH_READ_GRANULE          32u
#define FLASH_READ_WORDS            8u
#define FLASH_WAIT_BUDGET           0x01000000u

static int flash_wait_idle(void)
{
  uint32_t remaining = FLASH_WAIT_BUDGET;

  while ((BL1_REG32(FLASH_OP_CTRL) & FLASH_BUSY_SW) != 0)
    {
      if ((remaining & 0xffffu) == 0)
        {
          boot_wdt_feed();
        }

      if (remaining == 0)
        {
          return -1;
        }

      remaining--;
    }

  return 0;
}

static int flash_read_aligned(uint32_t address,
                              uint8_t output[FLASH_READ_GRANULE])
{
  uint32_t command;
  uint32_t index;

  if ((address & (FLASH_READ_GRANULE - 1u)) != 0 ||
      address > BK7258_FLASH_SIZE - FLASH_READ_GRANULE ||
      flash_wait_idle() < 0)
    {
      return -1;
    }

  command = BL1_REG32(FLASH_OP_CMD);
  command &= ~(FLASH_ADDRESS_MASK | FLASH_COMMAND_MASK);
  command |= address | (FLASH_COMMAND_READ << FLASH_COMMAND_SHIFT);
  BL1_REG32(FLASH_OP_CMD) = command;
  BL1_REG32(FLASH_OP_CTRL) = BL1_REG32(FLASH_OP_CTRL) | FLASH_OP_SW;
  if (flash_wait_idle() < 0)
    {
      return -1;
    }

  for (index = 0; index < FLASH_READ_WORDS; index++)
    {
      uint32_t word = BL1_REG32(FLASH_DATA_FLASH_TO_SW);
      output[index * 4u] = (uint8_t)word;
      output[index * 4u + 1u] = (uint8_t)(word >> 8);
      output[index * 4u + 2u] = (uint8_t)(word >> 16);
      output[index * 4u + 3u] = (uint8_t)(word >> 24);
    }

  boot_wdt_feed();
  return 0;
}

int bk7258_bl1_flash_read(uint32_t address, uint8_t *buffer, size_t len)
{
  uint8_t block[FLASH_READ_GRANULE];

  if (buffer == NULL || len == 0 || address >= BK7258_FLASH_SIZE ||
      len > BK7258_FLASH_SIZE - address)
    {
      return -1;
    }

  while (len != 0)
    {
      uint32_t aligned = address & ~(FLASH_READ_GRANULE - 1u);
      uint32_t offset = address - aligned;
      size_t count = FLASH_READ_GRANULE - offset;
      size_t index;

      if (count > len)
        {
          count = len;
        }

      if (flash_read_aligned(aligned, block) < 0)
        {
          return -1;
        }

      for (index = 0; index < count; index++)
        {
          buffer[index] = block[offset + index];
        }

      address += (uint32_t)count;
      buffer += count;
      len -= count;
    }

  return 0;
}
