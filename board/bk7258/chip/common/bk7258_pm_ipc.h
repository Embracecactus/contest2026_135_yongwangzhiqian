/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/
 * bk7258_pm_ipc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private CP/AP wire ABI for the BK7258 peripheral clock service.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_COMMON_BK7258_PM_IPC_H
#define __BOARD_BK7258_CHIP_COMMON_BK7258_PM_IPC_H

#include <stdint.h>

#define BK7258_PM_EPT_NAME             "bk7258-pm"
#define BK7258_PM_MAGIC                0x314d5042u /* "BPM1" */
#define BK7258_PM_VERSION              2u
#define BK7258_PM_ENDPOINT_WAIT_MS     3000u
#define BK7258_PM_SEND_TIMEOUT_MS      100u
#define BK7258_PM_REPLY_TIMEOUT_MS     1000u

enum bk7258_pm_command_e
{
  BK7258_PM_COMMAND_CLOCK_GET = 1,
  BK7258_PM_COMMAND_CLOCK_PUT,
  BK7258_PM_COMMAND_CPU_FREQ_VOTE,
  BK7258_PM_COMMAND_RESPONSE
};

/* Project-owned frequency clients.  Do not send the SDK's raw pm_dev_id_e
 * across RPMsg: the AP and CP v3.1.1.9 enums diverge near their tails.
 */

enum bk7258_pm_freq_client_e
{
  BK7258_PM_FREQ_CLIENT_VIDEO = 0,
  BK7258_PM_FREQ_CLIENT_COUNT
};

/* Values intentionally match v3.1.1.9 pm_cpu_freq_e. */

enum bk7258_pm_cpu_freq_e
{
  BK7258_PM_CPU_FREQ_26M = 0,
  BK7258_PM_CPU_FREQ_60M,
  BK7258_PM_CPU_FREQ_80M,
  BK7258_PM_CPU_FREQ_120M,
  BK7258_PM_CPU_FREQ_240M,
  BK7258_PM_CPU_FREQ_320M,
  BK7258_PM_CPU_FREQ_480M,
  BK7258_PM_CPU_FREQ_DEFAULT
};

struct bk7258_pm_wire_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t generation;
  uint32_t sequence;
  uint32_t clock;      /* Clock ID, or frequency-client ID */
  int32_t status;
  uint32_t refcount;
  uint32_t reserved;   /* CPU frequency for CPU_FREQ_VOTE */
};

_Static_assert(sizeof(struct bk7258_pm_wire_s) == 32u,
               "BK7258 PM wire ABI must remain 32 bytes");

#endif /* __BOARD_BK7258_CHIP_COMMON_BK7258_PM_IPC_H */
