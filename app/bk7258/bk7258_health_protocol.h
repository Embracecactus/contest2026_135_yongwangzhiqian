/****************************************************************************
 * app/bk7258/bk7258_health_protocol.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned CP command to AP device-health service wire contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_HEALTH_PROTOCOL_H
#define __APP_BK7258_BK7258_HEALTH_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BKHEALTH_RPC_MAGIC            0x424b484cu /* BKHL */
#define BKHEALTH_RPC_VERSION          1u
#define BKHEALTH_RPC_ENDPOINT         "bkhealth-v1"
#define BKHEALTH_RPC_ENDPOINT_WAIT_MS 3000u
#define BKHEALTH_RPC_SEND_WAIT_MS     1000u
#define BKHEALTH_RPC_REPLY_WAIT_MS    10000u
#define BKHEALTH_RPC_ATTEMPTS         2u

#define BKHEALTH_FLAG_BATTERY_STATE_VALID \
  (1u << 0)
#define BKHEALTH_FLAG_BATTERY_VOLTAGE_VALID \
  (1u << 1)
#define BKHEALTH_FLAG_TEMPERATURE_RAW_VALID \
  (1u << 2)
#define BKHEALTH_FLAG_TEMPERATURE_CALIBRATED \
  (1u << 3)
#define BKHEALTH_FLAG_TEMPERATURE_MILLICELSIUS_VALID \
  (1u << 4)

enum bkhealth_rpc_command_e
{
  BKHEALTH_RPC_STATUS = 1,
  BKHEALTH_RPC_RESPONSE = 0x8000,
};

/* These values intentionally mirror the stable NuttX battery_status_e ABI.
 * Keeping them in the App protocol avoids exposing a NuttX header to CP.
 */

enum bkhealth_battery_state_e
{
  BKHEALTH_BATTERY_UNKNOWN = 0,
  BKHEALTH_BATTERY_FAULT = 1,
  BKHEALTH_BATTERY_IDLE = 2,
  BKHEALTH_BATTERY_FULL = 3,
  BKHEALTH_BATTERY_CHARGING = 4,
  BKHEALTH_BATTERY_DISCHARGING = 5,
};

struct bkhealth_rpc_request_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  uint32_t reserved[2];
};

struct bkhealth_rpc_response_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  int32_t rpc_status;
  int32_t operation_status;
  int32_t battery_state_status;
  int32_t battery_voltage_status;
  int32_t temperature_status;
  uint32_t flags;
  uint32_t battery_state;
  int32_t battery_voltage_mv;
  uint32_t temperature_raw_code;
  uint32_t temperature_reference_raw;
  int32_t temperature_millicelsius;
  uint32_t temperature_generation;
  uint32_t temperature_sequence;
  uint32_t reserved[2];
};

/* CP and AP are separate images.  Keep wire drift a compile-time failure.
 * All integer fields use the native little-endian BK7258 representation.
 */

_Static_assert(sizeof(struct bkhealth_rpc_request_s) == 24,
               "bkhealth request wire size changed");
_Static_assert(offsetof(struct bkhealth_rpc_request_s, session) == 8,
               "bkhealth request session offset changed");
_Static_assert(offsetof(struct bkhealth_rpc_request_s, reserved) == 16,
               "bkhealth request reserved offset changed");
_Static_assert(sizeof(struct bkhealth_rpc_response_s) == 76,
               "bkhealth response wire size changed");
_Static_assert(offsetof(struct bkhealth_rpc_response_s, rpc_status) == 16,
               "bkhealth response status offset changed");
_Static_assert(offsetof(struct bkhealth_rpc_response_s, flags) == 36,
               "bkhealth response flags offset changed");
_Static_assert(offsetof(struct bkhealth_rpc_response_s,
                        temperature_millicelsius) == 56,
               "bkhealth response temperature offset changed");

int bkhealth_rpc_client_initialize(void);
int bkhealth_rpc_exchange(struct bkhealth_rpc_request_s *request,
                          struct bkhealth_rpc_response_s *response,
                          unsigned int timeout_ms);

#endif /* __APP_BK7258_BK7258_HEALTH_PROTOCOL_H */
