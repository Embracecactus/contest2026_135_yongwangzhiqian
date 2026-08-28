/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/common/
 * bk7258_pm_ipc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private CP/AP wire ABI for the BK7258 peripheral clock service.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_COMMON_BK7258_PM_IPC_H
#define __BOARD_BK7258_CHIP_COMMON_BK7258_PM_IPC_H

#include <stdint.h>

#include <arch/chip/bk7258_pm.h>

#define BK7258_PM_EPT_NAME             "bk7258-pm"
#define BK7258_PM_MAGIC                0x314d5042u /* "BPM1" */
#define BK7258_PM_VERSION              4u
#define BK7258_PM_ENDPOINT_WAIT_MS     3000u
#define BK7258_PM_SEND_TIMEOUT_MS      100u
#define BK7258_PM_REPLY_TIMEOUT_MS     1000u
#define BK7258_PM_REQUEST_ATTEMPTS     3u

enum bk7258_pm_command_e
{
  BK7258_PM_COMMAND_CLOCK_GET = 1,
  BK7258_PM_COMMAND_CLOCK_PUT,
  BK7258_PM_COMMAND_CPU_FREQ_VOTE,
  BK7258_PM_COMMAND_CPU_FREQ_QUERY,
  BK7258_PM_COMMAND_RESPONSE
};

struct bk7258_pm_wire_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t generation;
  uint32_t sequence;
  uint32_t clock;      /* Clock/client ID; response transition count */
  int32_t status;
  uint32_t refcount;   /* Clock refcount; response current frequency */
  uint32_t reserved;   /* Vote frequency; response peak frequency */
};

_Static_assert(sizeof(struct bk7258_pm_wire_s) == 32u,
               "BK7258 PM wire ABI must remain 32 bytes");

#endif /* __BOARD_BK7258_CHIP_COMMON_BK7258_PM_IPC_H */
