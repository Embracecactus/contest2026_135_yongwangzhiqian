/****************************************************************************
 * app/bk7258/bk7258_voice_turn_audio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * App-private BKVoice adapter for the public OpenVela media ABI.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_TURN_AUDIO_H
#define __APP_BK7258_BK7258_VOICE_TURN_AUDIO_H

#include "bk7258_voice_capture.h"
#include "bk7258_voice_turn.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct bkvoice_turn_audio_s
{
  void *mic_handle;
  void *dac_handle;
  bool mic_prepared;
  bool mic_started;
  bool mic_policy_active;
  volatile bool mic_reader_active;
  bool dac_prepared;
  bool dac_started;
  bool dac_completed;
  int dac_result;
  bool volume_override;
  unsigned int volume_percent;
};

/* Initialization does not open a device.  All operations are serialized by
 * the turn owner.  read() is intentionally not connected to a background
 * capture task here; a future transport slice must stop and join that task
 * before the arbiter is allowed to drain/release the recorder.
 */

int bkvoice_turn_audio_initialize(struct bkvoice_turn_audio_s *audio);
const struct bkvoice_turn_audio_ops_s *bkvoice_turn_audio_ops(void);
int bkvoice_turn_audio_reader_attach(struct bkvoice_turn_audio_s *audio);
int bkvoice_turn_audio_reader_detach(struct bkvoice_turn_audio_s *audio);
ssize_t bkvoice_turn_audio_read(struct bkvoice_turn_audio_s *audio,
                                void *pcm, size_t bytes);
int bkvoice_turn_audio_reader_stop(struct bkvoice_turn_audio_s *audio);
const struct bkvoice_capture_source_ops_s *
  bkvoice_turn_audio_capture_source_ops(void);
bool bkvoice_turn_audio_released(const struct bkvoice_turn_audio_s *audio);

#endif /* __APP_BK7258_BK7258_VOICE_TURN_AUDIO_H */
