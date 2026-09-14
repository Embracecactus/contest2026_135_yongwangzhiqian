/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_playback.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

static void release_workspace(struct bkcloud_playback_s *play)
{
  play->active = false;
  memset(play->frame, 0, sizeof(play->frame));
  play->frame_size = 0;
}
void bkcloud_playback_abort(struct bkcloud_playback_s *play, int reason)
{
  if (!play) return;
  if (play->turn && play->active)
    {
      play->token.sequence = play->turn->last_control_sequence + 1;
      (void)bkvoice_turn_cancel(play->turn, &play->token,
                                reason < 0 ? reason : -ECANCELED);
    }
  release_workspace(play);
  play->error = reason < 0 ? reason : -ECANCELED;
}
static int sequence(struct bkcloud_playback_s *play)
{
  if (play->turn->last_downlink_sequence >= UINT32_MAX - 1) return -EOVERFLOW;
  play->token.sequence = play->turn->last_downlink_sequence + 1;
  return 0;
}
static int emit_frame(struct bkcloud_playback_s *play)
{
  int ret = sequence(play);
  if (!ret) ret = bkvoice_turn_tts_audio(play->turn, &play->token, play->frame,
                    play->turn->limits.audio_frame_bytes,
                    play->now_ms(play->clock_context));
  if (!ret) { play->frame_size = 0; memset(play->frame, 0, sizeof(play->frame)); }
  return ret;
}
int bkcloud_playback_begin(struct bkcloud_playback_s *play,
                           struct bkvoice_turn_s *turn,
                           uint64_t (*now_ms)(void *), void *context)
{
  int ret;
  if (!play || !turn || !now_ms || !turn->limits.audio_frame_bytes ||
      turn->limits.audio_frame_bytes > sizeof(play->frame) ||
      turn->limits.audio_frame_bytes % 2) return -EINVAL;
  if (turn->state != BKVOICE_TURN_WAITING_TTS) return -EBUSY;
  /* Caller supplies an inactive workspace. All close/abort operations remain
   * with the same turn owner; the official Media graph owns resampling.
   */
  memset(play, 0, sizeof(*play));
  play->turn = turn;
  play->token = turn->active;
  play->now_ms = now_ms;
  play->clock_context = context;
  ret = sequence(play);
  if (!ret)
    ret = bkvoice_turn_tts_start(turn, &play->token, 24000, now_ms(context));
  if (ret) play->error = ret;
  else play->active = true;
  return ret;
}
int bkcloud_playback_feed(void *context, const void *pcm, size_t size)
{
  struct bkcloud_playback_s *play = context;
  const uint8_t *bytes = pcm;
  if (!play || !play->active) return -EINVAL;
  uint64_t remaining = 90u * 24000u * 2u - play->input_bytes;
  if ((!pcm && size) || size > remaining)
    { bkcloud_playback_abort(play, -EINVAL); return -EINVAL; }
  while (size)
    {
      size_t count = play->turn->limits.audio_frame_bytes - play->frame_size;
      if (count > size) count = size;
      memcpy(play->frame + play->frame_size, bytes, count);
      play->frame_size += count;
      play->input_bytes += count;
      bytes += count;
      size -= count;
      if (play->frame_size == play->turn->limits.audio_frame_bytes)
        {
          int ret = emit_frame(play);
          if (ret) { bkcloud_playback_abort(play, ret); return ret; }
        }
    }
  return 0;
}
int bkcloud_playback_end(struct bkcloud_playback_s *play)
{
  if (!play || !play->active) return -EINVAL;
  if (play->input_bytes % 2u)
    { bkcloud_playback_abort(play, -EBADMSG); return -EBADMSG; }
  int ret = play->input_bytes ? 0 : -ENODATA;
  if (!ret && play->frame_size)
    {
      memset(play->frame + play->frame_size, 0,
              play->turn->limits.audio_frame_bytes - play->frame_size);
      ret = emit_frame(play);
    }
  if (!ret) ret = sequence(play);
  if (!ret) ret = bkvoice_turn_tts_end(play->turn, &play->token);
  if (ret) bkcloud_playback_abort(play, ret);
  else release_workspace(play);
  return ret;
}
