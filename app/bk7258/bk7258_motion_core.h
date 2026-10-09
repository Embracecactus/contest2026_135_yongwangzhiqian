/****************************************************************************
 * app/bk7258/bk7258_motion_core.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_MOTION_CORE_H
#define __APP_BK7258_BK7258_MOTION_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "bk7258_motion_protocol.h"

/* Keep the policy core independent of the NuttX sensor implementation.  The
 * AP adapter translates one public struct sensor_accel into this value type;
 * host tests therefore exercise the wire policy without private driver data.
 */

struct bkmotion_sample_s
{
  uint64_t timestamp_us;
  float x;
  float y;
  float z;
  int32_t status;
};

struct bkmotion_source_ops_s
{
  int (*open)(void *context);
  int (*read)(void *context, struct bkmotion_sample_s *sample);
  int (*close)(void *context);
};

bool bkmotion_rpc_request_valid(const struct bkmotion_rpc_request_s *request);
bool bkmotion_rpc_response_valid(const struct bkmotion_rpc_response_s *response);
void bkmotion_rpc_make_response(struct bkmotion_rpc_response_s *response,
                                const struct bkmotion_rpc_request_s *request,
                                int rpc_status);
int bkmotion_rpc_handle_request(const struct bkmotion_rpc_request_s *request,
                                struct bkmotion_rpc_response_s *response,
                                const struct bkmotion_source_ops_s *ops,
                                void *context);

/* Single-owner candidate detector. Parameters must be supplied by a reviewed
 * sampling policy; there are no hardware-tuned defaults in this component.
 * Zero-initialize state once per owner lifetime; keep configuration immutable.
 * MOVED/SETTLED are acceleration candidates, not proof of pickup or putdown.
 */
enum bkmotion_action_e
{
  BKMOTION_ACTION_NONE = 0,
  BKMOTION_ACTION_MOVED,
  BKMOTION_ACTION_SETTLED,
  BKMOTION_ACTION_TILTED
};

struct bkmotion_actions_config_s
{
  uint32_t move_mms2;
  uint32_t quiet_mms2;
  uint32_t gravity_min_mms2;
  uint32_t gravity_max_mms2;
  uint16_t tilt_enter_cos; /* cosine multiplied by 1000 */
  uint16_t tilt_leave_cos;
  uint64_t settle_us;
  uint64_t cooldown_us;
  uint64_t max_gap_us;
};

struct bkmotion_actions_s
{
  int32_t previous[3];
  int32_t anchor[3];
  int32_t reference[3];
  uint64_t last_us;
  uint64_t stable_us;
  uint64_t event_us;
  bool initialized;
  bool reference_valid;
  bool moving;
  bool tilted;
  bool emitted;
};

/* No I/O or queued side effects. Errors/gates reset candidates; duplicate time
 * is rejected without advancing dwell. A time reversal resets the baseline.
 * Gaps restart from one observation, never invent continuity or replay events.
 * Admission/errors reset candidates but retain the emitted-event cooldown.
 */
int bkmotion_actions_step(struct bkmotion_actions_s *state,
                          const struct bkmotion_actions_config_s *config,
                          const struct bkmotion_rpc_response_s *sample,
                          bool admitted, enum bkmotion_action_e *event);

#endif /* __APP_BK7258_BK7258_MOTION_CORE_H */
