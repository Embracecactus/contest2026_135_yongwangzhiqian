/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include <common/bk_err.h>
#include <nuttx/mutex.h>

#include "bk7258_sdk_runtime.h"

static bk_err_t g_ckmn_result;
static bk_err_t g_ipc_result;
static bk_err_t g_mb_ipc_result;
static int g_bk_ipc_result;
static unsigned int g_sys_calls;
static unsigned int g_ckmn_calls;
static unsigned int g_ipc_calls;
static unsigned int g_mb_ipc_calls;
static unsigned int g_bk_ipc_calls;

void sys_drv_init(void)
{
  g_sys_calls++;
}

bk_err_t bk_ckmn_driver_init(void)
{
  g_ckmn_calls++;
  return g_ckmn_result;
}

bk_err_t ipc_init(void)
{
  g_ipc_calls++;
  return g_ipc_result;
}

bk_err_t mb_ipc_init(void)
{
  g_mb_ipc_calls++;
  return g_mb_ipc_result;
}

int bk_ipc_init(void)
{
  g_bk_ipc_calls++;
  return g_bk_ipc_result;
}

int main(void)
{
  mock_mutex_fail_next(1);
  assert(bk7258_sdk_runtime_initialize() == -EAGAIN);
  assert(g_sys_calls == 0);

  g_ckmn_result = BK_ERR_NOT_INIT;
  assert(bk7258_sdk_runtime_initialize() == -EIO);
  assert(g_sys_calls == 1 && g_ckmn_calls == 1);
  assert(g_ipc_calls == 0);

  g_ckmn_result = BK_OK;
  g_ipc_result = BK_ERR_NOT_INIT;
  assert(bk7258_sdk_runtime_initialize() == -EIO);
  assert(g_sys_calls == 2 && g_ckmn_calls == 2 && g_ipc_calls == 1);
  assert(g_mb_ipc_calls == 0);

  g_ipc_result = BK_OK;
  g_mb_ipc_result = BK_ERR_NOT_INIT;
  assert(bk7258_sdk_runtime_initialize() == -EIO);
  assert(g_mb_ipc_calls == 1 && g_bk_ipc_calls == 0);

  g_mb_ipc_result = BK_OK;
  g_bk_ipc_result = BK_ERR_NOT_INIT;
  assert(bk7258_sdk_runtime_initialize() == -EIO);
  assert(g_bk_ipc_calls == 1);

  g_bk_ipc_result = BK_OK;
  assert(bk7258_sdk_runtime_initialize() == 0);
  assert(g_sys_calls == 5 && g_ckmn_calls == 5 && g_ipc_calls == 4);
  assert(g_mb_ipc_calls == 3 && g_bk_ipc_calls == 2);

  assert(bk7258_sdk_runtime_initialize() == 0);
  assert(g_sys_calls == 5 && g_ckmn_calls == 5 && g_ipc_calls == 4);
  assert(g_mb_ipc_calls == 3 && g_bk_ipc_calls == 2);

  puts("BK7258_SDK_RUNTIME_TEST_PASS cases=6");
  return 0;
}
