/****************************************************************************
 * chips/bk7258/common/bk7258_sdk_runtime.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Chip-owned wrapper for the minimum v3.1.1.9 system-register, mailbox and
 * CP clock-monitor runtime required by SDK-backed drivers.  This deliberately
 * does not call the SDK's full driver_init(), because NuttX owns peripheral
 * selection and initialization.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_sdk_runtime.h>

#ifdef CONFIG_BK7258_SWD_DEBUG
#  include <arch/chip/bk7258_debug.h>
#endif

#include <common/bk_err.h>

#ifndef CONFIG_BK7258_AP_CORE
#  include <driver/ckmn.h>
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* These v3.1.1.9 entry points are source-verified in the official SDK, but
 * its immutable exported-header bundle only declares mb_ipc_init().
 */

extern void sys_drv_init(void);
extern bk_err_t ipc_init(void);
extern bk_err_t mb_ipc_init(void);
extern int bk_ipc_init(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_sdk_runtime_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_sdk_runtime_ready;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_sdk_runtime_initialize(void)
{
  bk_err_t ret;
  int result;

  result = nxmutex_lock(&g_bk7258_sdk_runtime_lock);
  if (result < 0)
    {
      return result;
    }

  if (g_bk7258_sdk_runtime_ready)
    {
      nxmutex_unlock(&g_bk7258_sdk_runtime_lock);
      return OK;
    }

  /* Match the relevant subset of official driver_init() ordering.  Both
   * images must initialize their local SYS_REG AMP lock before the AP client
   * can ask the CP server for ownership.  The three mailbox layers use
   * distinct logical channels and are independently idempotent.
   */

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_SDK_BEFORE_SYS);
#endif

  sys_drv_init();

#ifndef CONFIG_BK7258_AP_CORE
  /* The immutable CP Wi-Fi library starts temp_detect, whose periodic ROSC
   * compensation calls bk_rosc_32k_ckest_prog().  Official driver_init()
   * initializes CKMN before launching Wi-Fi; the NuttX-owned minimal runtime
   * must preserve that hardware dependency explicitly.  Without it every
   * temperature interval returns BK_ERR_CKMN_DRIVER_NOT_INIT (-0x4601).
   */

  ret = bk_ckmn_driver_init();
  if (ret != BK_OK)
    {
      result = -EIO;
      goto out;
    }
#endif

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_SDK_AFTER_SYS);
#endif

  ret = ipc_init();
#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_SDK_AFTER_IPC);
#endif
  if (ret != BK_OK)
    {
      result = -EIO;
      goto out;
    }

  ret = mb_ipc_init();
#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_SDK_AFTER_MB_IPC);
#endif
  if (ret != BK_OK)
    {
      result = -EIO;
      goto out;
    }

  result = bk_ipc_init();
#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_SDK_AFTER_BK_IPC);
#endif
  if (result != BK_OK)
    {
      result = -EIO;
      goto out;
    }

  g_bk7258_sdk_runtime_ready = true;
  result = OK;

#ifdef CONFIG_BK7258_SWD_DEBUG
  bk7258_swd_trace_snapshot(BK7258_SWD_TRACE_SDK_EXIT);
#endif

out:
  nxmutex_unlock(&g_bk7258_sdk_runtime_lock);
  return result;
}
