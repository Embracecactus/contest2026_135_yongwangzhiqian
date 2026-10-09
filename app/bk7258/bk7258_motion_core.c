/****************************************************************************
 * app/bk7258/bk7258_motion_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable single-sample motion policy.
 ****************************************************************************/

#include "bk7258_motion_core.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static bool bkmotion_scaled_valid(float value)
{
  double scaled = (double)value * 1000.0;

  return isfinite(value) && scaled >= (double)INT32_MIN &&
         scaled <= (double)INT32_MAX;
}

static int32_t bkmotion_scale_mms2(float value)
{
  return (int32_t)((double)value * 1000.0);
}

bool bkmotion_rpc_request_valid(const struct bkmotion_rpc_request_s *request)
{
  return request != NULL && request->magic == BKMOTION_RPC_MAGIC &&
         request->version == BKMOTION_RPC_VERSION &&
         request->command == BKMOTION_RPC_SAMPLE && request->session != 0 &&
         request->sequence != 0 && request->reserved[0] == 0 &&
         request->reserved[1] == 0;
}

bool bkmotion_rpc_response_valid(const struct bkmotion_rpc_response_s *response)
{
  if (response == NULL || response->magic != BKMOTION_RPC_MAGIC ||
      response->version != BKMOTION_RPC_VERSION ||
      response->command != BKMOTION_RPC_RESPONSE || response->session == 0 ||
      response->sequence == 0 || response->reserved0 != 0 ||
      response->reserved[0] != 0 || response->reserved[1] != 0 ||
      response->rpc_status > 0 || response->operation_status > 0 ||
      (response->flags & ~BKMOTION_FLAG_SAMPLE_VALID) != 0)
    {
      return false;
    }

  if (response->rpc_status < 0 || response->operation_status < 0)
    {
      return response->flags == 0 && response->timestamp_us == 0 &&
             response->x_mms2 == 0 && response->y_mms2 == 0 &&
             response->z_mms2 == 0 && response->sensor_status == 0;
    }

  return response->flags == BKMOTION_FLAG_SAMPLE_VALID &&
         response->timestamp_us != 0;
}

void bkmotion_rpc_make_response(struct bkmotion_rpc_response_s *response,
                                const struct bkmotion_rpc_request_s *request,
                                int rpc_status)
{
  if (response == NULL)
    {
      return;
    }

  memset(response, 0, sizeof(*response));
  response->magic = BKMOTION_RPC_MAGIC;
  response->version = BKMOTION_RPC_VERSION;
  response->command = BKMOTION_RPC_RESPONSE;
  response->rpc_status = rpc_status;
  response->operation_status = rpc_status < 0 ? rpc_status : -ENODATA;
  if (request != NULL)
    {
      response->session = request->session;
      response->sequence = request->sequence;
    }
}

int bkmotion_rpc_handle_request(const struct bkmotion_rpc_request_s *request,
                                struct bkmotion_rpc_response_s *response,
                                const struct bkmotion_source_ops_s *ops,
                                void *context)
{
  struct bkmotion_sample_s sample;
  int result;
  int close_result;

  if (response == NULL)
    {
      return -EINVAL;
    }

  if (!bkmotion_rpc_request_valid(request) || ops == NULL ||
      ops->open == NULL || ops->read == NULL || ops->close == NULL)
    {
      bkmotion_rpc_make_response(response, request, -EINVAL);
      return -EINVAL;
    }

  bkmotion_rpc_make_response(response, request, 0);
  result = ops->open(context);
  if (result < 0)
    {
      response->operation_status = result;
      return result;
    }

  memset(&sample, 0, sizeof(sample));
  result = ops->read(context, &sample);
  if (result >= 0 &&
      (sample.timestamp_us == 0 || !bkmotion_scaled_valid(sample.x) ||
       !bkmotion_scaled_valid(sample.y) || !bkmotion_scaled_valid(sample.z)))
    {
      result = -ERANGE;
    }

  close_result = ops->close(context);
  /* The operation result is the first error after a successful open.  Close
   * is still mandatory, but may only become the reported error when reading
   * and sample validation succeeded.
   */

  if (result < 0)
    {
      response->operation_status = result;
      return result;
    }

  if (close_result < 0)
    {
      response->operation_status = close_result;
      return close_result;
    }

  response->timestamp_us = sample.timestamp_us;
  response->x_mms2 = bkmotion_scale_mms2(sample.x);
  response->y_mms2 = bkmotion_scale_mms2(sample.y);
  response->z_mms2 = bkmotion_scale_mms2(sample.z);
  response->sensor_status = sample.status;
  response->flags = BKMOTION_FLAG_SAMPLE_VALID;
  response->operation_status = 0;
  return 0;
}

/* Arithmetic uses double before subtraction/multiplication so even a valid
 * full-range int32 wire sample cannot overflow integer intermediates. */
static double bkmotion_dot(const int32_t a[3], const int32_t b[3])
{
  return (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
}

static double bkmotion_distance(const int32_t a[3], const int32_t b[3])
{
  double x = (double)a[0] - b[0];
  double y = (double)a[1] - b[1];
  double z = (double)a[2] - b[2];
  return x * x + y * y + z * z;
}

static bool bkmotion_gravity(const struct bkmotion_actions_config_s *c,
                             const int32_t vector[3])
{
  double norm = bkmotion_dot(vector, vector);
  return norm >= (double)c->gravity_min_mms2 * c->gravity_min_mms2 &&
         norm <= (double)c->gravity_max_mms2 * c->gravity_max_mms2;
}

static bool bkmotion_aligned(const int32_t a[3], const int32_t b[3],
                             uint16_t cosine)
{
  double dot = bkmotion_dot(a, b);
  return dot >= 0 && dot * dot * 1000000.0 >=
    (double)cosine * cosine * bkmotion_dot(a, a) * bkmotion_dot(b, b);
}

/* Invalid observations rebase motion, not the already emitted event budget. */
static void bkmotion_actions_rebase(struct bkmotion_actions_s *s)
{
  uint64_t event_us = s->event_us;
  bool emitted = s->emitted;
  memset(s, 0, sizeof(*s));
  s->event_us = event_us;
  s->emitted = emitted;
}

int bkmotion_actions_step(struct bkmotion_actions_s *s,
                          const struct bkmotion_actions_config_s *c,
                          const struct bkmotion_rpc_response_s *sample,
                          bool admitted, enum bkmotion_action_e *event)
{
  int32_t vector[3];
  enum bkmotion_action_e candidate = BKMOTION_ACTION_NONE;
  uint64_t now;

  if (event == NULL || s == NULL) return -EINVAL;
  *event = BKMOTION_ACTION_NONE;
  if (c == NULL || c->quiet_mms2 == 0 || c->move_mms2 <= c->quiet_mms2 ||
      c->gravity_min_mms2 == 0 || c->gravity_max_mms2 <= c->gravity_min_mms2 ||
      c->tilt_enter_cos == 0 || c->tilt_leave_cos <= c->tilt_enter_cos ||
      c->tilt_leave_cos > 1000 || !c->settle_us || !c->max_gap_us ||
      !c->cooldown_us)
    {
      bkmotion_actions_rebase(s);
      return -EINVAL;
    }

  if (!admitted)
    {
      bkmotion_actions_rebase(s);
      return 0;
    }

  if (!bkmotion_rpc_response_valid(sample) || sample->rpc_status < 0 ||
      sample->operation_status < 0)
    {
      bkmotion_actions_rebase(s);
      return -ENODATA;
    }

  now = sample->timestamp_us;
  if (s->initialized && now <= s->last_us)
    {
      if (now < s->last_us) bkmotion_actions_rebase(s);
      return -ESTALE;
    }

  vector[0] = sample->x_mms2;
  vector[1] = sample->y_mms2;
  vector[2] = sample->z_mms2;
  if (s->initialized && now - s->last_us > c->max_gap_us)
    bkmotion_actions_rebase(s);
  if (!s->initialized)
    {
      memcpy(s->previous, vector, sizeof(vector));
      memcpy(s->anchor, vector, sizeof(vector));
      memcpy(s->reference, vector, sizeof(vector));
      s->reference_valid = bkmotion_gravity(c, vector);
      s->last_us = now;
      s->stable_us = now;
      s->initialized = true;
      return 0;
    }

  if (bkmotion_distance(vector, s->anchor) >
      (double)c->quiet_mms2 * c->quiet_mms2)
    {
      memcpy(s->anchor, vector, sizeof(vector));
      s->stable_us = now;
    }

  if (bkmotion_distance(vector, s->previous) >=
      (double)c->move_mms2 * c->move_mms2)
    {
      if (!s->moving) candidate = BKMOTION_ACTION_MOVED;
      s->moving = true;
    }
  else if (now - s->stable_us >= c->settle_us)
    {
      if (s->moving)
        {
          s->moving = false;
          candidate = BKMOTION_ACTION_SETTLED;
        }
      else if (bkmotion_gravity(c, vector))
        {
          if (!s->reference_valid)
            {
              memcpy(s->reference, vector, sizeof(vector));
              s->reference_valid = true;
            }
          else if (s->tilted)
            {
              if (bkmotion_aligned(vector, s->reference, c->tilt_leave_cos))
                s->tilted = false;
            }
          else if (!bkmotion_aligned(vector, s->reference, c->tilt_enter_cos))
            {
              s->tilted = true;
              candidate = BKMOTION_ACTION_TILTED;
            }
        }
    }

  memcpy(s->previous, vector, sizeof(vector));
  s->last_us = now;
  if (candidate != BKMOTION_ACTION_NONE &&
      (!s->emitted || (now >= s->event_us &&
                      now - s->event_us >= c->cooldown_us)))
    {
      s->emitted = true;
      s->event_us = now;
      *event = candidate;
    }

  return 0;
}
