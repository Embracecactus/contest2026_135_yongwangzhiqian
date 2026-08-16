/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_flash_guard.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned serialization and partition-permission guard for CP raw Flash.
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

#include <arch/board/bk7258_image_layout.h>

#include <driver/flash.h>

#include "bk7258_flash_guard.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_flash_guard = NXMUTEX_INITIALIZER;
static volatile pid_t g_bk7258_flash_guard_pid = (pid_t)-1;
static volatile enum bk7258_flash_guard_owner_e
  g_bk7258_flash_guard_owner;
static volatile unsigned int g_bk7258_flash_guard_depth;

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
         g_bk7258_flash_guard_pid == nxsched_gettid() &&
         bk7258_flash_guard_range(owner, addr, size);
}

int bk7258_flash_guard_lock(enum bk7258_flash_guard_owner_e owner,
                            bool write_access, uint32_t timeout_ms)
{
  int ret;

  if (up_interrupt_context() ||
      (owner != BK7258_FLASH_GUARD_DATA &&
       owner != BK7258_FLASH_GUARD_MCUBOOT))
    {
      return -EINVAL;
    }

  /* Permit only same-task, same-owner nesting; other tasks still block on
   * the mutex. */

  if (g_bk7258_flash_guard_pid == nxsched_gettid() &&
      g_bk7258_flash_guard_owner == owner &&
      g_bk7258_flash_guard_depth != 0)
    {
      g_bk7258_flash_guard_depth++;
      __asm volatile ("dmb sy" ::: "memory");
      return OK;
    }

  if (write_access && owner == BK7258_FLASH_GUARD_MCUBOOT)
    {
      return -EROFS;
    }

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
  g_bk7258_flash_guard_pid = nxsched_gettid();
  g_bk7258_flash_guard_depth = 1;
  __asm volatile ("dmb sy" ::: "memory");
  return OK;
}

void bk7258_flash_guard_unlock(void)
{
  DEBUGASSERT(g_bk7258_flash_guard_pid == nxsched_gettid());

  DEBUGASSERT(g_bk7258_flash_guard_depth != 0);
  if (g_bk7258_flash_guard_depth > 1)
    {
      g_bk7258_flash_guard_depth--;
      __asm volatile ("dmb sy" ::: "memory");
      return;
    }

  __asm volatile ("dmb sy" ::: "memory");
  g_bk7258_flash_guard_pid = (pid_t)-1;
  g_bk7258_flash_guard_owner = 0;
  g_bk7258_flash_guard_depth = 0;
  nxmutex_unlock(&g_bk7258_flash_guard);
}
