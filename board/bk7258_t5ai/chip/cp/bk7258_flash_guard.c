/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/bk7258_flash_guard.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-only serialization and SDK partition-permission guard for raw Flash.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>

#include <arch/chip/bk7258_amp.h>

#include <driver/flash.h>

#include "bk7258_flash_guard.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_FLASH_REMAP_ENABLE 0x44030064u
#define BK7258_REG32(address) \
  (*(volatile uint32_t *)(uintptr_t)(address))

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_flash_guard = NXMUTEX_INITIALIZER;
static volatile pid_t g_bk7258_flash_guard_pid = (pid_t)-1;
static volatile enum bk7258_flash_guard_owner_e
  g_bk7258_flash_guard_owner;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_flash_range(uint32_t addr, uint32_t size,
                               uint32_t start, uint32_t length)
{
  uint32_t offset;

  if (size == 0 || addr < start)
    {
      return false;
    }

  offset = addr - start;
  return offset < length && size <= length - offset;
}

static bool bk7258_flash_guard_range(
  enum bk7258_flash_guard_owner_e owner, uint32_t addr, uint32_t size)
{
  if (owner == BK7258_FLASH_GUARD_DATA)
    {
      return bk7258_flash_range(addr, size,
                                BK7258_DATA_RAW_PHYSICAL_OFFSET,
                                BK7258_DATA_RAW_PHYSICAL_SIZE);
    }

#ifdef CONFIG_BK7258_OTA_STAGING_WRITE
  if (owner == BK7258_FLASH_GUARD_OTA_STAGING)
    {
      if ((BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) & 1u) != 0)
        {
          return bk7258_flash_range(addr, size,
                                    BK7258_ROLE_SLOT_A_CP_OFFSET,
                                    BK7258_ROLE_SLOT_B_PAIR_SIZE);
        }

      return bk7258_flash_range(addr, size,
                                BK7258_ROLE_SLOT_B_PAIR_OFFSET,
                                BK7258_ROLE_SLOT_B_PAIR_SIZE);
    }
#endif

#ifdef CONFIG_BK7258_OTA_TRIAL_WRITE
  if (owner == BK7258_FLASH_GUARD_OTA_METADATA)
    {
      return bk7258_flash_range(
               addr, size, BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET,
               BK7258_ROLE_OTA_METADATA_PRIMARY_SIZE) ||
             bk7258_flash_range(
               addr, size, BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET,
               BK7258_ROLE_OTA_METADATA_MIRROR_SIZE);
    }
#endif

  return false;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool bk7258_flash_guard_write_authorized(uint32_t addr, uint32_t size)
{
  enum bk7258_flash_guard_owner_e owner;

  owner = g_bk7258_flash_guard_owner;
  return !up_interrupt_context() &&
         g_bk7258_flash_guard_pid == nxsched_getpid() &&
         bk7258_flash_guard_range(owner, addr, size);
}

int bk7258_flash_guard_lock(enum bk7258_flash_guard_owner_e owner,
                            bool write_access, uint32_t timeout_ms)
{
  int ret;

  if (up_interrupt_context() ||
      (owner != BK7258_FLASH_GUARD_DATA &&
       owner != BK7258_FLASH_GUARD_OTA_STAGING &&
       owner != BK7258_FLASH_GUARD_OTA_METADATA))
    {
      return -EINVAL;
    }

#ifndef CONFIG_BK7258_OTA_STAGING_WRITE
  if (write_access && owner == BK7258_FLASH_GUARD_OTA_STAGING)
    {
      return -EACCES;
    }
#endif

#ifndef CONFIG_BK7258_OTA_TRIAL_WRITE
  if (write_access && owner == BK7258_FLASH_GUARD_OTA_METADATA)
    {
      return -EACCES;
    }
#endif

  if (timeout_ms == 0)
    {
      ret = nxmutex_lock(&g_bk7258_flash_guard);
    }
  else
    {
      ret = nxmutex_timedlock(&g_bk7258_flash_guard, timeout_ms);
    }

  if (ret < 0)
    {
      return ret;
    }

  g_bk7258_flash_guard_owner = write_access ? owner :
                               BK7258_FLASH_GUARD_NONE;
  g_bk7258_flash_guard_pid = nxsched_getpid();
  __asm volatile ("dmb sy" ::: "memory");
  return OK;
}

void bk7258_flash_guard_unlock(void)
{
  DEBUGASSERT(g_bk7258_flash_guard_pid == nxsched_getpid());

  __asm volatile ("dmb sy" ::: "memory");
  g_bk7258_flash_guard_pid = (pid_t)-1;
  g_bk7258_flash_guard_owner = 0;
  nxmutex_unlock(&g_bk7258_flash_guard);
}
