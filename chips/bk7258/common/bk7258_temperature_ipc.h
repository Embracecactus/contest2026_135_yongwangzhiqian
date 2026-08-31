/****************************************************************************
 * chips/bk7258/common/bk7258_temperature_ipc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private CP/AP wire ABI for the BK7258 on-die temperature service.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_COMMON_BK7258_TEMPERATURE_IPC_H
#define __ARCH_ARM_SRC_BK7258_COMMON_BK7258_TEMPERATURE_IPC_H

#include <stdint.h>

#define BK7258_TEMPERATURE_EPT_NAME          "bk7258-temperature"
#define BK7258_TEMPERATURE_MAGIC             0x31504d54u /* "TMP1" */
#define BK7258_TEMPERATURE_VERSION           1u
#define BK7258_TEMPERATURE_ENDPOINT_WAIT_MS  3000u
#define BK7258_TEMPERATURE_SEND_TIMEOUT_MS   100u
#define BK7258_TEMPERATURE_REPLY_TIMEOUT_MS  3500u
#define BK7258_TEMPERATURE_REQUEST_ATTEMPTS  2u

enum bk7258_temperature_command_e
{
  BK7258_TEMPERATURE_COMMAND_READ = 1,
  BK7258_TEMPERATURE_COMMAND_RESPONSE
};

struct bk7258_temperature_wire_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t generation;
  uint32_t sequence;
  int32_t status;
  uint32_t raw_code;
  uint32_t flags;
  uint32_t reserved;
};

_Static_assert(sizeof(struct bk7258_temperature_wire_s) == 32u,
               "BK7258 temperature wire ABI must remain 32 bytes");

#endif /* __ARCH_ARM_SRC_BK7258_COMMON_BK7258_TEMPERATURE_IPC_H */
