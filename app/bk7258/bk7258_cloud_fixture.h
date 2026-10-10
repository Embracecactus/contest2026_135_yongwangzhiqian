/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_FIXTURE_H
#define __APP_BK7258_CLOUD_FIXTURE_H

#include "bk7258_voice_transport.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

enum bkcloud_fixture_role_e { BKCLOUD_FIXTURE_ASR, BKCLOUD_FIXTURE_LLM,
  BKCLOUD_FIXTURE_TTS };
enum bkcloud_fixture_mode_e { BKCLOUD_FIXTURE_NORMAL,
  BKCLOUD_FIXTURE_CANCEL_TAIL, BKCLOUD_FIXTURE_FOCUS_START,
  BKCLOUD_FIXTURE_FOCUS_STATUS, BKCLOUD_FIXTURE_FOCUS_PAUSE,
  BKCLOUD_FIXTURE_FOCUS_RESUME, BKCLOUD_FIXTURE_FOCUS_CANCEL,
  BKCLOUD_FIXTURE_FOCUS_FINISH };

struct bkcloud_fixture_ctx_s
{
  enum bkcloud_fixture_role_e role;
  enum bkcloud_fixture_mode_e mode;
  bool opened;
  atomic_bool canceled;
  bool stream;
  bool reply_ready;
  size_t sent;
  size_t offset;
  size_t tail_offset;
  uint32_t source_hash;
  char scan[32];
  size_t scan_size;
  char reply[12000];
  size_t reply_size;
};

struct bkcloud_fixture_report_s
{
  unsigned int asr_requests, plan_requests, final_requests, tts_requests;
  bool tail_released_after_media;
  size_t source_bytes;
  uint32_t source_hash;
};
struct bkcloud_fixture_pcm_expectation_s
{
  size_t source_bytes[2];
  uint32_t source_hash[2];
  size_t expected_16k_bytes[2];
  uint32_t expected_16k_hash[2];
};

int bkcloud_fixture_begin(struct bkcloud_fixture_ctx_s *ctx,
  enum bkcloud_fixture_role_e role, enum bkcloud_fixture_mode_e mode);
const struct bkvoice_wss_tls_ops_s *bkcloud_fixture_tls_ops(void);
int bkcloud_fixture_media_started(void);
int bkcloud_fixture_cancel(struct bkcloud_fixture_ctx_s *ctx);
int bkcloud_fixture_end(struct bkcloud_fixture_ctx_s *ctx);
int bkcloud_fixture_reset(enum bkcloud_fixture_mode_e mode);
int bkcloud_fixture_report(struct bkcloud_fixture_report_s *report);
int bkcloud_fixture_pcm_expectation(struct bkcloud_fixture_pcm_expectation_s *out);
#endif
