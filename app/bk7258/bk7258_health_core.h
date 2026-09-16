/****************************************************************************
 * app/bk7258/bk7258_health_core.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_HEALTH_CORE_H
#define __APP_BK7258_BK7258_HEALTH_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "bk7258_health_protocol.h"

#define BKHEALTH_TEMPERATURE_RAW_VALID  (1u << 0)
#define BKHEALTH_TEMPERATURE_CALIBRATED (1u << 1)

struct bkhealth_temperature_sample_s
{
  uint32_t flags;
  uint32_t raw_code;
  uint32_t reference_raw;
  int32_t temperature_millicelsius;
  uint32_t generation;
  uint32_t sequence;
};

/* The AP transport supplies these operations.  Keeping NuttX file and chip
 * APIs behind this table makes validity and partial-failure policy directly
 * host testable.
 */

struct bkhealth_source_ops_s
{
  int (*battery_open)(void *context);
  int (*battery_state)(void *context, uint32_t *state);
  int (*battery_voltage_mv)(void *context, int32_t *voltage_mv);
  int (*battery_close)(void *context);
  int (*temperature_read)(
    void *context, struct bkhealth_temperature_sample_s *sample);
};

bool bkhealth_rpc_request_valid(
  const struct bkhealth_rpc_request_s *request);
bool bkhealth_rpc_response_valid(
  const struct bkhealth_rpc_response_s *response);
void bkhealth_rpc_make_response(
  struct bkhealth_rpc_response_s *response,
  const struct bkhealth_rpc_request_s *request, int rpc_status);
int bkhealth_rpc_handle_request(
  const struct bkhealth_rpc_request_s *request,
  struct bkhealth_rpc_response_s *response,
  const struct bkhealth_source_ops_s *ops, void *context);

#endif /* __APP_BK7258_BK7258_HEALTH_CORE_H */
