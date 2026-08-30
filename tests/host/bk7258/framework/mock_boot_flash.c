/* SPDX-License-Identifier: Apache-2.0 */
#include "mock_boot_flash.h"

#include <string.h>

#include <bk7258_partitions.h>

#include "boot_flash.h"

static uint8_t g_raw_flash[BK7258_FLASH_SIZE];
static unsigned int g_read_calls;
static unsigned int g_erase_calls;
static unsigned int g_program_calls;

void mock_boot_flash_reset(void)
{
  memset(g_raw_flash, 0xff, sizeof(g_raw_flash));
  g_read_calls = 0;
  g_erase_calls = 0;
  g_program_calls = 0;
}

uint8_t *mock_boot_flash_data(void)
{
  return g_raw_flash;
}

unsigned int mock_boot_flash_read_calls(void)
{
  return g_read_calls;
}

unsigned int mock_boot_flash_erase_calls(void)
{
  return g_erase_calls;
}

unsigned int mock_boot_flash_program_calls(void)
{
  return g_program_calls;
}

int bk7258_boot_flash_read(uint32_t address, uint8_t *buffer, size_t len)
{
  if (buffer == NULL || len == 0u || address > sizeof(g_raw_flash) ||
      len > sizeof(g_raw_flash) - address)
    {
      return -1;
    }

  g_read_calls++;
  memcpy(buffer, &g_raw_flash[address], len);
  return 0;
}

int bk7258_boot_flash_program(uint32_t address, const uint8_t *buffer,
                              size_t len)
{
  if (buffer == NULL || len == 0u || address > sizeof(g_raw_flash) ||
      len > sizeof(g_raw_flash) - address)
    {
      return -1;
    }

  g_program_calls++;
  memcpy(&g_raw_flash[address], buffer, len);
  return 0;
}

int bk7258_boot_flash_erase(uint32_t address, size_t len)
{
  if (len == 0u || address > sizeof(g_raw_flash) ||
      len > sizeof(g_raw_flash) - address)
    {
      return -1;
    }

  g_erase_calls++;
  memset(&g_raw_flash[address], 0xff, len);
  return 0;
}
