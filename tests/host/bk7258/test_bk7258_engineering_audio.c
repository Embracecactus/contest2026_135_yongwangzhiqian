/* SPDX-License-Identifier: Apache-2.0 */
/* BKA1/BAS1 behavior.  The callback is the product adapter boundary; the
 * deployed audio_playback implementation has its own linked integration test. */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_engineering_test.h"

static unsigned int runs;

static void put32(uint8_t *p, uint32_t value)
{
  p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
}

static int audio_run(void *unused, struct bkengaudio_report_s *report)
{
  (void)unused;
  runs++;
  memset(report, 0, sizeof(*report));
  report->stages = 3;
  report->accepted_bytes = 3 * BKENGAUDIO_STAGE_BYTES;
  report->cancel_write = -ECANCELED;
  report->cancel_drain = -ECANCELED;
  return 0;
}

static const struct bkengtest_ops_s ops = { .audio_run = audio_run };

static int apply(struct bkengaudio_s *state, uint32_t session,
                 uint32_t sequence, uint32_t flags)
{
  uint8_t record[BKENGAUDIO_RECORD_SIZE] = {'B','K','A','1'};
  struct bkcontrol_status_s status = {0};
  put32(record + 4, BKENGAUDIO_VERSION);
  put32(record + 8, BKENGAUDIO_OP_RUN);
  put32(record + 12, session);
  put32(record + 16, sequence);
  put32(record + 20, flags);
  assert(bkengaudio_control(state, &ops, NULL, BKCONTROL_CONFIG_BEGIN,
                            0, NULL, sizeof(record), &status) == 0);
  return bkengaudio_control(state, &ops, NULL, BKCONTROL_CONFIG_APPLY,
                            0, record, sizeof(record), &status);
}

static void read_status(struct bkengaudio_s *state, uint8_t *wire)
{
  for (uint32_t offset = 0; offset < BKENGAUDIO_STATUS_SIZE; offset += 16)
    {
      struct bkcontrol_status_s status = {0};
      assert(bkengaudio_control(state, &ops, NULL, BKCONTROL_CONFIG_READ,
                                offset, NULL, 0, &status) == 0);
      assert(status.config_total == BKENGAUDIO_STATUS_SIZE);
      memcpy(wire + offset, status.config_chunk, 16);
    }
}

static void run_case(void)
{
  struct bkengaudio_s state = {0};
  uint8_t wire[BKENGAUDIO_STATUS_SIZE];
  assert(apply(&state, 7, 1, 0) == 0);
  assert(runs == 1);
  read_status(&state, wire);
  assert(!memcmp(wire, "BAS1", 4));
  assert(get32(wire + 4) == BKENGAUDIO_VERSION);
  assert(get32(wire + BKENGAUDIO_STATUS_STATE_OFFSET) ==
         BKENGAUDIO_COMPLETE);
  assert(get32(wire + BKENGAUDIO_STATUS_SESSION_OFFSET) == 7);
  assert(get32(wire + BKENGAUDIO_STATUS_SEQUENCE_OFFSET) == 1);
  assert(get32(wire + BKENGAUDIO_STATUS_STAGES_OFFSET) == 3);
  assert(get32(wire + BKENGAUDIO_STATUS_ACCEPTED_OFFSET) ==
         3 * BKENGAUDIO_STAGE_BYTES);
  assert((int32_t)get32(wire + BKENGAUDIO_STATUS_RESULT_OFFSET) == 0);
  assert((int32_t)get32(wire + BKENGAUDIO_STATUS_EOF_RESULT_OFFSET) == 0);
  assert((int32_t)get32(wire + BKENGAUDIO_STATUS_EOF_CLOSE_OFFSET) == 0);
  assert((int32_t)get32(wire + BKENGAUDIO_STATUS_CANCEL_WRITE_OFFSET) ==
         -ECANCELED);
  assert((int32_t)get32(wire + BKENGAUDIO_STATUS_CANCEL_DRAIN_OFFSET) ==
         -ECANCELED);
  assert((int32_t)get32(wire + BKENGAUDIO_STATUS_CANCEL_CLOSE_OFFSET) == 0);
  assert((int32_t)get32(wire + BKENGAUDIO_STATUS_NEXT_RESULT_OFFSET) == 0);
  assert((int32_t)get32(wire + BKENGAUDIO_STATUS_NEXT_CLOSE_OFFSET) == 0);
  assert(apply(&state, 8, 1, 0) == 0);
  assert(runs == 2);
}

static void invalid_case(void)
{
  struct bkengaudio_s state = {0};
  assert(apply(&state, 0, 1, 0) == -EINVAL);
  assert(apply(&state, 1, 2, 0) == -EINVAL);
  assert(apply(&state, 1, 1, 1) == -EINVAL);
  assert(runs == 0);
}

static int failed_run(void *unused, struct bkengaudio_report_s *report)
{
  (void)unused;
  memset(report, 0, sizeof(*report));
  report->result = -ETIMEDOUT;
  return -ETIMEDOUT;
}

static void failure_case(void)
{
  struct bkengaudio_s state = {0};
  struct bkengtest_ops_s failed = { .audio_run = failed_run };
  uint8_t record[BKENGAUDIO_RECORD_SIZE] = {'B','K','A','1'};
  struct bkcontrol_status_s status = {0};
  put32(record + 4, BKENGAUDIO_VERSION);
  put32(record + 8, BKENGAUDIO_OP_RUN);
  put32(record + 12, 9);
  put32(record + 16, 1);
  assert(bkengaudio_control(&state, &failed, NULL, BKCONTROL_CONFIG_BEGIN,
                            0, NULL, sizeof(record), &status) == 0);
  assert(bkengaudio_control(&state, &failed, NULL, BKCONTROL_CONFIG_APPLY,
                            0, record, sizeof(record), &status) == -ETIMEDOUT);
  assert(state.state == BKENGAUDIO_FAILED);
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  if (!strcmp(argv[1], "run")) run_case();
  else if (!strcmp(argv[1], "invalid")) invalid_case();
  else if (!strcmp(argv[1], "failure")) failure_case();
  else return 2;
  puts("CONTRACT_PASS");
  return 0;
}
