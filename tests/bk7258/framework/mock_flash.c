/*
 * mock_flash.c - host-side XIP flash window (mmap at the BK7258 XIP base).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mock_flash.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static uint8_t *g_window;

void *mock_flash_map(void)
{
  void *result;

  if (g_window != NULL)
    {
      return g_window;
    }

  result = mmap((void *)(uintptr_t)BK7258_HOST_FLASH_XIP_BASE,
                BK7258_HOST_FLASH_SIZE,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                -1, 0);
  if (result == MAP_FAILED)
    {
      return NULL;
    }

  g_window = result;
  memset(g_window, 0xff, BK7258_HOST_FLASH_SIZE);
  return g_window;
}

void mock_flash_erase_fill(uint32_t offset, uint32_t len)
{
  if (g_window != NULL && offset + len <= BK7258_HOST_FLASH_SIZE)
    {
      memset(g_window + offset, 0xff, len);
    }
}

void mock_flash_unmap(void)
{
  if (g_window != NULL)
    {
      munmap(g_window, BK7258_HOST_FLASH_SIZE);
      g_window = NULL;
    }
}

static uint32_t g_flash_fifo[8];
static unsigned g_flash_fifo_index;

uint32_t *mock_flash_fifo_ref(void)
{
  uint32_t *word = &g_flash_fifo[g_flash_fifo_index & 7u];

  g_flash_fifo_index++;
  return word;
}

void mock_flash_fifo_seed(const uint8_t data[32])
{
  memcpy(g_flash_fifo, data, 32);
  g_flash_fifo_index = 0;
}