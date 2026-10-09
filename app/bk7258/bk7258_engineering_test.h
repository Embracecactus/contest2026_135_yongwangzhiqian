/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_BK7258_ENGINEERING_TEST_H
#define __APP_BK7258_BK7258_ENGINEERING_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bk7258_control_session.h"

#define BKENGTEST_VERSION             1u
#define BKENGTEST_RECORD_SIZE        32u
#define BKENGTEST_STATUS_SIZE        64u
#define BKENGTEST_SESSION_IDLE_MS 60000u
#define BKENGTEST_ELAPSED_MAX_MS 120000u

#define BKENGAUDIO_VERSION      1u
#define BKENGAUDIO_RECORD_SIZE 32u
#define BKENGAUDIO_STATUS_SIZE 64u
#define BKENGAUDIO_STAGE_BYTES 8192u

#define BKENGAUDIO_STATUS_STATE_OFFSET        8u
#define BKENGAUDIO_STATUS_SESSION_OFFSET      12u
#define BKENGAUDIO_STATUS_SEQUENCE_OFFSET     16u
#define BKENGAUDIO_STATUS_STAGES_OFFSET       20u
#define BKENGAUDIO_STATUS_ACCEPTED_OFFSET     24u
#define BKENGAUDIO_STATUS_RESULT_OFFSET       28u
#define BKENGAUDIO_STATUS_EOF_RESULT_OFFSET   32u
#define BKENGAUDIO_STATUS_EOF_CLOSE_OFFSET    36u
#define BKENGAUDIO_STATUS_CANCEL_WRITE_OFFSET 40u
#define BKENGAUDIO_STATUS_CANCEL_DRAIN_OFFSET 44u
#define BKENGAUDIO_STATUS_CANCEL_CLOSE_OFFSET 48u
#define BKENGAUDIO_STATUS_NEXT_RESULT_OFFSET  52u
#define BKENGAUDIO_STATUS_NEXT_CLOSE_OFFSET   56u

#define BKENGTEST_STATUS_ENABLED 1u
#define BKENGTEST_STATUS_ACTIVE  2u
#define BKENGTEST_STATUS_EXPIRED 4u
#define BKENGTEST_STATUS_POWER_INTENT 8u

enum bkengtest_operation_e
{
  BKENGTEST_OP_SESSION = 1,
  BKENGTEST_OP_KEY,
  BKENGTEST_OP_ADVANCE,
  BKENGTEST_OP_END,
};

enum bkengtest_pm_mode_e
{
  BKENGTEST_PM_BLOCKED = 0,
  BKENGTEST_PM_DECLINED,
  BKENGTEST_PM_UNKNOWN,
  BKENGTEST_PM_PENDING,
  BKENGTEST_PM_LATE_ACK,
};

enum bkengtest_voice_state_e
{
  BKENGTEST_VOICE_UNAVAILABLE = 0,
  BKENGTEST_VOICE_IDLE,
  BKENGTEST_VOICE_BUSY,
};

enum bkengtest_storage_state_e
{
  BKENGTEST_STORAGE_UNAVAILABLE = 0,
  BKENGTEST_STORAGE_READY,
};

enum bkengaudio_operation_e
{
  BKENGAUDIO_OP_RUN = 1,
};

enum bkengaudio_state_e
{
  BKENGAUDIO_IDLE = 0,
  BKENGAUDIO_RUNNING,
  BKENGAUDIO_COMPLETE,
  BKENGAUDIO_FAILED,
};

struct bkengaudio_report_s
{
  uint32_t stages;
  uint32_t accepted_bytes;
  int32_t result;
  int32_t eof_result;
  int32_t eof_close;
  int32_t cancel_write;
  int32_t cancel_drain;
  int32_t cancel_close;
  int32_t next_result;
  int32_t next_close;
};

enum bkengtest_network_state_e
{
  BKENGTEST_NETWORK_OFFLINE = 0,
  BKENGTEST_NETWORK_LINK,
  BKENGTEST_NETWORK_READY,
};

struct bkengtest_ops_s
{
  uint64_t (*now_ms)(void *context);
  int (*key_begin)(void *context, uint32_t session, uint64_t now);
  int (*key_event)(void *context, uint32_t session, uint32_t sequence,
                   uint32_t pressed, uint64_t now, bool *power_accepted);
  int (*key_end)(void *context, uint32_t session);
  int (*power_status)(void *context, uint32_t *state, int32_t *error);
  int (*system_status)(void *context, uint32_t *voice, uint32_t *storage,
                       uint32_t *network);
  int (*audio_run)(void *context, struct bkengaudio_report_s *report);
};

struct bkengaudio_s
{
  struct bkengaudio_report_s report;
  uint32_t session;
  uint32_t sequence;
  uint32_t state;
};

struct bkengtest_s
{
  uint64_t base_ms;
  uint64_t expires_at;
  uint32_t session;
  uint32_t sequence;
  uint32_t event_sequence;
  uint32_t elapsed_ms;
  uint32_t key_mask;
  uint32_t pm_mode;
  uint32_t pm_requests;
  uint32_t pm_queries;
  uint32_t pm_session;
  int32_t last_result;
  bool active;
  bool expired;
  bool power_intent;
};

int bkengtest_control(struct bkengtest_s *state,
                      const struct bkengtest_ops_s *ops, void *context,
                      enum bkcontrol_command_e command, uint32_t offset,
                      const uint8_t *record, size_t size,
                      struct bkcontrol_status_s *status);
void bkengtest_step(struct bkengtest_s *state,
                    const struct bkengtest_ops_s *ops, void *context,
                    uint64_t now);
int bkengtest_disconnect(struct bkengtest_s *state,
                         const struct bkengtest_ops_s *ops, void *context);
int bkengtest_pm_request(struct bkengtest_s *state);
int bkengtest_pm_status(struct bkengtest_s *state);
int bkengaudio_control(struct bkengaudio_s *state,
                       const struct bkengtest_ops_s *ops, void *context,
                       enum bkcontrol_command_e command, uint32_t offset,
                       const uint8_t *record, size_t size,
                       struct bkcontrol_status_s *status);

#endif /* __APP_BK7258_BK7258_ENGINEERING_TEST_H */
