/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_PLAYBACK_H
#define __APP_BK7258_CLOUD_PLAYBACK_H
#include "bk7258_voice_turn.h"
/* One serialized turn owner. This adapter borrows the existing arbiter and
 * never opens a second player. End schedules drain; owner must still poll
 * the arbiter for playback completion before committing dialogue history.
 */
struct bkcloud_playback_s
{
  struct bkvoice_turn_s *turn;
  struct bkvoice_turn_token_s token;
  uint64_t (*now_ms)(void *context);
  void *clock_context;
  uint8_t frame[1280];
  size_t frame_size;
  uint64_t input_bytes;
  int error;
  bool active;
};
int bkcloud_playback_begin(struct bkcloud_playback_s *play,
                           struct bkvoice_turn_s *turn,
                           uint64_t (*now_ms)(void *), void *context);
/* Input: 24 kHz mono signed PCM16-LE, arbitrarily fragmented bytes.
 * A split sample is retained until the next feed; end rejects a trailing byte.
 */
int bkcloud_playback_feed(void *context, const void *pcm, size_t size);
int bkcloud_playback_end(struct bkcloud_playback_s *play);
void bkcloud_playback_abort(struct bkcloud_playback_s *play, int reason);
#endif
