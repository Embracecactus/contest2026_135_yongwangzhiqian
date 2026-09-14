/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_TTS_H
#define __APP_BK7258_CLOUD_TTS_H
#include "bk7258_cloud_request.h"
#include <stdbool.h>
/* MiMo chat-audio SSE decoder. Output is PCM16-LE mono at 24000 Hz;
 * the audio owner must configure that rate or resample before playback.
 * A successful finish is not a playback acknowledgement. Abort/discard
 * queued partial audio when feed or finish fails.
 */
struct bkcloud_tts_s
{
  char *line;
  char *event;
  size_t line_capacity;
  size_t event_capacity;
  uint8_t pcm[3072];
  size_t line_size;
  size_t event_size;
  size_t total;
  size_t audio_events;
  size_t first_audio_bytes;
  size_t max_audio_bytes;
  int error;
  bool stopped;
  bool done;
  bkcloud_write_t write;
  void *context;
};
void bkcloud_tts_init(struct bkcloud_tts_s *tts, bkcloud_write_t write, void *context);
int bkcloud_tts_feed(void *context, const void *data, size_t size);
int bkcloud_tts_finish(struct bkcloud_tts_s *tts);
void bkcloud_tts_clear(struct bkcloud_tts_s *tts);
#endif
