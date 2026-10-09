/* SPDX-License-Identifier: Apache-2.0 */
/* Authenticated development-build input. The callbacks are production event
 * and observation boundaries; this module owns no product lifecycle state.
 */

#include "bk7258_engineering_test.h"

#include <errno.h>
#include <string.h>

#define BKENGTEST_KEY_MASK 7u

static uint32_t get32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t *p, uint32_t value)
{
  p[0] = value >> 24;
  p[1] = value >> 16;
  p[2] = value >> 8;
  p[3] = value;
}

static int valid(const struct bkengtest_ops_s *ops)
{
  return ops != NULL && ops->now_ms != NULL && ops->key_begin != NULL &&
         ops->key_event != NULL && ops->key_end != NULL &&
         ops->power_status != NULL && ops->system_status != NULL;
}

static int command_apply(struct bkengtest_s *state,
                         const struct bkengtest_ops_s *ops, void *context,
                         const uint8_t record[BKENGTEST_RECORD_SIZE])
{
  uint32_t operation;
  uint32_t session;
  uint32_t sequence;
  uint32_t value;
  uint32_t elapsed;
  uint32_t flags;
  uint32_t power_state;
  int32_t power_error;
  uint64_t now;
  bool power_accepted = false;
  int ret;

  if (memcmp(record, "BKT1", 4) || get32(record + 4) != BKENGTEST_VERSION)
    {
      return -EPROTO;
    }

  operation = get32(record + 8);
  session = get32(record + 12);
  sequence = get32(record + 16);
  value = get32(record + 20);
  elapsed = get32(record + 24);
  flags = get32(record + 28);
  if (session == 0 || flags != 0 || elapsed > BKENGTEST_ELAPSED_MAX_MS)
    {
      return -EINVAL;
    }

  now = ops->now_ms(context);
  if (operation == BKENGTEST_OP_SESSION)
    {
      if (sequence != 1 || elapsed != 0 || value > BKENGTEST_PM_LATE_ACK)
        {
          return -EINVAL;
        }

      if (state->active)
        {
          return -EBUSY;
        }

      ret = ops->power_status(context, &power_state, &power_error);
      if (ret < 0)
        {
          return ret;
        }

      if (power_state != 0)
        {
          return -EBUSY;
        }

      ret = ops->key_begin(context, session, now);
      if (ret < 0)
        {
          state->active = false;
          return ret;
        }

      state->base_ms = now;
      state->expires_at = now + BKENGTEST_SESSION_IDLE_MS;
      state->session = session;
      state->sequence = sequence;
      state->event_sequence = 0;
      state->elapsed_ms = 0;
      state->key_mask = 0;
      state->pm_mode = value;
      state->pm_requests = 0;
      state->pm_queries = 0;
      state->pm_session = 0;
      state->active = true;
      state->expired = false;
      state->power_intent = false;
      return 0;
    }

  if (!state->active || session != state->session ||
      state->sequence == UINT32_MAX || sequence != state->sequence + 1u)
    {
      return -ESTALE;
    }

  if (elapsed < state->elapsed_ms)
    {
      return -ERANGE;
    }

  if (operation == BKENGTEST_OP_KEY)
    {
      if (value & ~BKENGTEST_KEY_MASK)
        {
          return -EINVAL;
        }

      ret = ops->key_event(context, session, state->event_sequence + 1u,
                           value, state->base_ms + elapsed, &power_accepted);
      if (ret < 0)
        {
          return ret;
        }

      state->event_sequence++;
      state->key_mask = value;
      state->power_intent |= power_accepted;
    }
  else if (operation == BKENGTEST_OP_ADVANCE)
    {
      if (value != 0)
        {
          return -EINVAL;
        }
    }
  else if (operation == BKENGTEST_OP_END)
    {
      if (value != 0)
        {
          return -EINVAL;
        }

      ret = ops->key_end(context, session);
      if (ret < 0)
        {
          return ret;
        }

      state->active = false;
      state->key_mask = 0;
      state->power_intent = false;
      state->pm_session = 0;
    }
  else
    {
      return -ENOTSUP;
    }

  state->sequence = sequence;
  state->elapsed_ms = elapsed;
  state->expires_at = now + BKENGTEST_SESSION_IDLE_MS;
  return 0;
}

static int status_read(struct bkengtest_s *state,
                       const struct bkengtest_ops_s *ops, void *context,
                       uint32_t offset, struct bkcontrol_status_s *status)
{
  uint8_t wire[BKENGTEST_STATUS_SIZE];
  uint32_t power_state;
  uint32_t voice_state;
  uint32_t storage_state;
  uint32_t network_state;
  int32_t power_error;
  uint32_t flags = BKENGTEST_STATUS_ENABLED;
  int ret;

  if ((offset & 15u) || offset >= sizeof(wire))
    {
      return -ERANGE;
    }

  ret = ops->power_status(context, &power_state, &power_error);
  if (ret < 0)
    {
      return ret;
    }

  ret = ops->system_status(context, &voice_state, &storage_state,
                           &network_state);
  if (ret < 0)
    {
      return ret;
    }

  if (voice_state > BKENGTEST_VOICE_BUSY ||
      storage_state > BKENGTEST_STORAGE_READY ||
      network_state > BKENGTEST_NETWORK_READY)
    {
      return -EPROTO;
    }

  if (state->active)
    {
      flags |= BKENGTEST_STATUS_ACTIVE;
    }

  if (state->expired)
    {
      flags |= BKENGTEST_STATUS_EXPIRED;
    }

  if (state->power_intent)
    {
      flags |= BKENGTEST_STATUS_POWER_INTENT;
    }

  memset(wire, 0, sizeof(wire));
  memcpy(wire, "BKS1", 4);
  put32(wire + 4, BKENGTEST_VERSION);
  put32(wire + 8, flags);
  put32(wire + 12, state->session);
  put32(wire + 16, state->sequence);
  put32(wire + 20, state->elapsed_ms);
  put32(wire + 24, state->key_mask);
  put32(wire + 28, state->pm_mode);
  put32(wire + 32, state->pm_requests);
  put32(wire + 36, state->pm_queries);
  put32(wire + 40, power_state);
  put32(wire + 44, (uint32_t)power_error);
  put32(wire + 48, (uint32_t)state->last_result);
  put32(wire + 52, voice_state);
  put32(wire + 56, storage_state);
  put32(wire + 60, network_state);
  status->config_total = sizeof(wire);
  memcpy(status->config_chunk, wire + offset, sizeof(status->config_chunk));
  return 0;
}

int bkengtest_control(struct bkengtest_s *state,
                      const struct bkengtest_ops_s *ops, void *context,
                      enum bkcontrol_command_e command, uint32_t offset,
                      const uint8_t *record, size_t size,
                      struct bkcontrol_status_s *status)
{
  int ret;

  if (state == NULL || !valid(ops) || status == NULL)
    {
      return -EINVAL;
    }

  if (command == BKCONTROL_CONFIG_READ)
    {
      if (record != NULL || size != 0)
        {
          return -EINVAL;
        }

      return status_read(state, ops, context, offset, status);
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return offset == 0 && record == NULL && size == BKENGTEST_RECORD_SIZE ?
             0 : -EINVAL;
    }

  if (command != BKCONTROL_CONFIG_APPLY || offset != 0 || record == NULL ||
      size != BKENGTEST_RECORD_SIZE)
    {
      return -ENOTSUP;
    }

  ret = command_apply(state, ops, context, record);
  state->last_result = ret;
  return ret;
}

void bkengtest_step(struct bkengtest_s *state,
                    const struct bkengtest_ops_s *ops, void *context,
                    uint64_t now)
{
  int ret;

  if (state == NULL || !valid(ops) || !state->active ||
      now < state->expires_at)
    {
      return;
    }

  ret = ops->key_end(context, state->session);
  state->last_result = ret < 0 ? ret : -ETIMEDOUT;
  state->active = false;
  state->expired = true;
  state->key_mask = 0;
  state->power_intent = false;
  state->pm_session = 0;
}

int bkengtest_disconnect(struct bkengtest_s *state,
                         const struct bkengtest_ops_s *ops, void *context)
{
  int ret;

  if (state == NULL || !valid(ops))
    {
      return -EINVAL;
    }

  if (!state->active)
    {
      return 0;
    }

  if (state->power_intent)
    {
      return 1;
    }

  ret = ops->key_end(context, state->session);
  if (ret < 0)
    {
      state->last_result = ret;
      return ret;
    }

  state->active = false;
  state->key_mask = 0;
  state->pm_session = 0;
  state->last_result = -ESTALE;
  return 0;
}

int bkengtest_pm_request(struct bkengtest_s *state)
{
  if (state == NULL || !state->active || !state->power_intent)
    {
      return -ECANCELED;
    }

  if (state->pm_session != 0 && state->pm_session != state->session)
    {
      return -ESTALE;
    }

  state->pm_session = state->session;
  state->pm_requests++;
  switch (state->pm_mode)
    {
      case BKENGTEST_PM_DECLINED:
      case BKENGTEST_PM_UNKNOWN:
      case BKENGTEST_PM_LATE_ACK:
        return -ETIMEDOUT;
      case BKENGTEST_PM_PENDING:
        return 0;
      default:
        return -ECANCELED;
    }
}

int bkengtest_pm_status(struct bkengtest_s *state)
{
  if (state == NULL || !state->active || !state->power_intent ||
      state->pm_session != state->session)
    {
      return 0;
    }

  state->pm_queries++;
  switch (state->pm_mode)
    {
      case BKENGTEST_PM_UNKNOWN:
        return -ETIMEDOUT;
      case BKENGTEST_PM_PENDING:
        return 1;
      case BKENGTEST_PM_LATE_ACK:
        return state->pm_queries >= 2 ? 1 : -ETIMEDOUT;
      default:
        return 0;
    }
}

static int audio_report_valid(const struct bkengaudio_report_s *report)
{
  return report->result == 0 && report->stages == 3 &&
         report->accepted_bytes == 3u * BKENGAUDIO_STAGE_BYTES &&
         report->eof_result == 0 && report->eof_close == 0 &&
         report->cancel_write == -ECANCELED &&
         report->cancel_drain == -ECANCELED &&
         report->cancel_close == 0 && report->next_result == 0 &&
         report->next_close == 0;
}

static int audio_apply(struct bkengaudio_s *state,
                       const struct bkengtest_ops_s *ops, void *context,
                       const uint8_t record[BKENGAUDIO_RECORD_SIZE])
{
  uint32_t operation;
  uint32_t session;
  uint32_t sequence;
  uint32_t flags;
  int ret;

  if (memcmp(record, "BKA1", 4) != 0 ||
      get32(record + 4) != BKENGAUDIO_VERSION)
    {
      return -EPROTO;
    }

  operation = get32(record + 8);
  session = get32(record + 12);
  sequence = get32(record + 16);
  flags = get32(record + 20);
  if (operation != BKENGAUDIO_OP_RUN || session == 0 || sequence != 1 ||
      flags != 0 || get32(record + 24) != 0 || get32(record + 28) != 0)
    {
      return -EINVAL;
    }

  if (state->state == BKENGAUDIO_RUNNING)
    {
      return -EBUSY;
    }

  if (state->session != 0 && session <= state->session)
    {
      return session == state->session ? -EALREADY : -ESTALE;
    }

  memset(&state->report, 0, sizeof(state->report));
  state->session = session;
  state->sequence = sequence;
  state->state = BKENGAUDIO_RUNNING;
  ret = ops->audio_run(context, &state->report);
  if (ret == 0 && !audio_report_valid(&state->report))
    {
      ret = state->report.result < 0 ? state->report.result : -EIO;
    }

  if (ret < 0 && state->report.result == 0)
    {
      state->report.result = ret;
    }

  state->state = ret < 0 ? BKENGAUDIO_FAILED : BKENGAUDIO_COMPLETE;
  return ret;
}

static int audio_status_read(const struct bkengaudio_s *state,
                             uint32_t offset,
                             struct bkcontrol_status_s *status)
{
  uint8_t wire[BKENGAUDIO_STATUS_SIZE];

  if ((offset & 15u) != 0 || offset >= sizeof(wire))
    {
      return -ERANGE;
    }

  memset(wire, 0, sizeof(wire));
  memcpy(wire, "BAS1", 4);
  put32(wire + 4, BKENGAUDIO_VERSION);
  put32(wire + BKENGAUDIO_STATUS_STATE_OFFSET, state->state);
  put32(wire + BKENGAUDIO_STATUS_SESSION_OFFSET, state->session);
  put32(wire + BKENGAUDIO_STATUS_SEQUENCE_OFFSET, state->sequence);
  put32(wire + BKENGAUDIO_STATUS_STAGES_OFFSET, state->report.stages);
  put32(wire + BKENGAUDIO_STATUS_ACCEPTED_OFFSET,
        state->report.accepted_bytes);
  put32(wire + BKENGAUDIO_STATUS_RESULT_OFFSET,
        (uint32_t)state->report.result);
  put32(wire + BKENGAUDIO_STATUS_EOF_RESULT_OFFSET,
        (uint32_t)state->report.eof_result);
  put32(wire + BKENGAUDIO_STATUS_EOF_CLOSE_OFFSET,
        (uint32_t)state->report.eof_close);
  put32(wire + BKENGAUDIO_STATUS_CANCEL_WRITE_OFFSET,
        (uint32_t)state->report.cancel_write);
  put32(wire + BKENGAUDIO_STATUS_CANCEL_DRAIN_OFFSET,
        (uint32_t)state->report.cancel_drain);
  put32(wire + BKENGAUDIO_STATUS_CANCEL_CLOSE_OFFSET,
        (uint32_t)state->report.cancel_close);
  put32(wire + BKENGAUDIO_STATUS_NEXT_RESULT_OFFSET,
        (uint32_t)state->report.next_result);
  put32(wire + BKENGAUDIO_STATUS_NEXT_CLOSE_OFFSET,
        (uint32_t)state->report.next_close);
  status->config_total = sizeof(wire);
  memcpy(status->config_chunk, wire + offset, sizeof(status->config_chunk));
  return 0;
}

int bkengaudio_control(struct bkengaudio_s *state,
                       const struct bkengtest_ops_s *ops, void *context,
                       enum bkcontrol_command_e command, uint32_t offset,
                       const uint8_t *record, size_t size,
                       struct bkcontrol_status_s *status)
{
  if (state == NULL || ops == NULL || ops->audio_run == NULL ||
      status == NULL)
    {
      return -EINVAL;
    }

  if (command == BKCONTROL_CONFIG_READ)
    {
      if (record != NULL || size != 0)
        {
          return -EINVAL;
        }

      return audio_status_read(state, offset, status);
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return offset == 0 && record == NULL &&
             size == BKENGAUDIO_RECORD_SIZE ? 0 : -EINVAL;
    }

  if (command != BKCONTROL_CONFIG_APPLY || offset != 0 || record == NULL ||
      size != BKENGAUDIO_RECORD_SIZE)
    {
      return -ENOTSUP;
    }

  return audio_apply(state, ops, context, record);
}
